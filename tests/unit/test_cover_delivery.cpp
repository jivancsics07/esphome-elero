#include "EleroCover.h"
#include "EleroGroupCover.h"
#include "elero_web/elero_web_utils.h"
#include <gtest/gtest.h>

using namespace esphome;
using namespace esphome::elero;
using namespace esphome::cover;

class TestCover : public EleroCover {
 public:
  using EleroCover::control;
  bool verifying() const { return stop_verification_active_; }
  const char *result() const { return stop_result_; }
  void fail_stop(uint32_t now) { fail_stop_verification_(now); }
  RxCutoff cutoff() const { return stop_rx_cutoff_; }
};
class TestGroup : public EleroGroupCover {
 public:
  using EleroGroupCover::control;
};

class CoverDeliveryTest : public ::testing::Test {
 protected:
  Elero hub;
  TestCover cover;
  RxTimeline timeline;
  void SetUp() override {
    test_now = 1000;
    cover.set_elero_parent(&hub);
    cover.set_blind_address(0x111111);
    cover.set_remote_address(0x123456);
    cover.set_poll_interval(300000);
    cover.setup();
  }
  void stop() {
    CoverCall call; call.stop = true;
    cover.control(call);
    ASSERT_TRUE(cover.verifying());
    transmit(1010);
    transmit(1020);
  }
  void transmit(uint32_t now) {
    const auto id = hub.advance(now);
    ASSERT_NE(id, 0u);
    hub.complete(id, true, now + 1, timeline.fence(now + 1));
  }
  RxMetadata fresh(uint32_t motor = 0x111111) {
    test_now += 10;
    auto meta = timeline.capture(test_now);
    meta.source = motor;
    return meta;
  }
};

TEST_F(CoverDeliveryTest, BufferedStoppedBeforeLocalTxCannotConfirmStop) {
  auto old = timeline.capture(900); old.source = 0x111111;
  stop();
  cover.set_rx_status(ELERO_STATE_STOPPED, old);
  EXPECT_TRUE(cover.verifying());
  EXPECT_STREQ(cover.result(), "stop_verifying");
}

TEST_F(CoverDeliveryTest, PreFencePrefixParsedLaterIsStillOld) {
  auto prefix = timeline.capture(900);
  stop();
  auto packet = fresh();
  packet.epoch = prefix.epoch;
  cover.set_rx_status(ELERO_STATE_STOPPED, packet);
  EXPECT_TRUE(cover.verifying());
}

TEST_F(CoverDeliveryTest, OnlyFreshCorrectMotorAndExplicitStillnessConfirmStop) {
  for (uint8_t state : {0x01, 0x02, 0x03, 0x04, 0x0d, 0x0e, 0x0f}) {
    test_now += 100;
    cover.request_stop();
    transmit(test_now + 1); transmit(test_now + 1);
    cover.set_rx_status(state, fresh(0x222222));
    EXPECT_TRUE(cover.verifying());
    cover.set_rx_status(state, fresh());
    EXPECT_FALSE(cover.verifying());
    EXPECT_STREQ(cover.result(), "stop_confirmed");
  }
}

TEST_F(CoverDeliveryTest, UnknownTimeoutAndUnknownRawValuesNeverConfirmStop) {
  stop();
  for (uint8_t state : {0x00, 0x07, 0xff}) {
    cover.set_rx_status(state, fresh());
    EXPECT_TRUE(cover.verifying());
    EXPECT_STREQ(cover.result(), "stop_verifying");
  }
}

TEST_F(CoverDeliveryTest, BlockingAndOverheatedAreTerminalMotorFailuresNotSuccess) {
  stop();
  cover.set_rx_status(ELERO_STATE_BLOCKING, fresh());
  EXPECT_FALSE(cover.verifying());
  EXPECT_STREQ(cover.result(), "stop_motor_blocking");
  cover.request_stop(); transmit(test_now + 1); transmit(test_now + 1);
  cover.set_rx_status(ELERO_STATE_OVERHEATED, fresh());
  EXPECT_FALSE(cover.verifying());
  EXPECT_STREQ(cover.result(), "stop_motor_overheated");
}

