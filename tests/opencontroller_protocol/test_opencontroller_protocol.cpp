// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <vector>

#include "gtest/gtest.h"

#include "opencontroller_protocol.h"

namespace {

using Frame = std::vector<uint8_t>;

struct Capture {
    std::vector<Frame> frames;
    bool               accept = true;
};

bool capture_send(const uint8_t *data, uint8_t length, void *context) {
    auto *capture = static_cast<Capture *>(context);
    if (!capture->accept) {
        return false;
    }
    capture->frames.emplace_back(data, data + length);
    return true;
}

void feed_ack(uint32_t now_ms) {
    ocp_feed_byte(0x61, now_ms);
    ocp_feed_byte(0x0D, now_ms);
    ocp_feed_byte(0x0A, now_ms);
}

void feed_status(uint8_t status, uint32_t now_ms) {
    ocp_feed_byte(0x5B, now_ms);
    ocp_feed_byte(status, now_ms);
    ocp_feed_byte(static_cast<uint8_t>(0x5B + status), now_ms);
}

/* Drives the A6 56 unlock through its ACK and 5B 37 reply, then discards the
 * frames it produced so the test under way sees only its own traffic. */
void negotiate_ready(Capture &capture, uint32_t now_ms) {
    ASSERT_TRUE(ocp_queue_sleep_negotiate());
    ocp_service(now_ms, capture_send, &capture);
    ASSERT_EQ(capture.frames.back(), (Frame{0xA6, 0x56, 0xFC}));
    feed_ack(now_ms);
    feed_status(0x37, now_ms);
    ocp_service(now_ms, capture_send, &capture); // The reply ACK for 5B 37.
    ASSERT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_READY);
    capture.frames.clear();
}

void arm_autosleep(Capture &capture, uint32_t now_ms) {
    ASSERT_TRUE(ocp_queue_autosleep_arm());
    ocp_service(now_ms, capture_send, &capture);
    ASSERT_EQ(capture.frames.back(), (Frame{0xA6, 0x57, 0xFD}));
    feed_ack(now_ms);
    ASSERT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_ARMED);
    capture.frames.clear();
}

class OpenControllerProtocol : public testing::Test {
   protected:
    void SetUp() override {
        ocp_init();
    }
};

TEST_F(OpenControllerProtocol, ControlFramesUseAdditiveChecksumAndThreeTotalAttempts) {
    Capture capture;

    ASSERT_TRUE(ocp_queue_control(0x30));
    ASSERT_TRUE(ocp_queue_control(0x51));
    EXPECT_TRUE(ocp_actions_pending());

    ocp_service(0, capture_send, &capture);
    ocp_service(19, capture_send, &capture);
    ocp_service(20, capture_send, &capture);
    ocp_service(40, capture_send, &capture);
    ocp_service(60, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 3U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA6, 0x30, 0xD6}));
    EXPECT_EQ(capture.frames[1], capture.frames[0]);
    EXPECT_EQ(capture.frames[2], capture.frames[0]);
    EXPECT_EQ(ocp_get_link_state(), OCP_LINK_DISCONNECTED);
    EXPECT_EQ(ocp_get_diagnostics()->tx_ack_timeouts, 1);
    EXPECT_FALSE(ocp_actions_pending());
    EXPECT_TRUE(ocp_take_tx_failure());
    EXPECT_FALSE(ocp_take_tx_failure());

    // The queued pair command was aborted with the failed transaction.
    ocp_service(80, capture_send, &capture);
    EXPECT_EQ(capture.frames.size(), 3U);
}

TEST_F(OpenControllerProtocol, ParserResynchronizesAndExpiresPartialFrames) {
    Capture capture;

    ocp_feed_byte(0x5B, 0);
    ocp_feed_byte(0x5A, 1);
    ocp_feed_byte(0x02, 1); // Bad status frame retains the LED header.
    ocp_feed_byte(0x5C, 1); // 0x5A + 0x02

    EXPECT_EQ(ocp_get_keyboard_leds(), 0x02);
    EXPECT_EQ(ocp_get_diagnostics()->rx_checksum_errors, 1);

    ocp_service(1, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0x61, 0x0D, 0x0A}));

    ocp_feed_byte(0x5B, 2);
    feed_status(0x32, 22);
    EXPECT_EQ(ocp_get_diagnostics()->rx_partial_timeouts, 1);
    EXPECT_EQ(ocp_get_link_state(), OCP_LINK_CONNECTED);
    EXPECT_EQ(ocp_get_connection_generation(), 1);
}

