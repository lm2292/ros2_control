// Copyright 2020 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "controller_interface/controller_interface.hpp"
#include "controller_manager/controller_manager.hpp"
#include "controller_manager_test_common.hpp"
#include "lifecycle_msgs/msg/state.hpp"

using test_controller::TEST_CONTROLLER_CLASS_NAME;
using ::testing::_;
using ::testing::Return;
const auto controller_name1 = "test_controller1";
const auto controller_name2 = "test_controller2";
using strvec = std::vector<std::string>;

class TestLoadController : public ControllerManagerFixture
{
protected:
  void _switch_test_controllers(
    const strvec & start_controllers, const strvec & stop_controllers,
    const std::future_status expected_future_status = std::future_status::timeout,
    const controller_interface::return_type expected_interface_status =
      controller_interface::return_type::OK)
  {
    // First activation not possible because controller not configured
    auto switch_future = std::async(
      std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
      start_controllers, stop_controllers, STRICT, true, rclcpp::Duration(0, 0));

    ASSERT_EQ(expected_future_status, switch_future.wait_for(std::chrono::milliseconds(100)))
      << "switch_controller should be blocking until next update cycle";
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(expected_interface_status, switch_future.get());
  }
};

TEST_F(TestLoadController, load_unknown_controller)
{
  ASSERT_EQ(cm_->load_controller("unknown_controller_name", "unknown_controller_type"), nullptr);
}

TEST_F(TestLoadController, load_controller_failed_init)
{
  ASSERT_EQ(
    cm_->load_controller(
      "test_controller_failed_init",
      test_controller_failed_init::TEST_CONTROLLER_FAILED_INIT_CLASS_NAME),
    nullptr);
}

TEST_F(TestLoadController, configuring_non_loaded_controller_fails)
{
  // try configure non-loaded controller
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::ERROR);
}

class TestLoadedController : public TestLoadController
{
public:
  controller_interface::ControllerInterfaceSharedPtr controller_if{nullptr};

  void SetUp() override
  {
    TestLoadController::SetUp();

    controller_if = cm_->load_controller(controller_name1, TEST_CONTROLLER_CLASS_NAME);
    ASSERT_NE(controller_if, nullptr);
  }

  void start_test_controller(
    const std::future_status expected_future_status = std::future_status::timeout,
    const controller_interface::return_type expected_interface_status =
      controller_interface::return_type::OK)
  {
    _switch_test_controllers(
      strvec{controller_name1}, strvec{}, expected_future_status, expected_interface_status);
  }

  void stop_test_controller(
    const std::future_status expected_future_status = std::future_status::timeout,
    const controller_interface::return_type expected_interface_status =
      controller_interface::return_type::OK)
  {
    _switch_test_controllers(
      strvec{}, strvec{controller_name1}, expected_future_status, expected_interface_status);
  }
};

TEST_F(TestLoadedController, load_and_configure_one_known_controller)
{
  EXPECT_EQ(1u, cm_->get_loaded_controllers().size());

  EXPECT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

  cm_->configure_controller(controller_name1);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());
}

TEST_F(TestLoadedController, can_start_configured_controller)
{
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);
  start_test_controller();
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if->get_state().id());
}

TEST_F(TestLoadedController, can_stop_active_controller)
{
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);

  start_test_controller();

  // Stop controller
  stop_test_controller();
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());
}

TEST_F(TestLoadedController, starting_and_stopping_a_controller)
{
  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

  // Only testing with STRICT now for simplicity
  {  // Test starting unconfigured controller, and starting configured afterwards
    start_test_controller(std::future_status::ready, controller_interface::return_type::ERROR);

    ASSERT_EQ(
      lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

    // Activate configured controller
    cm_->configure_controller(controller_name1);
    start_test_controller();
    ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if->get_state().id());
  }

  {  // Stop controller
    stop_test_controller();
    ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());
  }
}

TEST_F(TestLoadedController, can_not_configure_active_controller)
{
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);
  start_test_controller();

  // Can not configure active controller
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::ERROR);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if->get_state().id());
}

TEST_F(TestLoadedController, can_not_start_finalized_controller)
{
  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

  // Shutdown controller on purpose for testing
  ASSERT_EQ(
    controller_if->get_node()->shutdown().id(),
    lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED);

  //  Start controller
  start_test_controller(std::future_status::ready, controller_interface::return_type::ERROR);

  // Can not configure unconfigured controller
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::ERROR);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, controller_if->get_state().id());
}

TEST_F(TestLoadedController, inactive_controller_cannot_be_cleaned_up)
{
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);

  start_test_controller();

  stop_test_controller();

  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());

  std::shared_ptr<test_controller::TestController> test_controller =
    std::dynamic_pointer_cast<test_controller::TestController>(controller_if);
  size_t cleanup_calls = 0;
  test_controller->cleanup_calls = &cleanup_calls;
  // Configure from inactive state: controller can no be cleaned-up
  test_controller->simulate_cleanup_failure = true;
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::ERROR);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());
  EXPECT_EQ(0u, cleanup_calls);
}