TEST_F(CoverDeliveryTest, ManualStopBypassesFailureCooldownButNormalCommandsDoNot) {
  stop();
  cover.fail_stop(test_now);
  const auto failed_at = test_now;
  cover.submit_intent({CommandIntentKind::CHECK, 0});
  EXPECT_EQ(hub.advance(failed_at + 1), 0u);
  cover.request_stop();
  transmit(failed_at + 2); transmit(failed_at + 4);
  cover.set_rx_status(ELERO_STATE_STOPPED, fresh());
  cover.submit_intent({CommandIntentKind::OPEN, 0});
  EXPECT_EQ(hub.advance(failed_at + 20), 0u);
  EXPECT_NE(hub.advance(failed_at + 3000), 0u);
}

TEST_F(CoverDeliveryTest, CoverWebButtonAndAutomationShareStopEntry) {
  // The web parser and the custom-button mapper feed the real virtual intent
  // entry, exactly as their production adapters do. CoverCall covers ESPHome
  // API/automation; automatic position control uses request_stop() itself.
  for (int route = 0; route < 4; route++) {
    if (route == 0) { CoverCall call; call.stop = true; cover.control(call); }
    if (route == 1) {
      CommandIntent intent;
      ASSERT_TRUE(web_utils::parse_cover_intent("stop", intent));
      static_cast<EleroBlindBase *>(&cover)->submit_control_intent(intent);
    }
    if (route == 2) {
      auto mapping = cover.get_command_delivery_config().mapping;
      cover.submit_intent(cover_intent_for_command_byte(mapping, mapping.stop));
    }
    if (route == 3) cover.request_stop();
    ASSERT_TRUE(cover.verifying());
    transmit(test_now + 1); transmit(test_now + 1);
    cover.set_rx_status(ELERO_STATE_STOPPED, fresh());
    ASSERT_STREQ(cover.result(), "stop_confirmed");
  }
}

TEST_F(CoverDeliveryTest, FreshMovementUsesOnlyOneExtraBurstThenFailsExplicitly) {
  stop();
  cover.set_rx_status(ELERO_STATE_MOVING_UP, fresh());
  transmit(test_now + 1); transmit(test_now + 1);
  cover.set_rx_status(ELERO_STATE_MOVING_UP, fresh());
  EXPECT_FALSE(cover.verifying()); EXPECT_STREQ(cover.result(), "stop_failed");
  ASSERT_EQ(hub.packets.size(), 4u);
}

TEST_F(CoverDeliveryTest, StopVerificationDeadlineSurvivesMillisWrap) {
  test_now = UINT32_MAX - 1000;
  cover.request_stop(); transmit(test_now + 1); transmit(test_now + 1);
  const auto first = cover.cutoff().completed_at_ms;
  test_now = first + 100; cover.loop(); EXPECT_EQ(hub.advance(test_now), 0u);
  test_now = first + 1999; cover.loop(); EXPECT_EQ(hub.advance(test_now), 0u);
  test_now = first + 2000; cover.loop(); EXPECT_NE(hub.advance(test_now), 0u);
  EXPECT_TRUE(cover.verifying());
}

TEST_F(CoverDeliveryTest, WebCommandsUseTheHomeAssistantMovementStatePath) {
  transmit(1001);  // setup's initial CHECK

  CommandIntent close;
  ASSERT_TRUE(web_utils::parse_cover_intent("close", close));
  EXPECT_TRUE(intent_was_accepted(cover.submit_control_intent(close)));
  EXPECT_STREQ(cover.get_operation_str(), "closing");
  transmit(1010);

  CommandIntent stop;
  ASSERT_TRUE(web_utils::parse_cover_intent("stop", stop));
  EXPECT_TRUE(intent_was_accepted(cover.submit_control_intent(stop)));
  EXPECT_TRUE(cover.verifying());
}

TEST_F(CoverDeliveryTest, OrdinaryOpenWithoutMotorResponseRemainsDeliveryUnconfirmed) {
  transmit(1001);  // setup's initial CHECK, without a motor response
  CoverCall open; open.position = COVER_OPEN; cover.control(open);
  const auto id = hub.advance(1010);
  ASSERT_NE(id, 0u);
  const auto outcome = hub.complete(id, true, 1011, timeline.fence(1011));
  EXPECT_EQ(outcome.motor_evidence, MotorDeliveryEvidence::LOCAL_TX_UNCONFIRMED);
  EXPECT_EQ(hub.status, "delivery_unconfirmed");
  test_now = 1500; cover.loop();
  EXPECT_EQ(hub.status, "delivery_unconfirmed");
  EXPECT_FALSE(cover.verifying());
}

