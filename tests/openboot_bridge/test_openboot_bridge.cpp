// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <deque>
#include <numeric>
#include <vector>

#include "gtest/gtest.h"

#include "openboot_bridge.h"

namespace {

using Bytes = std::vector<uint8_t>;

struct Capture {
    std::vector<Bytes> uart_writes;
    std::vector<Bytes> host_reports;
    bool               uart_accept = true;
    bool               host_accept = true;
};

bool capture_uart(const uint8_t *data, uint8_t length, void *context) {
    auto *capture = static_cast<Capture *>(context);
    if (!capture->uart_accept) {
        return false;
    }
    capture->uart_writes.emplace_back(data, data + length);
    return true;
}

bool capture_host(const uint8_t *data, uint8_t length, void *context) {
    auto *capture = static_cast<Capture *>(context);
    if (!capture->host_accept) {
        return false;
    }
    capture->host_reports.emplace_back(data, data + length);
    return true;
}

Bytes make_report(uint8_t tag, const Bytes &payload) {
    Bytes report(OBB_REPORT_SIZE, 0);
    report[0] = tag;
    report[1] = static_cast<uint8_t>(payload.size());
    for (size_t i = 0; i < payload.size() && i + 2 < report.size(); i++) {
        report[i + 2] = payload[i];
    }
    return report;
}

/* Deliberately builds the length byte from the caller so oversized values can
 * be exercised without the helper clamping them first. */
Bytes make_raw_report(uint8_t tag, uint8_t length, const Bytes &payload) {
    Bytes report = make_report(tag, payload);
    report[1]    = length;
    return report;
}

Bytes make_enter(uint32_t magic, uint16_t settle_ms, uint8_t flags) {
    return make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_ENTER, static_cast<uint8_t>(magic & 0xFF), static_cast<uint8_t>((magic >> 8) & 0xFF), static_cast<uint8_t>((magic >> 16) & 0xFF), static_cast<uint8_t>((magic >> 24) & 0xFF), static_cast<uint8_t>(settle_ms & 0xFF), static_cast<uint8_t>(settle_ms >> 8), flags});
}

uint32_t le32_at(const Bytes &report, size_t index) {
    return static_cast<uint32_t>(report[index]) | (static_cast<uint32_t>(report[index + 1]) << 8) | (static_cast<uint32_t>(report[index + 2]) << 16) | (static_cast<uint32_t>(report[index + 3]) << 24);
}

class OpenbootBridge : public testing::Test {
   protected:
    Capture           capture;
    std::deque<uint8_t> module_bytes;

    void SetUp() override {
        obb_init();
        obb_set_ocp_state(true, 0);
    }

    /* Mirrors the glue loop in opencontroller.c: pull at most one host report,
     * drain the UART only while the bridge will take it, then service. */
    void pump(uint32_t now_ms) {
        if (obb_owns_uart()) {
            while (obb_can_accept_uart_bytes() && !module_bytes.empty()) {
                Bytes chunk;
                while (chunk.size() < OBB_MAX_DATA && !module_bytes.empty()) {
                    chunk.push_back(module_bytes.front());
                    module_bytes.pop_front();
                }
                obb_on_uart_bytes(chunk.data(), static_cast<uint8_t>(chunk.size()), now_ms);
            }
        }
        obb_service(now_ms, capture_uart, capture_host, &capture);
    }

    void send(const Bytes &report, uint32_t now_ms) {
        ASSERT_TRUE(obb_can_accept_host_report());
        obb_on_host_report(report.data(), static_cast<uint8_t>(report.size()), now_ms);
    }

    /* Drives ENTER through to passthrough, acknowledging the OTA command at t0. */
    void reach_passthru(uint32_t t0, uint16_t settle_ms = 100) {
        send(make_enter(OBB_ENTER_MAGIC, settle_ms, 0), t0);
        pump(t0);
        EXPECT_TRUE(obb_take_ota_request());
        obb_on_ota_acked(t0);
        EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
        pump(t0 + settle_ms);
        ASSERT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
        capture.uart_writes.clear();
        capture.host_reports.clear();
    }

    const Bytes &last_host_report() const {
        return capture.host_reports.back();
    }
};

TEST_F(OpenbootBridge, StrayDataReportInIdleIsDropped) {
    send(make_report(OBB_TAG_DATA, Bytes{0xB0, 0x07, 0x01}), 0);
    pump(0);

    EXPECT_TRUE(capture.uart_writes.empty());
    EXPECT_TRUE(capture.host_reports.empty());
    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
}