TEST_F(OpenControllerProtocol, ReleaseBarrierIsAckedAndDwellsBeforeQueuedControls) {
    Capture       capture;
    const uint8_t release_command[] = {0x11};

    ASSERT_TRUE(ocp_queue_release_then_controls(release_command, sizeof(release_command)));
    ASSERT_TRUE(ocp_queue_control(0x30));
    EXPECT_EQ(ocp_queue_available(), 5);
    EXPECT_TRUE(ocp_actions_pending());

    ocp_service(0, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA1, 0, 0, 0, 0, 0, 0, 0, 0, 0xA1}));

    feed_ack(1);
    ocp_service(8, capture_send, &capture);
    EXPECT_EQ(capture.frames.size(), 1U);

    ocp_service(9, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x11, 0xB7}));

    feed_ack(10);
    ocp_service(10, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 3U);
    EXPECT_EQ(capture.frames[2], (Frame{0xA6, 0x30, 0xD6}));
    EXPECT_EQ(ocp_queue_available(), 8);
    EXPECT_TRUE(ocp_actions_pending());

    feed_ack(11);
    EXPECT_FALSE(ocp_actions_pending());
}

TEST_F(OpenControllerProtocol, ReleaseDwellIsTimerWrapSafe) {
    Capture       capture;
    const uint8_t command[] = {0x11};

    ASSERT_TRUE(ocp_queue_release_then_controls(command, sizeof(command)));
    ocp_service(UINT32_MAX - 4, capture_send, &capture);
    feed_ack(UINT32_MAX - 3);

    ocp_service(3, capture_send, &capture); // Seven milliseconds after ACK.
    EXPECT_EQ(capture.frames.size(), 1U);

    ocp_service(4, capture_send, &capture); // Eight milliseconds after ACK.
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x11, 0xB7}));
}

TEST_F(OpenControllerProtocol, DestructiveBarrierSequenceEnqueueIsAtomic) {
    Capture       capture;
    const uint8_t queued_controls[]      = {0x30, 0x30, 0x30, 0x30, 0x30, 0x30};
    const uint8_t destructive_controls[] = {0x30, 0x52, 0x51};

    ASSERT_TRUE(ocp_queue_control_sequence(queued_controls, sizeof(queued_controls)));
    ASSERT_EQ(ocp_queue_available(), 2);

    EXPECT_FALSE(ocp_queue_release_then_controls(destructive_controls, sizeof(destructive_controls)));
    EXPECT_EQ(ocp_queue_available(), 2);
    EXPECT_EQ(ocp_get_diagnostics()->control_queue_overflows, 1);

    for (uint32_t now_ms = 0; now_ms < 6; ++now_ms) {
        ocp_service(now_ms, capture_send, &capture);
        feed_ack(now_ms);
    }

    ASSERT_EQ(capture.frames.size(), 6U);
    for (const Frame &frame : capture.frames) {
        EXPECT_EQ(frame, (Frame{0xA6, 0x30, 0xD6}));
    }
    EXPECT_EQ(ocp_queue_available(), 8);
    EXPECT_TRUE(ocp_queue_release_then_controls(destructive_controls, sizeof(destructive_controls)));
    ocp_service(10, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 7U);
    EXPECT_EQ(capture.frames[6], (Frame{0xA1, 0, 0, 0, 0, 0, 0, 0, 0, 0xA1}));
}

namespace {
Frame report_of(const Frame &frame) {
    return Frame(frame.begin() + 1, frame.begin() + 1 + OCP_KEYBOARD_REPORT_SIZE);
}
} // namespace