TEST_F(CoverDeliveryTest, NoFeedbackHasBoundedExtraBurstAndVisibleFailure) {
  stop();
  const auto first = cover.cutoff().completed_at_ms;
  test_now = first + 2000; cover.loop(); transmit(test_now + 1); // CHECK
  test_now = first + 4000; cover.loop(); transmit(test_now + 1); transmit(test_now + 1);
  const auto retry = cover.cutoff().completed_at_ms;
  test_now = retry + 2000; cover.loop(); transmit(test_now + 1);
  test_now = retry + 4000; cover.loop();
  EXPECT_FALSE(cover.verifying());
  EXPECT_STREQ(cover.result(), "stop_failed");
  ASSERT_EQ(hub.packets.size(), 6u);
  EXPECT_EQ(hub.packets[0].counter, hub.packets[1].counter);
  EXPECT_EQ(hub.packets[3].counter, hub.packets[4].counter);
}

TEST_F(CoverDeliveryTest, GroupStopVerifiesAllMembersAndBlocksNativeMovement) {
  TestCover other;
  other.set_elero_parent(&hub); other.set_blind_address(0x222222);
  other.set_remote_address(0x123456); other.set_poll_interval(300000); other.setup();
  TestGroup group;
  group.set_elero_parent(&hub); group.add_member(&cover); group.add_member(&other); group.setup();
  CoverCall stop; stop.stop = true; group.control(stop);
  ASSERT_TRUE(cover.verifying()); ASSERT_TRUE(other.verifying());
  transmit(1010); transmit(1020);
  CoverCall open; open.position = COVER_OPEN; group.control(open);
  // Initial member CHECKs may run; the native OPEN must not.
  for (int i = 0; i < 3; i++) {
    auto id = hub.advance(1100 + i);
    if (id) {
      EXPECT_EQ(hub.packets.back().payload[4], 0);
      hub.complete(id, true, 1100 + i, timeline.fence(1100 + i));
    }
  }
  cover.set_rx_status(ELERO_STATE_STOPPED, fresh());
  EXPECT_EQ(hub.advance(1200), 0u);
  other.set_rx_status(ELERO_STATE_STOPPED, fresh(0x222222));
  ASSERT_NE(hub.advance(1300), 0u);
  EXPECT_EQ(hub.packets.back().payload[4], 0x20);
}

TEST_F(CoverDeliveryTest, GroupPartialStopThenTerminalFailureCannotLeaveVerificationStuck) {
  TestCover other;
  other.set_elero_parent(&hub); other.set_blind_address(0x222222);
  other.set_remote_address(0x123456); other.set_poll_interval(300000); other.setup();
  TestGroup group;
  group.set_elero_parent(&hub); group.add_member(&cover); group.add_member(&other); group.setup();
  CoverCall stop; stop.stop = true; group.control(stop);
  transmit(1010);  // first native packet really completed
  for (uint32_t now = 1100; now <= 1400; now += 100) {
    auto id = hub.advance(now);
    ASSERT_NE(id, 0u);
    hub.complete(id, false, now + 1);
  }
  EXPECT_FALSE(cover.verifying()); EXPECT_FALSE(other.verifying());
  EXPECT_STREQ(cover.result(), "stop_failed"); EXPECT_STREQ(other.result(), "stop_failed");
}