TEST_F(OpenbootBridge, EnterWithWrongMagicIsRejectedAndDoesNotDisturbTheLink) {
    send(make_enter(OBB_ENTER_MAGIC ^ 0xFFu, 0, 0), 0);
    pump(0);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[0], OBB_TAG_CONTROL);
    EXPECT_EQ(last_host_report()[2], OBB_OP_ENTER);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_MAGIC);
    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
    EXPECT_FALSE(obb_take_ota_request());
    EXPECT_FALSE(obb_owns_uart());
}

TEST_F(OpenbootBridge, EnterRequestsTheOtaCommandOnceAndDefersItsResponse) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    EXPECT_EQ(obb_get_state(), OBB_STATE_QUIESCE);

    pump(0);

    EXPECT_EQ(obb_get_state(), OBB_STATE_ENTER_SENT);
    EXPECT_TRUE(obb_take_ota_request());
    EXPECT_FALSE(obb_take_ota_request());
    EXPECT_TRUE(capture.host_reports.empty()) << "the ENTER response must wait for the acknowledgement";
    EXPECT_FALSE(obb_owns_uart());
}

TEST_F(OpenbootBridge, QuiesceWaitsForTheLinkToGoIdleUpToItsCap) {
    obb_set_ocp_state(false, 0);
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);

    pump(OBB_QUIESCE_TIMEOUT_MS - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_QUIESCE);
    EXPECT_FALSE(obb_take_ota_request());

    pump(OBB_QUIESCE_TIMEOUT_MS);
    EXPECT_EQ(obb_get_state(), OBB_STATE_ENTER_SENT);
    EXPECT_TRUE(obb_take_ota_request());
}

TEST_F(OpenbootBridge, AcknowledgementAnswersEnterAndSeizesTheUart) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    pump(0);
    EXPECT_TRUE(obb_take_ota_request());

    obb_on_ota_acked(50);

    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
    EXPECT_TRUE(obb_owns_uart());
    EXPECT_TRUE(obb_take_seize_request());
    EXPECT_FALSE(obb_take_seize_request());

    pump(50);
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], OBB_OP_ENTER);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_OK);
}

TEST_F(OpenbootBridge, MissingAcknowledgementFailsEnterAndLeavesTheLinkAlone) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    pump(0);
    EXPECT_TRUE(obb_take_ota_request());

    obb_on_ota_failed(60);
    pump(60);

    EXPECT_EQ(obb_get_state(), OBB_STATE_ERROR);
    EXPECT_FALSE(obb_owns_uart());
    EXPECT_FALSE(obb_take_seize_request());
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], OBB_OP_ENTER);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_NO_ACK);
}

TEST_F(OpenbootBridge, EnterSentTimesOutWhenNeitherEdgeArrives) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    pump(0);
    EXPECT_TRUE(obb_take_ota_request());

    pump(OBB_OTA_TIMEOUT_MS - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_ENTER_SENT);

    pump(OBB_OTA_TIMEOUT_MS);
    EXPECT_EQ(obb_get_state(), OBB_STATE_ERROR);
}

TEST_F(OpenbootBridge, ForceEntersSettlingWithoutAnAcknowledgement) {
    send(make_enter(OBB_ENTER_MAGIC, 100, OBB_ENTER_FLAG_FORCE), 0);
    pump(0);

    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
    EXPECT_TRUE(obb_owns_uart());
    EXPECT_TRUE(obb_take_ota_request()) << "force still offers the OTA command; it is harmless noise";
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_OK);
}

TEST_F(OpenbootBridge, EnterIsAcceptedAgainAfterAFailure) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    pump(0);
    EXPECT_TRUE(obb_take_ota_request());
    obb_on_ota_failed(60);
    ASSERT_EQ(obb_get_state(), OBB_STATE_ERROR);

    send(make_enter(OBB_ENTER_MAGIC, 100, OBB_ENTER_FLAG_FORCE), 100);
    pump(100);

    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
}

TEST_F(OpenbootBridge, SettlingSwallowsThePostResetNoise) {
    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 0);
    pump(0);
    EXPECT_TRUE(obb_take_ota_request());
    obb_on_ota_acked(0);
    pump(0);
    capture.host_reports.clear();

    module_bytes = {0xFF, 0x00, 0xB0, 0xAA};
    pump(10);

    EXPECT_TRUE(module_bytes.empty()) << "settling must keep draining or the driver queue overflows";
    EXPECT_TRUE(capture.host_reports.empty());
}