TEST_F(OpenControllerProtocol, KeyboardReportsQueueInOrderAndRepeatsAreNotTransitions) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> a     = {0, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> ab    = {0, 0, 0x04, 0x05, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> empty = {0};

    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture); // ACK the status frame.
    capture.frames.clear();

    ocp_set_keyboard_report(a.data());
    ocp_set_keyboard_report(a.data());  // the same state again: not a transition
    ocp_set_keyboard_report(ab.data());
    ocp_set_keyboard_report(empty.data());
    EXPECT_EQ(ocp_keyboard_queue_count(), 3U);

    for (uint32_t t = 1; t <= 3; ++t) {
        ocp_service(t, capture_send, &capture);
        feed_ack(t);
    }
    ocp_service(4, capture_send, &capture);

    // Every transition, in order, each waiting for the previous ACK.
    ASSERT_EQ(capture.frames.size(), 3U);
    EXPECT_EQ(report_of(capture.frames[0]), Frame(a.begin(), a.end()));
    EXPECT_EQ(report_of(capture.frames[1]), Frame(ab.begin(), ab.end()));
    EXPECT_EQ(report_of(capture.frames[2]), Frame(empty.begin(), empty.end()));
    EXPECT_EQ(ocp_keyboard_queue_count(), 0U);
    EXPECT_TRUE(ocp_is_idle());
}

TEST_F(OpenControllerProtocol, ATapDuringTheReconnectKeepsBothItsPressAndItsRelease) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> down  = {0, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> empty = {0};

    feed_status(0x33, 0); // the module tore the link down
    ocp_service(0, capture_send, &capture);
    capture.frames.clear();

    ocp_set_keyboard_report(down.data());  // press and release while the link is down:
    ocp_set_keyboard_report(empty.data()); // the single slot used to keep only the release
    ocp_service(1, capture_send, &capture);
    EXPECT_TRUE(capture.frames.empty()); // nothing goes out while DISCONNECTED
    EXPECT_EQ(ocp_keyboard_queue_count(), 2U);

    feed_status(0x35, 2); // RECONNECTING: the module's FIFO takes reports now
    ocp_service(2, capture_send, &capture);
    capture.frames.clear();
    ocp_service(3, capture_send, &capture);
    feed_ack(3);
    ocp_service(4, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(report_of(capture.frames[0]), Frame(down.begin(), down.end()));
    EXPECT_EQ(report_of(capture.frames[1]), Frame(empty.begin(), empty.end()));
}

TEST_F(OpenControllerProtocol, ReconnectResyncResendsTheHeldStateOnceWhenNothingWasQueued) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> held = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> live = {0x02, 0, 0x05, 0, 0, 0, 0, 0};

    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture);
    ocp_set_keyboard_report(held.data()); // held since before the outage
    ocp_service(1, capture_send, &capture);
    feed_ack(1);
    feed_status(0x33, 2); // outage: the dongle released every key on the host
    ocp_service(2, capture_send, &capture);
    feed_status(0x32, 3); // back
    ocp_service(3, capture_send, &capture);
    capture.frames.clear();

    ocp_begin_keyboard_resync(held.data()); // the matrix still holds it
    ocp_set_keyboard_report(live.data());
    ocp_service(4, capture_send, &capture);
    feed_ack(4);
    ocp_service(5, capture_send, &capture);
    feed_ack(5);
    ocp_service(6, capture_send, &capture);

    // The held state exactly once (a repeat of the newest state would otherwise
    // be dropped), then the live report; no empty report in between.
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(report_of(capture.frames[0]), Frame(held.begin(), held.end()));
    EXPECT_EQ(report_of(capture.frames[1]), Frame(live.begin(), live.end()));
}

TEST_F(OpenControllerProtocol, ReconnectResyncAddsNoSecondReportWhenTheQueuedTransitionCarriesTheState) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> down = {0, 0, 0x04, 0, 0, 0, 0, 0};

    feed_status(0x33, 0);
    ocp_service(0, capture_send, &capture);
    ocp_set_keyboard_report(down.data()); // pressed during the outage, still held
    feed_status(0x32, 1);
    ocp_service(1, capture_send, &capture);
    capture.frames.clear();

    ocp_begin_keyboard_resync(down.data());
    EXPECT_EQ(ocp_keyboard_queue_count(), 1U); // nothing added: the queued press carries the state
    ocp_service(2, capture_send, &capture);
    feed_ack(2);
    ocp_service(3, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 1U); // one report on the wire, not a second assertion of the same state
    EXPECT_EQ(report_of(capture.frames[0]), Frame(down.begin(), down.end()));
}