TEST_F(CoverDeliveryTest, TiltCloseSendsConfiguredByteAndDedupesRepeatedCalls) {
  // Setters that feed get_command_delivery_config() must run before setup(),
  // which snapshots the mapping into the (by-then-attached) delivery lane.
  TestCover tilt_cover;
  tilt_cover.set_elero_parent(&hub);
  tilt_cover.set_blind_address(0x111111);
  tilt_cover.set_remote_address(0x123456);
  tilt_cover.set_poll_interval(300000);
  tilt_cover.set_supports_tilt(true);
  tilt_cover.set_command_tilt(0x24);
  tilt_cover.set_command_tilt_close(0x25);
  tilt_cover.setup();

  // Both `cover` (the fixture default) and `tilt_cover` queue an initial
  // status CHECK in setup(); drain those before exercising tilt commands.
  for (uint32_t now = 1001; now <= 1004; now++) {
    auto id = hub.advance(now);
    if (id == 0) break;
    EXPECT_EQ(hub.packets.back().payload[4], 0);
    hub.complete(id, true, now + 1, timeline.fence(now + 1));
  }

  CoverCall tilt_open;
  tilt_open.tilt = 1.0f;
  tilt_cover.control(tilt_open);
  transmit(1010);
  EXPECT_EQ(hub.packets.back().payload[4], 0x24);
  EXPECT_EQ(hub.packets.back().dest_addrs[0], 0x111111u);
  EXPECT_FLOAT_EQ(tilt_cover.tilt, 1.0f);

  CoverCall tilt_close;
  tilt_close.tilt = 0.0f;
  tilt_cover.control(tilt_close);
  // A second identical tilt=0 call before the first transmits must dedupe
  // instead of queueing a duplicate RF command (regression guard for the
  // CUSTOM-bypasses-coalescing issue).
  tilt_cover.control(tilt_close);
  EXPECT_EQ(tilt_cover.get_command_delivery()->size(), 1u);
  transmit(1020);
  EXPECT_EQ(hub.packets.back().payload[4], 0x25);
  EXPECT_FLOAT_EQ(tilt_cover.tilt, 0.0f);
}

TEST_F(CoverDeliveryTest, GroupTiltClosePropagatesOnlyWhenEveryMemberSupportsIt) {
  TestCover with_close;
  with_close.set_elero_parent(&hub);
  with_close.set_blind_address(0x111111);
  with_close.set_remote_address(0x123456);
  with_close.set_poll_interval(300000);
  with_close.set_supports_tilt(true);
  with_close.set_command_tilt(0x24);
  with_close.set_command_tilt_close(0x25);
  with_close.setup();

  TestCover without_close;
  without_close.set_elero_parent(&hub);
  without_close.set_blind_address(0x222222);
  without_close.set_remote_address(0x123456);
  without_close.set_poll_interval(300000);
  without_close.set_supports_tilt(true);
  without_close.set_command_tilt(0x24);
  // No command_tilt_close configured on this member.
  without_close.setup();

  TestGroup group;
  group.set_elero_parent(&hub);
  group.add_member(&with_close);
  group.add_member(&without_close);
  group.setup();

  // Each member's own initial status CHECK (queued by its setup()) is already
  // sitting in its queue; the group's gated TILT_CLOSE submission must not add
  // to that — nothing should be submitted when one member lacks the config.
  const auto with_close_baseline = with_close.get_command_delivery()->size();
  const auto without_close_baseline = without_close.get_command_delivery()->size();
  CoverCall tilt_close;
  tilt_close.tilt = 0.0f;
  group.control(tilt_close);
  EXPECT_EQ(with_close.get_command_delivery()->size(), with_close_baseline);
  EXPECT_EQ(without_close.get_command_delivery()->size(), without_close_baseline);
}

TEST_F(CoverDeliveryTest, ConcurrentSameProfileStopPreservesBothTwoPacketBursts) {
  TestCover other;
  other.set_elero_parent(&hub); other.set_blind_address(0x222222);
  other.set_remote_address(0x123456); other.set_poll_interval(300000); other.setup();
  cover.request_stop(); transmit(1010);
  other.request_stop();
  transmit(1020); transmit(1030); transmit(1040);
  ASSERT_EQ(hub.packets.size(), 4u);
  EXPECT_EQ(hub.packets[0].dest_addrs[0], 0x111111u);
  EXPECT_EQ(hub.packets[1].dest_addrs[0], 0x111111u);
  EXPECT_EQ(hub.packets[0].counter, hub.packets[1].counter);
  EXPECT_EQ(hub.packets[2].dest_addrs[0], 0x222222u);
  EXPECT_EQ(hub.packets[2].counter, hub.packets[3].counter);
}
