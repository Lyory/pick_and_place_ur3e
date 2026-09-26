#!/usr/bin/env python3
"""Interactive or ROS-topic entry point for validated LLM tasks."""
import argparse
import json
import re
import threading
import rclpy
from rclpy.executors import MultiThreadedExecutor
from rclpy.node import Node
from std_msgs.msg import String
from ur3_llm_control.srv import GetWorldState
from llm_planner import LLMPlanner, PlannerError
from task_validator import validate, student_mapping, plan_student_rearrangement, ValidationError
from skill_executor import SkillExecutor, SkillExecutionError


class TaskManager(Node):
    def __init__(self, topic_mode=False):
        super().__init__("task_manager_topic" if topic_mode else "task_manager")
        self.planner = None
        self.state_client = self.create_client(GetWorldState, "/get_world_state")
        self.skill_executor = SkillExecutor(self)
        self.busy = threading.Lock()
        if topic_mode:
            self.create_subscription(String, "/ur3_llm_control/command", self.on_command, 10)

    def on_command(self, msg):
        if not self.busy.acquire(blocking=False):
            self.get_logger().warning("Task already in progress")
            return
        threading.Thread(target=self._run_topic, args=(msg.data,), daemon=True).start()

    def _run_topic(self, command):
        try:
            self.run_command(command)
        finally:
            self.busy.release()

    def world_state(self):
        if not self.state_client.wait_for_service(timeout_sec=10):
            raise SkillExecutionError("SERVICE_UNAVAILABLE", "/get_world_state is unavailable")
        done = threading.Event()
        future = self.state_client.call_async(GetWorldState.Request())
        future.add_done_callback(lambda _: done.set())
        if not done.wait(10) or future.result() is None:
            raise SkillExecutionError("SERVICE_TIMEOUT", "Could not read robot world state")
        response = future.result()
        return {
            "locations": dict(zip(response.objects, response.locations)),
            "held_object": response.held_object,
        }

    def run_command(self, command):
        print("\n========================================\nUR3e LLM Robot Control\n========================================")
        print(f"USER COMMAND:\n{command}")
        try:
            state = self.world_state()
            suffix = command.strip() if re.fullmatch(r"[0-9]{2}", command.strip()) else None
            wants_student = suffix is not None or "student id" in command.lower() or "mssv" in command.lower()
            mapping = student_mapping(suffix=suffix) if wants_student else None
            locations = state["locations"]
            occupied = set(locations.values())
            rearranging = mapping is not None and (
                {"zone_a", "zone_b", "zone_c"} <= occupied or "buffer" in occupied
            )
            if rearranging:
                document = plan_student_rearrangement(state, mapping)
                if not document["plan"]:
                    print("\nTASK ALREADY COMPLETE: cubes are in their assigned zones")
                    return
            else:
                if self.planner is None:
                    self.planner = LLMPlanner()
                document = self.planner.generate_plan(command, state, mapping)
            steps = validate(document, state)
            if mapping is not None:
                desired = {zone: obj for zone, obj in mapping.items()}
                resulting = dict(state["locations"])
                for step in steps:
                    if step["skill"] == "place":
                        resulting[step["object"]] = step["zone"]
                if any(resulting[obj] != zone for zone, obj in desired.items()):
                    raise ValidationError("INVALID_STUDENT_PLAN", "Plan does not satisfy student ID mapping")
            print("\nREARRANGEMENT PLAN:" if rearranging else "\nLLM PLAN:")
            for i, step in enumerate(steps, 1):
                arguments = ", ".join(x for x in (step["object"], step["zone"]) if x)
                print(f"{i}. {step['skill']}({arguments})")
            print("\nEXECUTION:")
            self.skill_executor.execute(steps)
            print("\n========================================\nTASK SUCCESS\n========================================")
        except (PlannerError, ValidationError, SkillExecutionError) as exc:
            status = getattr(exc, "status", "PLANNER_FAILED")
            print(f"\n========================================\nTASK FAILED: {status}\n{exc}\n========================================")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--topic", action="store_true", help="Listen on /ur3_llm_control/command")
    args = parser.parse_args(rclpy.utilities.remove_ros_args()[1:])
    rclpy.init()
    node = TaskManager(args.topic)
    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(node)
    spin_thread = threading.Thread(target=executor.spin, daemon=True)
    spin_thread.start()
    try:
        if args.topic:
            spin_thread.join()
        else:
            while rclpy.ok():
                try:
                    command = input("\nEnter command or last two digits of student ID (e.g. 23):\n> ").strip()
                except (EOFError, KeyboardInterrupt):
                    break
                if command:
                    node.run_command(command)
    except KeyboardInterrupt:
        pass
    finally:
        executor.shutdown()
        spin_thread.join(timeout=2.0)
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