/* A session forming after a module reboot is announced as 0x34 then 0x35 with
 * no 0x33: either status alone must invalidate what was sent, so they are
 * tested separately (codex). */
class ModuleRebootStatus : public OpenControllerProtocol, public testing::WithParamInterface<uint8_t> {};

TEST_P(ModuleRebootStatus, AModuleRebootWithoutADisconnectStillGetsTheResync) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> held = {0, 0, 0x04, 0, 0, 0, 0, 0};

    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture);
    ocp_set_keyboard_report(held.data());
    ocp_service(1, capture_send, &capture);
    feed_ack(1); // the held key reached the module: sent-since-link-loss is set

    feed_status(GetParam(), 2); // the module rebooted straight into a forming session
    ocp_service(2, capture_send, &capture);
    feed_status(0x32, 3);
    ocp_service(3, capture_send, &capture);
    capture.frames.clear();

    ocp_begin_keyboard_resync(held.data());
    ocp_service(4, capture_send, &capture);

    // The dongle released the key and the module's FIFO is gone: re-assert.
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(report_of(capture.frames[0]), Frame(held.begin(), held.end()));
}

INSTANTIATE_TEST_SUITE_P(TransportSelectedOrReconnecting, ModuleRebootStatus, testing::Values(0x34, 0x35));

TEST_F(OpenControllerProtocol, TheResyncComparesTheSnapshotBeforeSuppressingIt) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> a = {0, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> b = {0, 0, 0x05, 0, 0, 0, 0, 0};

    feed_status(0x33, 0); // link down; a transition is queued but cannot drain
    ocp_service(0, capture_send, &capture);
    ocp_set_keyboard_report(a.data());
    ASSERT_EQ(ocp_keyboard_queue_count(), 1U);

    // The matrix moved on without this queue seeing it (AUTO sent those
    // reports to USB): the snapshot no longer matches what is queued, so the
    // resync must queue it rather than trust the queue to carry the state.
    ocp_begin_keyboard_resync(b.data());
    EXPECT_EQ(ocp_keyboard_queue_count(), 2U);

    feed_status(0x32, 1);
    ocp_service(1, capture_send, &capture);
    capture.frames.clear();
    for (uint32_t t = 2; t <= 3; ++t) {
        ocp_service(t, capture_send, &capture);
        feed_ack(t);
    }
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(report_of(capture.frames[1]), Frame(b.begin(), b.end()));
}

TEST_F(OpenControllerProtocol, AReleaseBarrierDoesNotSwallowARealReleaseQueuedBesideIt) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> down  = {0, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> empty = {0};
    const uint8_t                                       controls[] = {0x11, 0x30};

    feed_status(0x33, 0); // torn down; a tap arrives and the driver re-drives the link
    ocp_service(0, capture_send, &capture);
    capture.frames.clear();
    ocp_set_keyboard_report(down.data());
    ASSERT_TRUE(ocp_queue_release_then_controls(controls, sizeof(controls)));
    ocp_service(1, capture_send, &capture); // the barrier goes out ...
    ocp_set_keyboard_report(empty.data());  // ... in the same millisecond the tap's release arrives
    feed_ack(1);
    ocp_service(15, capture_send, &capture);
    feed_ack(15);
    ocp_service(16, capture_send, &capture);
    feed_ack(16);
    feed_status(0x35, 17);
    ocp_service(17, capture_send, &capture);
    capture.frames.clear();
    for (uint32_t t = 18; t <= 20; ++t) {
        ocp_service(t, capture_send, &capture);
        feed_ack(t);
    }

    // Both the press and the release are still delivered, in order: the
    // barrier is not a matrix transition and must not hide the real release.
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(report_of(capture.frames[0]), Frame(down.begin(), down.end()));
    EXPECT_EQ(report_of(capture.frames[1]), Frame(empty.begin(), empty.end()));
}