TEST_F(OpenbootBridge, SettleValueIsDefaultedAndClamped) {
    send(make_enter(OBB_ENTER_MAGIC, 0, 0), 0);
    pump(0);
    obb_on_ota_acked(0);
    pump(OBB_DEFAULT_SETTLE_MS - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
    pump(OBB_DEFAULT_SETTLE_MS);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);

    obb_init();
    obb_set_ocp_state(true, 0);
    send(make_enter(OBB_ENTER_MAGIC, 20000, 0), 0);
    pump(0);
    obb_on_ota_acked(0);
    pump(OBB_MAX_SETTLE_MS - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);
    pump(OBB_MAX_SETTLE_MS);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, PassthroughBeginsExactlyAtTheSettleDeadline) {
    send(make_enter(OBB_ENTER_MAGIC, 1500, 0), 0);
    pump(0);
    obb_on_ota_acked(40);

    pump(40 + 1500 - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_SETTLING);

    pump(40 + 1500);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, AFullSizedDataReportReachesTheUartInOneBurst) {
    reach_passthru(0);

    Bytes payload(OBB_MAX_DATA);
    std::iota(payload.begin(), payload.end(), 1);
    send(make_report(OBB_TAG_DATA, payload), 200);
    pump(200);

    ASSERT_EQ(capture.uart_writes.size(), 1u);
    EXPECT_EQ(capture.uart_writes[0], payload);
}

TEST_F(OpenbootBridge, AnOversizedDataReportIsRejectedWithoutTouchingTheUart) {
    reach_passthru(0);

    obb_on_host_report(make_raw_report(OBB_TAG_DATA, OBB_MAX_DATA + 1, Bytes{0x01}).data(), OBB_REPORT_SIZE, 200);
    pump(200);

    EXPECT_TRUE(capture.uart_writes.empty());
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], OBB_OP_DATA_ERROR);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_ARG);
}

TEST_F(OpenbootBridge, ARefusedUartWriteIsRetriedWithTheSameBytesAndBlocksTheNextReport) {
    reach_passthru(0);

    Bytes payload{0xB0, 0x07, 0x01, 0x02};
    capture.uart_accept = false;
    send(make_report(OBB_TAG_DATA, payload), 200);
    pump(200);

    EXPECT_TRUE(capture.uart_writes.empty());
    EXPECT_FALSE(obb_can_accept_host_report()) << "backpressure must stop the glue pulling another report";

    capture.uart_accept = true;
    pump(210);

    ASSERT_EQ(capture.uart_writes.size(), 1u);
    EXPECT_EQ(capture.uart_writes[0], payload);
    EXPECT_TRUE(obb_can_accept_host_report());

    pump(220);
    EXPECT_EQ(capture.uart_writes.size(), 1u) << "a delivered burst must not be sent twice";
}

TEST_F(OpenbootBridge, AStuckUartWriteIsDroppedRatherThanWedgingTheBridge) {
    reach_passthru(0);

    capture.uart_accept = false;
    send(make_report(OBB_TAG_DATA, Bytes{0xB0, 0x07}), 200);
    pump(200);
    pump(200 + OBB_TX_STAGE_TIMEOUT_MS - 1);
    EXPECT_FALSE(obb_can_accept_host_report());

    pump(200 + OBB_TX_STAGE_TIMEOUT_MS);
    EXPECT_TRUE(obb_can_accept_host_report());
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, ModuleBytesAreChunkedAcrossReportsAndReassembleInOrder) {
    reach_passthru(0);

    Bytes stream(130);
    std::iota(stream.begin(), stream.end(), 0);
    module_bytes.assign(stream.begin(), stream.end());

    for (uint32_t tick = 0; tick < 5; tick++) {
        pump(200 + tick);
    }

    ASSERT_EQ(capture.host_reports.size(), 3u);
    EXPECT_EQ(capture.host_reports[0][1], OBB_MAX_DATA);
    EXPECT_EQ(capture.host_reports[1][1], OBB_MAX_DATA);
    EXPECT_EQ(capture.host_reports[2][1], 130 - 2 * OBB_MAX_DATA);

    Bytes joined;
    for (const auto &report : capture.host_reports) {
        EXPECT_EQ(report[0], OBB_TAG_DATA);
        joined.insert(joined.end(), report.begin() + 2, report.begin() + 2 + report[1]);
    }
    EXPECT_EQ(joined, stream);
}

TEST_F(OpenbootBridge, ARefusedHostReportIsReofferedUnchanged) {
    reach_passthru(0);

    module_bytes = {0xB0, 0x07, 0x81};
    capture.host_accept = false;
    pump(200);
    EXPECT_TRUE(capture.host_reports.empty());

    capture.host_accept = true;
    pump(210);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[1], 3);
    EXPECT_EQ(last_host_report()[2], 0xB0);
    EXPECT_EQ(last_host_report()[4], 0x81);

    pump(220);
    EXPECT_EQ(capture.host_reports.size(), 1u);
}

