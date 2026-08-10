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

TEST_F(OpenControllerProtocol, ReconnectResyncHoldsLiveReportUntilSnapshotSequenceCompletes) {
    Capture                                             capture;
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> snapshot = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
    const std::array<uint8_t, OCP_KEYBOARD_REPORT_SIZE> live     = {0x02, 0, 0x05, 0, 0, 0, 0, 0};

    feed_status(0x32, 0);
    ocp_service(0, capture_send, &capture); // ACK the status frame.
    ocp_begin_keyboard_resync(snapshot.data());
    ocp_set_keyboard_report(live.data());

    ocp_service(1, capture_send, &capture);
    feed_ack(2);
    ocp_service(2, capture_send, &capture);
    feed_ack(3);
    ocp_service(3, capture_send, &capture);
    feed_ack(4);
    ocp_service(4, capture_send, &capture);

    ASSERT_EQ(capture.frames.size(), 5U);
    EXPECT_EQ(capture.frames[0], (Frame{0x61, 0x0D, 0x0A}));
    EXPECT_EQ(Frame(capture.frames[1].begin() + 1, capture.frames[1].begin() + 9), Frame(snapshot.begin(), snapshot.end()));
    EXPECT_EQ(Frame(capture.frames[2].begin() + 1, capture.frames[2].begin() + 9), Frame(OCP_KEYBOARD_REPORT_SIZE, 0));
    EXPECT_EQ(Frame(capture.frames[3].begin() + 1, capture.frames[3].begin() + 9), Frame(snapshot.begin(), snapshot.end()));
    EXPECT_EQ(Frame(capture.frames[4].begin() + 1, capture.frames[4].begin() + 9), Frame(live.begin(), live.end()));
    EXPECT_FALSE(ocp_resync_is_active());

    feed_ack(5);
    ocp_service(25, capture_send, &capture);
    EXPECT_TRUE(ocp_is_idle());
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

} // namespace