TEST_F(OpenControllerProtocol, AFullKeyboardQueueKeepsTheFinalState) {
    std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> state = {0};
    Capture                                       capture;

    feed_status(0x33, 0); // nothing drains while the link is down
    ocp_service(0, capture_send, &capture);
    for (uint8_t k = 1; k <= OCP_KEYBOARD_QUEUE_CAPACITY + 3; ++k) {
        state[2] = k;
        ocp_set_keyboard_report(state.data());
    }
    EXPECT_EQ(ocp_keyboard_queue_count(), OCP_KEYBOARD_QUEUE_CAPACITY);
    EXPECT_EQ(ocp_get_diagnostics()->keyboard_queue_coalesced, 3U);

    // Drain it: the last report carries the final matrix state.
    feed_status(0x32, 1);
    ocp_service(1, capture_send, &capture);
    capture.frames.clear();
    for (uint32_t t = 2; t < 2 + OCP_KEYBOARD_QUEUE_CAPACITY; ++t) {
        ocp_service(t, capture_send, &capture);
        feed_ack(t);
    }
    ASSERT_EQ(capture.frames.size(), static_cast<size_t>(OCP_KEYBOARD_QUEUE_CAPACITY));
    EXPECT_EQ(report_of(capture.frames.back())[2], OCP_KEYBOARD_QUEUE_CAPACITY + 3);
}

TEST_F(OpenControllerProtocol, RejectedTransportDoesNotSendPendingKeyboardReports) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> report = {0, 0, 0x04, 0, 0, 0, 0, 0};

    ocp_set_keyboard_report(report.data());
    feed_status(0x36, 0);
    ocp_service(0, capture_send, &capture);
    ocp_service(1, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0x61, 0x0D, 0x0A}));
}

TEST_F(OpenControllerProtocol, SleepCommandsAreRefusedUntilTheReadyStatusIsSeen) {
    Capture capture;

    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_UNKNOWN);
    EXPECT_FALSE(ocp_queue_autosleep_arm());
    EXPECT_FALSE(ocp_queue_sleep_now());
    EXPECT_EQ(ocp_get_diagnostics()->control_queue_overflows, 0);

    ASSERT_TRUE(ocp_queue_sleep_negotiate());
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_PENDING);
    ocp_service(0, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA6, 0x56, 0xFC}));

    // A stock module ACKs the unlock like any other frame and says no more.
    feed_ack(1);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_PENDING);
    ocp_service(100, capture_send, &capture);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_PENDING);
    ocp_service(101, capture_send, &capture);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_UNSUPPORTED);
    EXPECT_FALSE(ocp_queue_autosleep_arm());
    EXPECT_FALSE(ocp_queue_sleep_now());
    EXPECT_EQ(capture.frames.size(), 1U);

    // A late 5B 37 is still proof.
    feed_status(0x37, 200);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_READY);
    ocp_service(200, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0x61, 0x0D, 0x0A}));
    EXPECT_TRUE(ocp_queue_autosleep_arm());
}

TEST_F(OpenControllerProtocol, AutoSleepStateFollowsTheModuleLifetimeRules) {
    Capture capture;

    negotiate_ready(capture, 0);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_OFF);

    ASSERT_TRUE(ocp_queue_autosleep_arm());
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_ARMING);
    EXPECT_FALSE(ocp_queue_autosleep_arm());
    ocp_service(1, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA6, 0x57, 0xFD}));
    feed_ack(2);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_ARMED);

    // Unpair clears it on the module.
    ASSERT_TRUE(ocp_queue_control(0x52));
    ocp_service(3, capture_send, &capture);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_ARMED);
    feed_ack(4);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_OFF);

    // Re-sending the unlock is the disable path and keeps the capability.
    arm_autosleep(capture, 5);
    ASSERT_TRUE(ocp_queue_sleep_negotiate());
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_READY);
    ocp_service(6, capture_send, &capture);
    feed_ack(7);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_OFF);
    ocp_service(200, capture_send, &capture);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_READY);
}