TEST_F(OpenbootBridge, StatusReportsElapsedTimeAnchoredOnTheAcknowledgement) {
    send(make_enter(OBB_ENTER_MAGIC, 1000, 0), 0);
    pump(0);
    obb_on_ota_acked(500);
    pump(500);
    capture.host_reports.clear();

    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_STATUS}), 800);
    pump(800);
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[4], OBB_STATE_SETTLING);
    EXPECT_EQ(le32_at(last_host_report(), 5), 300u);

    pump(1500);
    ASSERT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_STATUS}), 2000);
    pump(2000);
    ASSERT_EQ(capture.host_reports.size(), 2u);
    EXPECT_EQ(last_host_report()[4], OBB_STATE_PASSTHRU);
    EXPECT_EQ(le32_at(last_host_report(), 5), 1500u);
}

TEST_F(OpenbootBridge, StatusSurvivesADeferredEnterResponse) {
    send(make_enter(OBB_ENTER_MAGIC, 1000, 0), 0);
    pump(0);
    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_STATUS}), 10);
    obb_on_ota_acked(20);

    pump(20);
    pump(21);

    ASSERT_EQ(capture.host_reports.size(), 2u);
    EXPECT_EQ(capture.host_reports[0][2], OBB_OP_STATUS);
    EXPECT_EQ(capture.host_reports[1][2], OBB_OP_ENTER);
    EXPECT_EQ(capture.host_reports[1][3], OBB_STATUS_OK);
}

TEST_F(OpenbootBridge, TheWatchdogReleasesTheUartExactlyOnce) {
    reach_passthru(0);
    const uint32_t start = 100;
    pump(start);

    pump(start + OBB_WATCHDOG_MS - 1);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
    EXPECT_FALSE(obb_take_release_request());

    pump(start + OBB_WATCHDOG_MS);
    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
    EXPECT_TRUE(obb_take_release_request());
    EXPECT_FALSE(obb_take_release_request());
}

TEST_F(OpenbootBridge, TrafficInEitherDirectionResetsTheWatchdog) {
    reach_passthru(0);

    pump(OBB_WATCHDOG_MS - 1);
    send(make_report(OBB_TAG_DATA, Bytes{0x01}), OBB_WATCHDOG_MS - 1);
    pump(OBB_WATCHDOG_MS - 1);

    pump(2 * OBB_WATCHDOG_MS - 3);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);

    module_bytes = {0x42};
    pump(2 * OBB_WATCHDOG_MS - 3);
    pump(3 * OBB_WATCHDOG_MS - 6);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, LosingUsbReleasesTheUart) {
    reach_passthru(0);

    obb_on_usb_inactive(300);

    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
    EXPECT_TRUE(obb_take_release_request());
}

TEST_F(OpenbootBridge, ExitAnswersThenReleasesTheUart) {
    reach_passthru(0);

    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_EXIT}), 300);
    pump(300);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], OBB_OP_EXIT);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_OK);
    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
    EXPECT_FALSE(obb_owns_uart());
    EXPECT_TRUE(obb_take_release_request());
}

TEST_F(OpenbootBridge, EnterDuringPassthroughIsRejectedWithoutDisturbingIt) {
    reach_passthru(0);

    send(make_enter(OBB_ENTER_MAGIC, 100, 0), 300);
    pump(300);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_STATE);
    EXPECT_EQ(obb_get_state(), OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, IdentifyAnswersInEveryStateAndAdvertisesTheReportCapacity) {
    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_IDENTIFY}), 0);
    pump(0);
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], OBB_OP_IDENTIFY);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_OK);
    EXPECT_EQ(last_host_report()[6], OBB_MAX_DATA);
    EXPECT_EQ(last_host_report()[9], OBB_STATE_IDLE);
    EXPECT_EQ(last_host_report()[10], OBB_CAP_FORCE);

    reach_passthru(100);
    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_IDENTIFY}), 300);
    pump(300);
    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[9], OBB_STATE_PASSTHRU);
}

TEST_F(OpenbootBridge, UnknownControlOpcodesAreRefusedRatherThanIgnored) {
    send(make_report(OBB_TAG_CONTROL, Bytes{0x55}), 0);
    pump(0);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[2], 0x55);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_ARG);
}

TEST_F(OpenbootBridge, UnknownTagsAreDroppedSilently) {
    send(make_report(0x7E, Bytes{0x01, 0x02}), 0);
    pump(0);

    EXPECT_TRUE(capture.host_reports.empty());
    EXPECT_TRUE(capture.uart_writes.empty());
}

TEST_F(OpenbootBridge, ShortEnterRequestsAreRefused) {
    send(make_report(OBB_TAG_CONTROL, Bytes{OBB_OP_ENTER, 0x31, 0x42, 0x42}), 0);
    pump(0);

    ASSERT_EQ(capture.host_reports.size(), 1u);
    EXPECT_EQ(last_host_report()[3], OBB_STATUS_E_ARG);
    EXPECT_EQ(obb_get_state(), OBB_STATE_IDLE);
}

} // namespace
