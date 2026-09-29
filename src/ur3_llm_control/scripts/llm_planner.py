#!/usr/bin/env python3
"""Request a JSON task plan from a local OpenAI-compatible 9Router endpoint."""
import json
import os
import re
from pathlib import Path
from ament_index_python.packages import get_package_share_directory

DEFAULT_BASE_URL = "http://localhost:20128/v1"
ENV_KEYS = {"NINEROUTER_API_KEY", "ROBOT_LLM_MODEL", "ROBOT_LLM_BASE_URL"}


def load_local_env():
    """Read the workspace .env without executing its contents as shell code."""
    selected = os.getenv("ROBOT_LLM_ENV_FILE")
    if selected:
        candidates = [Path(selected).expanduser()]
    else:
        candidates = [parent / ".env" for parent in Path(__file__).resolve().parents]
        candidates += [Path.cwd() / ".env"]
        candidates += [parent / ".env" for parent in Path.cwd().parents]
    for path in candidates:
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[7:].strip()
            name, separator, value = line.partition("=")
            name, value = name.strip(), value.strip()
            if separator and name in ENV_KEYS and value:
                if value[0] in ("\"", "'") and value[-1] == value[0]:
                    value = value[1:-1]
                os.environ[name] = value
        return path
    return None


class PlannerError(Exception):
    pass


class LLMPlanner:
    def __init__(self, prompt_path=None, model=None, base_url=None):
        load_local_env()
        self.prompt_path = Path(prompt_path or (
            Path(get_package_share_directory("ur3_llm_control")) / "prompt/planner_prompt.txt"
        ))
        self.model = model or os.getenv("ROBOT_LLM_MODEL", "")
        self.base_url = base_url or os.getenv("ROBOT_LLM_BASE_URL", DEFAULT_BASE_URL)
        if not self.model:
            raise PlannerError("Set ROBOT_LLM_MODEL in .env to a model available in 9Router")
        try:
            from openai import OpenAI
        except ImportError as exc:
            raise PlannerError("Install the openai Python package") from exc
        api_key = os.getenv("NINEROUTER_API_KEY")
        if not api_key:
            raise PlannerError("Set NINEROUTER_API_KEY in .env to the API key copied from the 9Router dashboard")
        self.client = OpenAI(
            api_key=api_key,
            base_url=self.base_url,
            timeout=60.0,
        )

    def generate_plan(self, command, world_state, student_mapping=None):
        if not isinstance(command, str) or not command.strip():
            raise PlannerError("Command is empty")
        context = {
            "world_state": world_state,
            "held_object": world_state.get("held_object", ""),
        }
        if student_mapping is not None:
            context["student_task_configuration"] = student_mapping
        system = self.prompt_path.read_text(encoding="utf-8") + "\nCurrent context:\n" + json.dumps(context)
        try:
            response = self.client.chat.completions.create(
                model=self.model,
                temperature=0,
                messages=[
                    {"role": "system", "content": system},
                    {"role": "user", "content": command},
                ],
            )
            raw = response.choices[0].message.content
            if not isinstance(raw, str):
                raise ValueError("LLM response content is empty")
            raw = raw.strip()
            fenced = re.fullmatch(r"```(?:json)?\s*\n(.*?)\n```", raw, re.DOTALL | re.IGNORECASE)
            if fenced:
                raw = fenced.group(1).strip()
            return json.loads(raw)
        except (ValueError, IndexError, AttributeError, TypeError) as exc:
            raise PlannerError("LLM returned invalid JSON") from exc
        except Exception as exc:
            raise PlannerError(f"9Router request failed: {exc}") from exc
