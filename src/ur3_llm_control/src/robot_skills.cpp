#include <chrono>
#include <cmath>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <Eigen/SVD>
#include <moveit/robot_state/robot_state.h>
#include <yaml-cpp/yaml.h>
#include <geometry_msgs/msg/pose.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/msg/collision_object.hpp>
#include <moveit_msgs/msg/attached_collision_object.hpp>
#include <moveit_msgs/msg/constraints.hpp>
#include <moveit_msgs/msg/object_color.hpp>
#include <moveit_msgs/msg/orientation_constraint.hpp>
#include <moveit_msgs/msg/position_constraint.hpp>
#include <shape_msgs/msg/solid_primitive.hpp>
#include <rclcpp/rclcpp.hpp>
#include "ur3_llm_control/srv/execute_skill.hpp"
#include "ur3_llm_control/srv/get_world_state.hpp"
#include "ur3_llm_control/srv/set_attachment.hpp"

using namespace std::chrono_literals;
using ExecuteSkill = ur3_llm_control::srv::ExecuteSkill;
using GetWorldState = ur3_llm_control::srv::GetWorldState;
using SetAttachment = ur3_llm_control::srv::SetAttachment;

struct Point {
  double x, y, z;
};
struct Result {
  std::string status;
  std::string message;
  bool ok() const { return status == "SUCCESS"; }
};

class RobotSkills {
public:
  explicit RobotSkills(const rclcpp::Node::SharedPtr &node)
      : node_(node), move_group_(node, "ur_manipulator") {
    const auto scene_file = node_->declare_parameter<std::string>("scene_file", "");
    if (scene_file.empty()) throw std::runtime_error("scene_file parameter is required");
    const YAML::Node config = YAML::LoadFile(scene_file);
    const auto home_file = node_->declare_parameter<std::string>("home_file", "");
    if (home_file.empty()) throw std::runtime_error("home_file parameter is required");
    const YAML::Node home_config = YAML::LoadFile(home_file);
    const auto staging_file = node_->declare_parameter<std::string>("staging_file", "");
    if (staging_file.empty()) throw std::runtime_error("staging_file parameter is required");
    const YAML::Node staging_config = YAML::LoadFile(staging_file);
    for (const auto *joint : {"shoulder_pan_joint", "shoulder_lift_joint", "elbow_joint",
                              "wrist_1_joint", "wrist_2_joint", "wrist_3_joint"}) {
      home_joints_[joint] = home_config[joint].as<double>();
      staging_joints_[joint] = staging_config[joint].as<double>();
    }
    frame_ = config["frame_id"].as<std::string>();
    base_height_ = config["robot_base_height"].as<double>();
    cube_size_ = config["cube_size"].as<double>();
    approach_ = config["approach_height"].as<double>();
    grasp_offset_ = config["grasp_offset"].as<double>();
    table_ = readPoint(config["table"]);
    for (const auto &entry : config["objects"]) {
      const auto key = entry.first.as<std::string>();
      starts_[key] = readPoint(entry.second);
      locations_[key] = key + "_start";
    }
    for (const auto &entry : config["zones"])
      zones_[entry.first.as<std::string>()] = readPoint(entry.second);
    for (size_t i = 0; i < 3; ++i)
      table_size_[i] = config["table"]["size"][i].as<double>();
    tray_width_ = config["zone_size"][0].as<double>();
    tray_floor_ = config["zone_size"][2].as<double>();
    tray_wall_ = config["zone_wall_thickness"].as<double>();
    tray_height_ = config["zone_wall_height"].as<double>();
    move_group_.setPoseReferenceFrame(frame_);
    move_group_.setEndEffectorLink("tool0");
    move_group_.setPlanningTime(10.0);
    move_group_.setNumPlanningAttempts(5);
    move_group_.setMaxVelocityScalingFactor(0.08);
    move_group_.setMaxAccelerationScalingFactor(0.08);
    configureSafetyConstraints();
    client_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    attach_client_ = node_->create_client<SetAttachment>(
      "/set_virtual_attachment", rmw_qos_profile_services_default, client_group_);
    joint_sub_ = node_->create_subscription<sensor_msgs::msg::JointState>(
      "/joint_states", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::JointState::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        for (size_t i = 0; i < msg->name.size() && i < msg->position.size(); ++i)
          raw_joints_[msg->name[i]] = msg->position[i];
      });
    if (!applyScene())
      RCLCPP_ERROR(node_->get_logger(), "Planning scene could not be initialized");
    server_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    skill_service_ = node_->create_service<ExecuteSkill>(
      "/execute_skill", [this](const std::shared_ptr<ExecuteSkill::Request> request,
                              std::shared_ptr<ExecuteSkill::Response> response) {
        Result result = execute(*request);
        response->success = result.ok();
        response->status = result.status;
        response->message = result.message;
        RCLCPP_INFO(node_->get_logger(), "%s(%s,%s): %s - %s", request->skill.c_str(),
                    request->object.c_str(), request->zone.c_str(),
                    result.status.c_str(), result.message.c_str());
      }, rmw_qos_profile_services_default, server_group_);
    state_service_ = node_->create_service<GetWorldState>(
      "/get_world_state", [this](const std::shared_ptr<GetWorldState::Request>,
                               std::shared_ptr<GetWorldState::Response> response) {
        for (const auto &[name, location] : locations_) {
          response->objects.push_back(name);
          response->locations.push_back(location);
        }
        response->held_object = held_;
      });
  }