TEST_F(OpenControllerProtocol, WakePreambleLeadsTheFirstFrameAfterSilenceUnlessConnected) {
    Capture capture;

    negotiate_ready(capture, 0);
    arm_autosleep(capture, 1);

    // Inside the module's activity holdoff: no preamble.
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(50, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA6, 0x30, 0xD6}));
    feed_ack(51);
    EXPECT_EQ(ocp_get_diagnostics()->wake_preambles, 0);

    // After the holdoff the module may be asleep: NULL, gap, frame.
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(200, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0x00}));
    EXPECT_EQ(ocp_get_diagnostics()->wake_preambles, 1);
    EXPECT_FALSE(ocp_is_idle());
    ocp_service(204, capture_send, &capture);
    EXPECT_EQ(capture.frames.size(), 2U);
    ocp_service(205, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 3U);
    EXPECT_EQ(capture.frames[2], (Frame{0xA6, 0x30, 0xD6}));
    EXPECT_TRUE(ocp_actions_pending());
    feed_ack(206);

    // The rest of a burst rides on the holdoff the preamble started.
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(250, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 4U);
    EXPECT_EQ(capture.frames[3], (Frame{0xA6, 0x30, 0xD6}));
    feed_ack(251);

    // A connected module never auto-sleeps, however long the host is quiet.
    feed_status(0x32, 300);
    ocp_service(300, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 5U);
    EXPECT_EQ(capture.frames[4], (Frame{0x61, 0x0D, 0x0A}));
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(5000, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 6U);
    EXPECT_EQ(capture.frames[5], (Frame{0xA6, 0x30, 0xD6}));
    EXPECT_EQ(ocp_get_diagnostics()->wake_preambles, 1);
}

TEST_F(OpenControllerProtocol, ReplyAckNeverCarriesAPreambleWhileAutoSleeping) {
    Capture capture;

    negotiate_ready(capture, 0);
    arm_autosleep(capture, 1);

    // The module spoke, so it is awake: answer it plainly, and the answer
    // itself restarts the holdoff.
    feed_status(0x33, 500);
    ocp_service(500, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0x61, 0x0D, 0x0A}));
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(520, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x30, 0xD6}));
    EXPECT_EQ(ocp_get_diagnostics()->wake_preambles, 0);
}

TEST_F(OpenControllerProtocol, ExplicitSleepFollowsAReleaseBarrierHoldsTransmitThroughEntryThenWakesWithPreamble) {
    Capture capture;

    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture);
    negotiate_ready(capture, 0);

    ASSERT_TRUE(ocp_queue_sleep_now());
    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_SLEEP_REQUESTED);
    EXPECT_FALSE(ocp_queue_sleep_now());

    // The empty report goes first, and the sleep waits out its RF dwell: a
    // release still on its way to the receiver lands before the link is cut.
    ocp_service(1, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0xA1, 0, 0, 0, 0, 0, 0, 0, 0, 0xA1}));
    feed_ack(2);
    ocp_service(9, capture_send, &capture);
    EXPECT_EQ(capture.frames.size(), 1U);
    ocp_service(10, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x54, 0xFA}));
    capture.frames.clear();
    feed_ack(11);
    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_ASLEEP);
    EXPECT_EQ(ocp_get_link_state(), OCP_LINK_DISCONNECTED);
    EXPECT_FALSE(ocp_is_idle());

    // Nothing goes out while the module is on its way down, not even the ACK
    // for something it said on the way; the queued frame simply waits.
    feed_status(0x33, 12);
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(12, capture_send, &capture);
    ocp_service(60, capture_send, &capture);
    EXPECT_TRUE(capture.frames.empty());

    // Then the first frame is led by the wake preamble.
    ocp_service(61, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0x00}));
    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_AWAKE);
    ocp_service(65, capture_send, &capture);
    EXPECT_EQ(capture.frames.size(), 1U);
    ocp_service(66, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x30, 0xD6}));
    feed_ack(67);
    EXPECT_FALSE(ocp_actions_pending());
}

TEST_F(OpenControllerProtocol, ANewLinkDemotesTheCapabilityForRenegotiation) {
    Capture capture;

    negotiate_ready(capture, 0);
    arm_autosleep(capture, 1);

    // The module may have reset unseen between two links; the only cheap
    // moment to re-prove the boot-scoped unlock is when a link comes up.
    feed_status(0x32, 2);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_UNKNOWN);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_OFF);
    EXPECT_FALSE(ocp_queue_sleep_now());
    ocp_service(2, capture_send, &capture);
    capture.frames.clear();

    negotiate_ready(capture, 3);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_READY);
    EXPECT_TRUE(ocp_queue_autosleep_arm());

    // A stock module that replaced it never answers, and stays unsupported.
    feed_status(0x32, 4);
    ASSERT_TRUE(ocp_queue_sleep_negotiate());
    ocp_service(5, capture_send, &capture); // the reply ACK
    ocp_service(6, capture_send, &capture); // the arm queued earlier, inert now
    feed_ack(6);
    ocp_service(7, capture_send, &capture);
    EXPECT_EQ(capture.frames.back(), (Frame{0xA6, 0x56, 0xFC}));
    feed_ack(8);
    ocp_service(200, capture_send, &capture);
    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_UNSUPPORTED);
}