TEST_F(TestLoadedController, inactive_controller_cannot_be_configured)
{
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);

  start_test_controller();

  stop_test_controller();
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());

  std::shared_ptr<test_controller::TestController> test_controller =
    std::dynamic_pointer_cast<test_controller::TestController>(controller_if);
  size_t cleanup_calls = 0;
  test_controller->cleanup_calls = &cleanup_calls;
  // Configure from inactive state
  test_controller->simulate_cleanup_failure = false;
  EXPECT_EQ(cm_->configure_controller(controller_name1), controller_interface::return_type::OK);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());
  EXPECT_EQ(1u, cleanup_calls);
}

class SwitchTest
: public TestLoadedController,
  public ::testing::WithParamInterface<
    std::tuple<controller_interface::return_type, int, strvec, strvec, std::string>>
{
};

const auto UNSPECIFIED = 0;
const auto EMPTY_STR_VEC = strvec{};
const auto NONEXISTENT_CONTROLLER = strvec{"nonexistent_controller"};
const auto VALID_CONTROLLER = strvec{controller_name1};
const auto VALID_PLUS_NONEXISTENT_CONTROLLERS = strvec{controller_name1, "nonexistent_controller"};

TEST_P(SwitchTest, EmptyListOrNonExistentTest)
{
  EXPECT_EQ(1u, cm_->get_loaded_controllers().size());

  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

  auto params = GetParam();
  auto result = std::get<0>(params);
  auto strictness = std::get<1>(params);
  auto start_controllers = std::get<2>(params);
  auto stop_controllers = std::get<3>(params);
  auto error_message = std::get<4>(params);

  EXPECT_EQ(
    result, cm_->switch_controller(
              start_controllers, stop_controllers, strictness, true, rclcpp::Duration(0, 0)))
    << error_message;
}

INSTANTIATE_TEST_SUITE_P(
  EmptyListOrNonExistentTest, SwitchTest,
  ::testing::Values(
    // empty lists
    std::make_tuple(
      controller_interface::return_type::OK, UNSPECIFIED, EMPTY_STR_VEC, EMPTY_STR_VEC,
      "Switch with no controllers specified and strictness UNSPECIFIED didn't return OK"),
    std::make_tuple(
      controller_interface::return_type::OK, STRICT, EMPTY_STR_VEC, EMPTY_STR_VEC,
      "Switch with no controllers specified and strictness STRICT didn't return OK"),
    std::make_tuple(
      controller_interface::return_type::OK, BEST_EFFORT, EMPTY_STR_VEC, EMPTY_STR_VEC,
      "Switch with no controllers specified and strictness BEST_EFFORT didn't return OK"),
    // combination of empty and non-existent controller
    std::make_tuple(
      controller_interface::return_type::OK, UNSPECIFIED, NONEXISTENT_CONTROLLER, EMPTY_STR_VEC,
      "Switch with nonexistent start controller specified and strictness UNSPECIFIED didn't return "
      "OK"),
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, NONEXISTENT_CONTROLLER, EMPTY_STR_VEC,
      "Switch with nonexistent start controller specified and strictness STRICT didn't return "
      "ERROR"),
    std::make_tuple(
      controller_interface::return_type::OK, BEST_EFFORT, NONEXISTENT_CONTROLLER, EMPTY_STR_VEC,
      "Switch with nonexistent start controller specified and strictness BEST_EFFORT didn't return "
      "OK"),
    std::make_tuple(
      controller_interface::return_type::OK, UNSPECIFIED, EMPTY_STR_VEC, NONEXISTENT_CONTROLLER,
      "Switch with nonexistent stop controller specified and strictness UNSPECIFIED didn't return "
      "OK"),
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, EMPTY_STR_VEC, NONEXISTENT_CONTROLLER,
      "Switch with nonexistent stop controller specified and strictness STRICT didn't return "
      "ERROR"),
    std::make_tuple(
      controller_interface::return_type::OK, BEST_EFFORT, EMPTY_STR_VEC, NONEXISTENT_CONTROLLER,
      "Switch with nonexistent stop controller specified and strictness BEST_EFFORT didn't return "
      "OK"),
    std::make_tuple(
      controller_interface::return_type::OK, UNSPECIFIED, NONEXISTENT_CONTROLLER,
      NONEXISTENT_CONTROLLER,
      "Switch with nonexistent start and stop controllers specified, and strictness UNSPECIFIED, "
      "didn't return OK"),
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, NONEXISTENT_CONTROLLER,
      NONEXISTENT_CONTROLLER,
      "Switch with nonexistent start and stop controllers specified, and strictness STRICT, didn't "
      "return ERROR"),
    std::make_tuple(
      controller_interface::return_type::OK, BEST_EFFORT, NONEXISTENT_CONTROLLER,
      NONEXISTENT_CONTROLLER,
      "Switch with nonexistent start and stop controllers specified, and strictness BEST_EFFORT, "
      "didn't return OK"),
    // valid controller used
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, NONEXISTENT_CONTROLLER, VALID_CONTROLLER,
      "Switch with valid stopped controller and nonexistent start controller specified, and "
      "strictness STRICT, didn't return ERROR"),
    std::make_tuple(
      controller_interface::return_type::OK, BEST_EFFORT, NONEXISTENT_CONTROLLER, VALID_CONTROLLER,
      "Switch with valid stopped controller specified, nonexistent start controller and strictness "
      "BEST_EFFORT didn't return OK"),
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, VALID_PLUS_NONEXISTENT_CONTROLLERS,
      EMPTY_STR_VEC,
      "Switch with valid and nonexistent start controller specified and strictness STRICT didn't "
      "return ERROR"),
    std::make_tuple(
      controller_interface::return_type::ERROR, STRICT, VALID_CONTROLLER, NONEXISTENT_CONTROLLER,
      "Switch with valid start controller and nonexistent controller specified, and strinctness "
      "STRICT, didn't return ERROR")));

