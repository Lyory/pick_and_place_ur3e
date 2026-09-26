#include <chrono>
#include <future>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <gazebo/common/Events.hh>
#include <gazebo/common/Plugin.hh>
#include <gazebo/physics/physics.hh>
#include <gazebo_ros/node.hpp>
#include <rclcpp/rclcpp.hpp>
#include "ur3_llm_control/srv/set_attachment.hpp"

namespace gazebo {
class VirtualAttachment : public WorldPlugin {
public:
  void Load(physics::WorldPtr world, sdf::ElementPtr sdf) override {
    world_ = world;
    node_ = gazebo_ros::Node::Get(sdf);
    service_ = node_->create_service<ur3_llm_control::srv::SetAttachment>(
      "/set_virtual_attachment",
      [this](const std::shared_ptr<ur3_llm_control::srv::SetAttachment::Request> req,
             std::shared_ptr<ur3_llm_control::srv::SetAttachment::Response> resp) {
        auto job = std::make_shared<Job>();
        job->object = req->object;
        job->attach = req->attach;
        job->set_pose = req->set_pose;
        job->x = req->x;
        job->y = req->y;
        job->z = req->z;
        auto future = job->done.get_future();
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (pending_) {
            resp->message = "Attachment service busy";
            return;
          }
          pending_ = job;
        }
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
          resp->message = "Gazebo update timed out";
          return;
        }
        auto result = future.get();
        resp->success = result.first;
        resp->message = result.second;
      });
    update_ = event::Events::ConnectWorldUpdateBegin([this](const common::UpdateInfo &) { OnUpdate(); });
  }

private:
  struct Job {
    std::string object;
    bool attach;
    bool set_pose;
    double x, y, z;
    std::promise<std::pair<bool, std::string>> done;
  };

  void OnUpdate() {
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job.swap(pending_);
    }
    if (job) job->done.set_value(Apply(*job));
    if (cube_ && tool_) Freeze(cube_, tool_->WorldPose() * offset_);
    for (const auto &[name, pose] : placed_) {
      auto model = world_->ModelByName(name);
      if (model) Freeze(model, pose);
    }
  }

  static void Freeze(const physics::ModelPtr &model, const ignition::math::Pose3d &pose) {
    model->SetWorldPose(pose);
    for (const auto &link : model->GetLinks()) {
      link->SetLinearVel(ignition::math::Vector3d::Zero);
      link->SetAngularVel(ignition::math::Vector3d::Zero);
    }
  }

  std::pair<bool, std::string> Apply(const Job &job) {
    if (job.attach) {
      if (cube_) return {false, "Already holding " + held_};
      auto robot = world_->ModelByName("ur");
      auto cube = world_->ModelByName(job.object);
      if (!robot || !cube) return {false, "Robot or cube model unavailable"};
      auto tool = robot->GetLink("wrist_3_link");
      auto block = cube->GetLink("link");
      if (!tool || !block) return {false, "Tool or cube link unavailable"};
      if ((tool->WorldPose().Pos() - block->WorldPose().Pos()).Length() > 0.25)
        return {false, "Tool is too far from cube"};
      placed_.erase(job.object);
      offset_ = tool->WorldPose().Inverse() * cube->WorldPose();
      for (const auto &collision : block->GetCollisions()) collision->SetCollideBits(0);
      cube->SetStatic(true);
      tool_ = tool;
      cube_ = cube;
      held_ = job.object;
      return {true, "Attached " + held_};
    }
    if (!cube_ || held_ != job.object) return {false, "Object is not attached"};
    auto cube = cube_;
    cube_.reset();
    tool_.reset();
    if (job.set_pose) {
      const ignition::math::Pose3d pose(job.x, job.y, job.z, 0, 0, 0);
      placed_[job.object] = pose;
      Freeze(cube, pose);
    } else {
      Freeze(cube, cube->WorldPose());
    }
    held_.clear();
    return {true, "Detached " + job.object};
  }

  physics::WorldPtr world_;
  physics::ModelPtr cube_;
  physics::LinkPtr tool_;
  ignition::math::Pose3d offset_;
  std::map<std::string, ignition::math::Pose3d> placed_;
  std::string held_;
  gazebo_ros::Node::SharedPtr node_;
  rclcpp::Service<ur3_llm_control::srv::SetAttachment>::SharedPtr service_;
  event::ConnectionPtr update_;
  std::mutex mutex_;
  std::shared_ptr<Job> pending_;
};
GZ_REGISTER_WORLD_PLUGIN(VirtualAttachment)
}  // namespace gazebo