TEST_F(OpenControllerProtocol, ReplyAckIsDroppedWhileTheModuleIsExplicitlyAsleep) {
    Capture capture;

    negotiate_ready(capture, 0);
    ASSERT_TRUE(ocp_queue_sleep_now());
    ocp_service(1, capture_send, &capture); // the release barrier
    feed_ack(2);
    ocp_service(10, capture_send, &capture); // the sleep, after the dwell
    feed_ack(11);
    ASSERT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_ASLEEP);
    capture.frames.clear();

    // Long after entry: an unsolicited frame is answered with nothing, and the
    // module stays asleep as far as the host is concerned.
    feed_status(0x21, 1000);
    ocp_service(1000, capture_send, &capture);
    ocp_service(1001, capture_send, &capture);
    EXPECT_TRUE(capture.frames.empty());
    EXPECT_EQ(ocp_get_power_state(), OCP_POWER_LOW);
    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_ASLEEP);
    EXPECT_TRUE(ocp_is_idle());
}

TEST_F(OpenControllerProtocol, ExplicitSleepIsInertWithoutTheCapability) {
    Capture capture;

    // Queued as a raw control by a caller that skipped negotiation.
    ASSERT_TRUE(ocp_queue_control(0x54));
    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture);
    ocp_service(1, capture_send, &capture);
    feed_ack(2);

    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_AWAKE);
    EXPECT_EQ(ocp_get_link_state(), OCP_LINK_CONNECTED);
    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(3, capture_send, &capture);
    EXPECT_EQ(capture.frames.back(), (Frame{0xA6, 0x30, 0xD6}));
}

TEST_F(OpenControllerProtocol, TransactionAbortForgetsTheSleepCapability) {
    Capture capture;

    negotiate_ready(capture, 0);
    arm_autosleep(capture, 1);

    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(10, capture_send, &capture);
    ocp_service(30, capture_send, &capture);
    ocp_service(50, capture_send, &capture);
    ocp_service(70, capture_send, &capture);
    ASSERT_TRUE(ocp_take_tx_failure());

    EXPECT_EQ(ocp_get_sleep_capability(), OCP_SLEEP_CAP_UNKNOWN);
    EXPECT_EQ(ocp_get_autosleep_state(), OCP_AUTOSLEEP_OFF);
    EXPECT_EQ(ocp_get_module_sleep_state(), OCP_MODULE_AWAKE);

    // The renegotiation that follows goes out plainly.
    ASSERT_TRUE(ocp_queue_sleep_negotiate());
    ocp_service(1000, capture_send, &capture);
    EXPECT_EQ(capture.frames.back(), (Frame{0xA6, 0x56, 0xFC}));
}

TEST_F(OpenControllerProtocol, WakeGapIsTimerWrapSafe) {
    Capture capture;

    negotiate_ready(capture, UINT32_MAX - 300);
    arm_autosleep(capture, UINT32_MAX - 299);

    ASSERT_TRUE(ocp_queue_control(0x30));
    ocp_service(UINT32_MAX - 2, capture_send, &capture);
    ASSERT_EQ(capture.frames.size(), 1U);
    EXPECT_EQ(capture.frames[0], (Frame{0x00}));

    ocp_service(1, capture_send, &capture); // Four milliseconds after the preamble.
    EXPECT_EQ(capture.frames.size(), 1U);
    ocp_service(2, capture_send, &capture); // Five.
    ASSERT_EQ(capture.frames.size(), 2U);
    EXPECT_EQ(capture.frames[1], (Frame{0xA6, 0x30, 0xD6}));
}

} // namespace