class TestTwoLoadedControllers : public TestLoadController
{
public:
  controller_interface::ControllerInterfaceSharedPtr controller_if1{nullptr};
  controller_interface::ControllerInterfaceSharedPtr controller_if2{nullptr};

  void SetUp() override
  {
    TestLoadController::SetUp();
    controller_if1 = cm_->load_controller(controller_name1, TEST_CONTROLLER_CLASS_NAME);
    ASSERT_NE(controller_if1, nullptr);
    EXPECT_EQ(1u, cm_->get_loaded_controllers().size());
    controller_if2 = cm_->load_controller(controller_name2, TEST_CONTROLLER_CLASS_NAME);
    ASSERT_NE(controller_if2, nullptr);
    EXPECT_EQ(2u, cm_->get_loaded_controllers().size());
    ASSERT_EQ(
      lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if1->get_state().id());
    ASSERT_EQ(
      lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if2->get_state().id());
  }

  void switch_test_controllers(
    const strvec & start_controllers, const strvec & stop_controllers,
    const std::future_status expected_future_status = std::future_status::timeout,
    const controller_interface::return_type expected_interface_status =
      controller_interface::return_type::OK)
  {
    _switch_test_controllers(
      start_controllers, stop_controllers, expected_future_status, expected_interface_status);
  }
};

TEST_F(TestTwoLoadedControllers, load_and_configure_two_known_controllers)
{
  cm_->configure_controller(controller_name1);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if1->get_state().id());

  cm_->configure_controller(controller_name2);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if2->get_state().id());
}

TEST_F(TestTwoLoadedControllers, switch_multiple_controllers)
{
  // Only testing with STRICT now for simplicity
  //  Test starting a stopped controller, and stopping afterwards

  cm_->configure_controller(controller_name1);

  // Start controller #1
  RCLCPP_INFO(cm_->get_logger(), "Starting stopped controller #1");
  switch_test_controllers(strvec{controller_name1}, strvec{});
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if1->get_state().id());
  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if2->get_state().id());

  // Stop controller 1, start controller 2
  // Both fail because controller 2 because it is not configured and STRICT is used
  RCLCPP_INFO(
    cm_->get_logger(),
    "Stopping controller #1, starting unconfigured controller #2 fails (STRICT)");
  switch_test_controllers(
    strvec{controller_name2}, strvec{controller_name1}, std::future_status::ready,
    controller_interface::return_type::ERROR);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if1->get_state().id());
  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if2->get_state().id());

  cm_->configure_controller(controller_name2);

  // Stop controller 1
  RCLCPP_INFO(cm_->get_logger(), "Stopping controller #1");
  switch_test_controllers(strvec{}, strvec{controller_name1});
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if1->get_state().id());
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if2->get_state().id());

  // Start controller 1 again
  RCLCPP_INFO(cm_->get_logger(), "Starting stopped controller #1");
  switch_test_controllers(strvec{controller_name1}, strvec{});
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if1->get_state().id());
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if2->get_state().id());

  // Stop controller 1, start controller 2
  RCLCPP_INFO(cm_->get_logger(), "Stopping controller #1, starting controller #2");
  switch_test_controllers(strvec{controller_name2}, strvec{controller_name1});
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if1->get_state().id());
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, controller_if2->get_state().id());

  // Stop controller 2
  RCLCPP_INFO(cm_->get_logger(), "Stopping controller #2");
  switch_test_controllers(strvec{}, strvec{controller_name2});
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if2->get_state().id());
}

TEST_F(TestLoadController, can_set_and_get_non_default_update_rate)
{
  auto controller_if =
    cm_->load_controller("test_controller_01", test_controller::TEST_CONTROLLER_CLASS_NAME);
  ASSERT_NE(controller_if, nullptr);

  ASSERT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, controller_if->get_state().id());

  controller_if->get_node()->set_parameter({"update_rate", 1337});

  cm_->configure_controller("test_controller_01");
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, controller_if->get_state().id());

  EXPECT_EQ(1337u, controller_if->get_update_rate());
}
