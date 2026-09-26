#!/usr/bin/env python3
"""Execute validated skills in order, stopping at the first failure."""
import threading
from ur3_llm_control.srv import ExecuteSkill


class SkillExecutionError(Exception):
    def __init__(self, status, message):
        self.status = status
        super().__init__(message)


class SkillExecutor:
    def __init__(self, node):
        self.client = node.create_client(ExecuteSkill, "/execute_skill")

    def execute(self, steps, report=print):
        if not self.client.wait_for_service(timeout_sec=10):
            raise SkillExecutionError("SERVICE_UNAVAILABLE", "/execute_skill is unavailable")
        for step in steps:
            request = ExecuteSkill.Request()
            request.skill = step["skill"]
            request.object = step["object"]
            request.zone = step["zone"]
            done = threading.Event()
            future = self.client.call_async(request)
            future.add_done_callback(lambda _: done.set())
            if not done.wait(180):
                raise SkillExecutionError("SERVICE_TIMEOUT", "Skill service timed out")
            response = future.result()
            if response is None:
                raise SkillExecutionError("SERVICE_FAILED", "No skill response")
            label = step["skill"] + (
                f"({step['object']}, {step['zone']})" if step["zone"] else
                f"({step['object']})" if step["object"] else "()"
            )
            report(f"{label:.<40} {response.status}")
            if not response.success:
                raise SkillExecutionError(response.status, response.message)