private:
  static Point readPoint(const YAML::Node &n) {
    return {n["x"].as<double>(), n["y"].as<double>(), n["z"].as<double>()};
  }
  Point local(Point p) const {
    p.z -= base_height_;
    return p;
  }
  moveit_msgs::msg::CollisionObject box(const std::string &name, Point world,
                                         const std::vector<double> &size) const {
    moveit_msgs::msg::CollisionObject object;
    object.header.frame_id = frame_;
    object.id = name;
    auto p = local(world);
    geometry_msgs::msg::Pose pose;
    pose.position.x = p.x;
    pose.position.y = p.y;
    pose.position.z = p.z;
    pose.orientation.w = 1.0;
    shape_msgs::msg::SolidPrimitive primitive;
    primitive.type = shape_msgs::msg::SolidPrimitive::BOX;
    primitive.dimensions.assign(size.begin(), size.end());
    object.primitives.push_back(primitive);
    object.primitive_poses.push_back(pose);
    object.operation = moveit_msgs::msg::CollisionObject::ADD;
    return object;
  }
  static moveit_msgs::msg::ObjectColor color(const std::string &id, float r, float g,
                                              float b, float a = 1.0f) {
    moveit_msgs::msg::ObjectColor result;
    result.id = id;
    result.color.r = r;
    result.color.g = g;
    result.color.b = b;
    result.color.a = a;
    return result;
  }
  static std_msgs::msg::ColorRGBA cubeColor(const std::string &name) {
    std_msgs::msg::ColorRGBA c;
    c.a = 1.0f;
    if (name == "red_cube") { c.r = 0.95f; c.g = 0.05f; c.b = 0.04f; }
    else if (name == "yellow_cube") { c.r = 1.0f; c.g = 0.78f; c.b = 0.02f; }
    else { c.r = 0.04f; c.g = 0.25f; c.b = 0.98f; }
    return c;
  }
  moveit_msgs::msg::CollisionObject tray(const std::string &name, Point center) const {
    auto result = box(name, center, {tray_width_, tray_width_, tray_floor_});
    auto add_part = [&](Point p, const std::vector<double> &size) {
      const auto part = box(name, p, size);
      result.primitives.push_back(part.primitives.front());
      result.primitive_poses.push_back(part.primitive_poses.front());
    };
    const double wall_offset = (tray_width_ - tray_wall_) / 2.0;
    const double wall_z = center.z + (tray_floor_ + tray_height_) / 2.0;
    add_part({center.x - wall_offset, center.y, wall_z},
             {tray_wall_, tray_width_, tray_height_});
    add_part({center.x + wall_offset, center.y, wall_z},
             {tray_wall_, tray_width_, tray_height_});
    add_part({center.x, center.y - wall_offset, wall_z},
             {tray_width_ - 2.0 * tray_wall_, tray_wall_, tray_height_});
    add_part({center.x, center.y + wall_offset, wall_z},
             {tray_width_ - 2.0 * tray_wall_, tray_wall_, tray_height_});
    return result;
  }
  void configureSafetyConstraints() {
    safety_constraints_.name = "upright_above_work_surface";
    moveit_msgs::msg::OrientationConstraint orientation;
    orientation.header.frame_id = frame_;
    orientation.link_name = "tool0";
    orientation.orientation.y = 1.0;
    orientation.orientation.w = 0.0;
    orientation.absolute_x_axis_tolerance = 0.65;
    orientation.absolute_y_axis_tolerance = 0.65;
    orientation.absolute_z_axis_tolerance = 3.14159;
    orientation.weight = 1.0;
    safety_constraints_.orientation_constraints.push_back(orientation);

    moveit_msgs::msg::PositionConstraint position;
    position.header.frame_id = frame_;
    position.link_name = "tool0";
    shape_msgs::msg::SolidPrimitive region;
    region.type = shape_msgs::msg::SolidPrimitive::BOX;
    region.dimensions = {0.90, 1.0, 0.74};
    geometry_msgs::msg::Pose region_pose;
    region_pose.position.x = 0.35;   // x from -0.10 to 0.80
    region_pose.position.z = 0.43;   // world z from 0.80 to 1.54
    region_pose.orientation.w = 1.0;
    position.constraint_region.primitives.push_back(region);
    position.constraint_region.primitive_poses.push_back(region_pose);
    position.weight = 1.0;
    safety_constraints_.position_constraints.push_back(position);
    move_group_.setPathConstraints(safety_constraints_);
  }
  bool applyScene() {
    std::vector<moveit_msgs::msg::CollisionObject> objects;
    std::vector<moveit_msgs::msg::ObjectColor> colors;
    objects.push_back(box("safety_floor", {0.0, 0.0, 0.62}, {2.0, 2.0, 0.20}));
    colors.push_back(color("safety_floor", 0.35f, 0.37f, 0.40f, 0.12f));
    objects.push_back(box("table", table_, {table_size_[0], table_size_[1], table_size_[2]}));
    colors.push_back(color("table", 0.66f, 0.43f, 0.23f));
    for (const auto &[name, p] : starts_) {
      objects.push_back(box(name, p, {cube_size_, cube_size_, cube_size_}));
      const auto c = cubeColor(name);
      colors.push_back(color(name, c.r, c.g, c.b));
    }
    for (const auto &[name, p] : zones_) {
      objects.push_back(tray(name, p));
      if (name == "zone_a") colors.push_back(color(name, 0.60f, 0.50f, 0.72f));
      else if (name == "zone_b") colors.push_back(color(name, 0.36f, 0.68f, 0.62f));
      else colors.push_back(color(name, 0.82f, 0.57f, 0.35f));
    }
    for (int attempt = 0; attempt < 30 && rclcpp::ok(); ++attempt) {
      if (scene_.applyCollisionObjects(objects, colors)) return true;
      std::this_thread::sleep_for(1s);
    }
    return false;
  }
  Result checkTrajectory(const trajectory_msgs::msg::JointTrajectory &trajectory,
                         bool allow_upright = false) const {
    if (trajectory.points.empty())
      return {"SAFETY_REJECTED", "MoveIt returned an empty trajectory"};
    const auto model = move_group_.getRobotModel();
    const auto *group = model->getJointModelGroup("ur_manipulator");
    const auto *tool = model->getLinkModel("tool0");
    if (!group || !tool)
      return {"SAFETY_REJECTED", "Robot safety model is incomplete"};
    moveit::core::RobotState state(model);
    state.setToDefaultValues();
    double minimum_singular_value = 1e9;
    for (const auto &point : trajectory.points) {
      if (point.positions.size() != trajectory.joint_names.size())
        return {"SAFETY_REJECTED", "Trajectory joint dimensions do not match"};
      for (size_t j = 0; j < trajectory.joint_names.size(); ++j)
        state.setVariablePosition(trajectory.joint_names[j], point.positions[j]);
      state.update();
      if (!state.satisfiesBounds(group, 1e-5))
        return {"SAFETY_REJECTED", "Trajectory exceeds a joint limit"};
      const auto &pose = state.getGlobalLinkTransform(tool);
      if (pose.translation().z() + base_height_ < 0.80)
        return {"SAFETY_REJECTED", "Tool path drops below the safe work height"};
      if ((!allow_upright || pose.translation().z() + base_height_ < 0.90) &&
          pose.rotation()(2, 2) > -0.72)
        return {"SAFETY_REJECTED", "Tool path tilts away from the top-down grasp"};
      const auto *elbow = model->getJointModel("elbow_joint");
      const auto *wrist = model->getJointModel("wrist_2_joint");
      if (std::abs(std::sin(state.getVariablePosition(elbow->getVariableNames().front()))) < 0.10 ||
          std::abs(std::sin(state.getVariablePosition(wrist->getVariableNames().front()))) < 0.10)
        return {"SAFETY_REJECTED", "Trajectory approaches an elbow or wrist singularity"};
      Eigen::MatrixXd jacobian;
      if (!state.getJacobian(group, tool, Eigen::Vector3d::Zero(), jacobian))
        return {"SAFETY_REJECTED", "Could not check robot Jacobian"};
      const auto singular_values = jacobian.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).singularValues();
      if (singular_values.size() == 0)
        return {"SAFETY_REJECTED", "Robot Jacobian is empty"};
      minimum_singular_value = std::min(minimum_singular_value, singular_values.minCoeff());
      if (minimum_singular_value < 0.002)
        return {"SAFETY_REJECTED", "Trajectory approaches a singular configuration"};
    }
    return {"SUCCESS", "Trajectory passed safety checks"};
  }
  Result planExecute(bool allow_upright = false) {
    Result failure{"PLANNING_FAILED", "MoveIt could not plan a collision-free path"};
    for (int attempt = 0; attempt < 3; ++attempt) {
      move_group_.setStartStateToCurrentState();
      moveit::planning_interface::MoveGroupInterface::Plan plan;
      const auto planned = move_group_.plan(plan);
      if (planned != moveit::core::MoveItErrorCode::SUCCESS) {
        RCLCPP_WARN(node_->get_logger(), "Planning attempt %d failed", attempt + 1);
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        auto &trajectory = plan.trajectory_.joint_trajectory;
        if (trajectory.points.empty()) {
          failure = {"PLANNING_FAILED", "MoveIt returned an empty trajectory"};
          continue;
        }
        constexpr double two_pi = 6.283185307179586;
        bool state_ready = true;
        for (size_t j = 0; j < trajectory.joint_names.size(); ++j) {
          const auto &name = trajectory.joint_names[j];
          auto current = raw_joints_.find(name);
          if (current == raw_joints_.end()) {
            state_ready = false;
            break;
          }
          const double first = trajectory.points.front().positions[j];
          const double delta = current->second - first;
          const double turns = std::round(delta / two_pi);
          if (std::abs(delta - turns * two_pi) > 0.15) {
            state_ready = false;
            RCLCPP_WARN(node_->get_logger(), "Joint %s start differs from Gazebo by %.3f rad",
                        name.c_str(), delta);
            break;
          }
        }
        if (!state_ready) {
          failure = {"STATE_MISMATCH", "MoveIt trajectory does not start at Gazebo joint state"};
          continue;
        }
      }
      const Result safety = checkTrajectory(plan.trajectory_.joint_trajectory, allow_upright);
      if (!safety.ok()) {
        RCLCPP_WARN(node_->get_logger(), "Planning attempt %d rejected: %s", attempt + 1,
                    safety.message.c_str());
        failure = safety;
        continue;
      }
      const auto executed = move_group_.execute(plan);
      if (executed == moveit::core::MoveItErrorCode::SUCCESS) {
        move_group_.clearPoseTargets();
        return {"SUCCESS", "Motion complete"};
      }
      failure = {"EXECUTION_FAILED", "MoveIt could not execute the trajectory"};
      RCLCPP_WARN(node_->get_logger(), "Execution attempt %d failed", attempt + 1);
    }
    move_group_.clearPoseTargets();
    return failure;
  }
  Result moveToPose(double x, double y, double world_z,
                    double qx = 0.0, double qy = 1.0,
                    double qz = 0.0, double qw = 0.0) {
    // Keep the wrist above the scene while moving laterally, then descend vertically.
    if (qx == 0.0 && qy == 1.0 && qz == 0.0 && qw == 0.0) {
      const auto current = move_group_.getCurrentPose("tool0").pose.position;
      constexpr double travel_height = 1.00;
      Result linear{"SUCCESS", "Already at travel height"};
      if (current.z + base_height_ < travel_height - 0.005)
        linear = moveLinearToPose(current.x, current.y, travel_height);
      if (linear.ok()) linear = moveLinearToPose(x, y, travel_height);
      if (linear.ok()) linear = moveLinearToPose(x, y, world_z);
      if (linear.ok()) return linear;
      RCLCPP_WARN(node_->get_logger(), "Cartesian approach failed: %s", linear.message.c_str());
    }
    geometry_msgs::msg::Pose pose;
    pose.position.x = x;
    pose.position.y = y;
    pose.position.z = world_z - base_height_;
    pose.orientation.x = qx;
    pose.orientation.y = qy;
    pose.orientation.z = qz;
    pose.orientation.w = qw;
    if (!move_group_.setPoseTarget(pose, "tool0"))
      return {"PLANNING_FAILED", "Target pose is unreachable"};
    return planExecute();
  }
  Result moveLinearToPose(double x, double y, double world_z) {
    geometry_msgs::msg::Pose pose;
    pose.position.x = x;
    pose.position.y = y;
    pose.position.z = world_z - base_height_;
    pose.orientation.y = 1.0;
    pose.orientation.w = 0.0;
    moveit_msgs::msg::RobotTrajectory trajectory;
    const double fraction = move_group_.computeCartesianPath({pose}, 0.005, 0.0, trajectory,
                                                          safety_constraints_, true);
    if (fraction < 0.99)
      return {"PLANNING_FAILED", "Cartesian path blocked; fraction " + std::to_string(fraction)};
    {
      std::lock_guard<std::mutex> lock(joint_mutex_);
      constexpr double two_pi = 6.283185307179586;
      auto &joint_path = trajectory.joint_trajectory;
      if (joint_path.points.empty())
        return {"PLANNING_FAILED", "Cartesian path is empty"};
      for (size_t j = 0; j < joint_path.joint_names.size(); ++j) {
        auto actual = raw_joints_.find(joint_path.joint_names[j]);
        if (actual == raw_joints_.end())
          return {"STATE_UNAVAILABLE", "Gazebo joint state unavailable"};
        const double delta = actual->second - joint_path.points.front().positions[j];
        const double turns = std::round(delta / two_pi);
        if (std::abs(delta - turns * two_pi) > 0.15)
          return {"STATE_MISMATCH", "Cartesian path starts away from Gazebo joint state"};
      }
    }
    const Result safety = checkTrajectory(trajectory.joint_trajectory);
    if (!safety.ok()) return safety;
    if (move_group_.execute(trajectory) != moveit::core::MoveItErrorCode::SUCCESS)
      return {"EXECUTION_FAILED", "MoveIt could not execute the Cartesian path"};
    return {"SUCCESS", "Reached Home pose"};
  }
  bool nearJoints(const std::map<std::string, double> &target) {
    const auto state = move_group_.getCurrentState(2.0);
    if (!state) return false;
    for (const auto &[joint, value] : target) {
      const double delta = state->getVariablePosition(joint) - value;
      if (std::abs(std::remainder(delta, 2.0 * M_PI)) > 0.02) return false;
    }
    return true;
  }
  Result moveToStaging() {
    if (nearJoints(staging_joints_)) return {"SUCCESS", "Already at staging pose"};
    move_group_.clearPathConstraints();
    if (!move_group_.setJointValueTarget(staging_joints_)) {
      move_group_.setPathConstraints(safety_constraints_);
      return {"PLANNING_FAILED", "Staging joint target is invalid"};
    }
    Result result = planExecute(true);
    move_group_.setPathConstraints(safety_constraints_);
    return result;
  }
  Result home() {
    if (nearJoints(home_joints_)) return {"SUCCESS", "Already at upright Home pose"};
    Result stage = moveToPose(0.25, 0.0, 0.98);
    if (!stage.ok()) return {stage.status, "Home staging: " + stage.message};
    move_group_.clearPathConstraints();
    if (!move_group_.setJointValueTarget(home_joints_)) {
      move_group_.setPathConstraints(safety_constraints_);
      return {"PLANNING_FAILED", "Home joint target is invalid"};
    }
    Result result = planExecute(true);
    move_group_.setPathConstraints(safety_constraints_);
    return result;
  }
  bool gazeboAttach(const std::string &name, bool attach, std::string &message,
                    const Point *target = nullptr) {
    if (!attach_client_->wait_for_service(5s)) {
      message = "Gazebo attachment service unavailable";
      return false;
    }
    auto request = std::make_shared<SetAttachment::Request>();
    request->object = name;
    request->attach = attach;
    request->set_pose = target != nullptr;
    if (target) {
      request->x = target->x;
      request->y = target->y;
      request->z = target->z;
    }
    auto future = attach_client_->async_send_request(request);
    if (future.wait_for(8s) != std::future_status::ready) {
      message = "Gazebo attachment service timed out";
      return false;
    }
    auto response = future.get();
    message = response->message;
    return response->success;
  }
  Point position(const std::string &name) const {
    const auto &location = locations_.at(name);
    if (location == name + "_start") return starts_.at(name);
    auto zone = zones_.at(location);
    zone.z += 0.0225;  // zone half thickness + cube half height
    return zone;
  }
  Result pick(const std::string &name) {
    if (!starts_.count(name)) return {"INVALID_OBJECT", "Unknown object: " + name};
    if (!held_.empty()) return {"ALREADY_HOLDING", "Robot is holding " + held_};
    Point p = position(name);
    Result result = moveToStaging();
    if (!result.ok()) { result.message = "Staging: " + result.message; return result; }
    result = moveToPose(p.x, p.y, p.z + approach_);
    if (!result.ok()) { result.message = "Pre-grasp: " + result.message; return result; }
    result = moveLinearToPose(p.x, p.y, p.z + grasp_offset_);
    if (!result.ok()) { result.message = "Grasp: " + result.message; return result; }
    std::string message;
    if (!gazeboAttach(name, true, message))
      return {"ATTACH_FAILED", message};
    moveit_msgs::msg::AttachedCollisionObject attached;
    attached.link_name = "tool0";
    attached.object = box(name, p, {cube_size_ - 0.002, cube_size_ - 0.002, cube_size_ - 0.002});
    attached.object.header.frame_id = "tool0";
    attached.object.primitive_poses.front().position.x = 0.0;
    attached.object.primitive_poses.front().position.y = 0.0;
    // tool0 faces downward, so positive tool-local Z is below the wrist.
    attached.object.primitive_poses.front().position.z = grasp_offset_;
    attached.touch_links = {"tool0", "wrist_3_link"};
    if (!scene_.applyAttachedCollisionObject(attached) ||
        !scene_.getAttachedObjects({name}).count(name)) {
      gazeboAttach(name, false, message);
      return {"ATTACH_FAILED", "MoveIt Planning Scene could not attach the object"};
    }
    held_ = name;
    locations_[name] = "held";
    return {"SUCCESS", name + " picked successfully"};
  }
  Result place(const std::string &name, const std::string &zone_name) {
    if (!starts_.count(name)) return {"INVALID_OBJECT", "Unknown object: " + name};
    if (!zones_.count(zone_name)) return {"INVALID_ZONE", "Unknown zone: " + zone_name};
    if (held_ != name) return {"NOT_HOLDING_OBJECT", "Robot is not holding " + name};
    for (const auto &[object, location] : locations_)
      if (object != name && location == zone_name)
        return {"ZONE_OCCUPIED", zone_name + " contains " + object};
    Point p = zones_.at(zone_name);
    p.z += 0.0225;
    Result result = moveToPose(p.x, p.y, p.z + approach_);
    if (!result.ok()) return result;
    result = moveLinearToPose(p.x, p.y, p.z + grasp_offset_ + 0.005);
    if (!result.ok()) return result;
    std::string message;
    if (!gazeboAttach(name, false, message, &p)) return {"DETACH_FAILED", message};
    moveit_msgs::msg::AttachedCollisionObject detached;
    detached.link_name = "tool0";
    detached.object.id = name;
    detached.object.operation = moveit_msgs::msg::CollisionObject::REMOVE;
    if (!scene_.applyAttachedCollisionObject(detached))
      return {"DETACH_FAILED", "MoveIt Planning Scene could not detach the object"};
    held_.clear();
    locations_[name] = zone_name;
    if (!scene_.applyCollisionObject(box(name, p, {cube_size_, cube_size_, cube_size_}),
                                     cubeColor(name)))
      return {"SCENE_FAILED", "Cube placed, but Planning Scene update failed"};
    return {"SUCCESS", name + " placed in " + zone_name};
  }
  Result execute(const ExecuteSkill::Request &req) {
    if (req.skill == "home") {
      if (!req.object.empty() || !req.zone.empty())
        return {"INVALID_ARGUMENT", "home takes no object or zone"};
      if (!held_.empty()) return {"ALREADY_HOLDING", "Place held object before home"};
      return home();
    }
    if (req.skill == "pick") {
      if (!req.zone.empty()) return {"INVALID_ARGUMENT", "pick takes no zone"};
      return pick(req.object);
    }
    if (req.skill == "place") return place(req.object, req.zone);
    return {"INVALID_SKILL", "Unknown skill: " + req.skill};
  }

  rclcpp::Node::SharedPtr node_;
  moveit::planning_interface::MoveGroupInterface move_group_;
  moveit::planning_interface::PlanningSceneInterface scene_;
  rclcpp::CallbackGroup::SharedPtr client_group_, server_group_;
  rclcpp::Client<SetAttachment>::SharedPtr attach_client_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
  std::mutex joint_mutex_;
  std::map<std::string, double> raw_joints_;
  rclcpp::Service<ExecuteSkill>::SharedPtr skill_service_;
  rclcpp::Service<GetWorldState>::SharedPtr state_service_;
  std::string frame_, held_;
  double base_height_, cube_size_, approach_, grasp_offset_;
  double table_size_[3];
  double tray_width_, tray_floor_, tray_wall_, tray_height_;
  moveit_msgs::msg::Constraints safety_constraints_;
  Point table_;
  std::map<std::string, Point> starts_, zones_;
  std::map<std::string, double> home_joints_, staging_joints_;
  std::map<std::string, std::string> locations_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("robot_skills");
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 4);
  executor.add_node(node);
  std::thread spinning([&executor]() { executor.spin(); });
  try {
    RobotSkills skills(node);
    RCLCPP_INFO(node->get_logger(), "UR3e skill server ready");
    spinning.join();
  } catch (const std::exception &e) {
    RCLCPP_FATAL(node->get_logger(), "Skill server failed: %s", e.what());
    executor.cancel();
    spinning.join();
  }
  rclcpp::shutdown();
  return 0;
}
