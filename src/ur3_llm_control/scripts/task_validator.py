#!/usr/bin/env python3
"""Validate syntax, whitelist, and sequential robot state before execution."""
import json
import re
from pathlib import Path
import yaml
from ament_index_python.packages import get_package_share_directory

VALID_SKILLS = {"home", "pick", "place"}
VALID_OBJECTS = {"red_cube", "yellow_cube", "blue_cube"}
VALID_ZONES = {"zone_a", "zone_b", "zone_c", "buffer"}
TASK_MAP = {
    0: {"zone_a": "red_cube", "zone_b": "yellow_cube", "zone_c": "blue_cube"},
    1: {"zone_a": "red_cube", "zone_b": "blue_cube", "zone_c": "yellow_cube"},
    2: {"zone_a": "yellow_cube", "zone_b": "red_cube", "zone_c": "blue_cube"},
    3: {"zone_a": "yellow_cube", "zone_b": "blue_cube", "zone_c": "red_cube"},
    4: {"zone_a": "blue_cube", "zone_b": "red_cube", "zone_c": "yellow_cube"},
    5: {"zone_a": "blue_cube", "zone_b": "yellow_cube", "zone_c": "red_cube"},
}


class ValidationError(Exception):
    def __init__(self, status, message):
        self.status = status
        super().__init__(message)


def student_mapping(config_path=None, *, suffix=None):
    if suffix is None:
        path = Path(config_path or (
            Path(get_package_share_directory("ur3_llm_control")) / "config/student_config.yaml"
        ))
        student = yaml.safe_load(path.read_text(encoding="utf-8"))["student"]
        student_id = str(student.get("id", ""))
        suffix = student_id[-2:]
    if not isinstance(suffix, str) or not re.fullmatch(r"[0-9]{2}", suffix):
        raise ValidationError("INVALID_STUDENT_ID", "Enter a two-digit student ID suffix")
    return TASK_MAP[int(suffix) % 6]


def plan_student_rearrangement(world_state, mapping):
    """Use the buffer to free a zone when every destination is occupied."""
    locations = dict(world_state.get("locations", {}))
    if set(locations) != VALID_OBJECTS or world_state.get("held_object", ""):
        raise ValidationError("INVALID_STATE", "Expected three unheld cubes")
    targets = {obj: zone for zone, obj in mapping.items()}
    if set(targets) != VALID_OBJECTS:
        raise ValidationError("INVALID_STATE", "Student mapping must assign all cubes")
    steps = []
    while any(locations[obj] != targets[obj] for obj in VALID_OBJECTS):
        occupied = {place: obj for obj, place in locations.items() if place in VALID_ZONES}
        movable = next((obj for obj in targets
                        if locations[obj] != targets[obj] and targets[obj] not in occupied), None)
        if movable is None:
            if "buffer" in occupied:
                raise ValidationError("INVALID_STATE", "No free destination or buffer")
            movable = next(obj for obj in targets if locations[obj] != targets[obj])
            destination = "buffer"
        else:
            destination = targets[movable]
        steps.extend((
            {"skill": "pick", "object": movable},
            {"skill": "place", "object": movable, "zone": destination},
        ))
        locations[movable] = destination
    if steps:
        steps.append({"skill": "home"})
    return {"plan": steps}


def validate(plan_document, world_state):
    if isinstance(plan_document, str):
        try:
            plan_document = json.loads(plan_document)
        except json.JSONDecodeError as exc:
            raise ValidationError("INVALID_JSON", "Plan must be JSON") from exc
    if not isinstance(plan_document, dict) or set(plan_document) != {"plan"}:
        raise ValidationError("INVALID_PLAN", "Expected exactly one top-level 'plan' key")
    steps = plan_document["plan"]
    if not isinstance(steps, list) or not steps or len(steps) > 20:
        raise ValidationError("INVALID_PLAN", "Plan must contain 1 to 20 steps")
    if not isinstance(world_state, dict):
        raise ValidationError("INVALID_STATE", "World state is unavailable")
    locations = world_state.get("locations", {})
    held = world_state.get("held_object", "")
    if set(locations) != VALID_OBJECTS or held not in VALID_OBJECTS | {""}:
        raise ValidationError("INVALID_STATE", "World state has unexpected objects")
    locations = dict(locations)
    if held and locations[held] != "held":
        raise ValidationError("INVALID_STATE", "Held object state is inconsistent")
    normalized = []
    for index, step in enumerate(steps, 1):
        if not isinstance(step, dict) or "skill" not in step:
            raise ValidationError("INVALID_STEP", f"Step {index} must be an object with skill")
        if set(step) - {"skill", "object", "zone"}:
            raise ValidationError("INVALID_STEP", f"Step {index} has unexpected keys")
        skill = step["skill"]
        if skill not in VALID_SKILLS:
            raise ValidationError("INVALID_SKILL", f"Step {index}: {skill}")
        obj = step.get("object", "")
        zone = step.get("zone", "")
        if not isinstance(obj, str) or not isinstance(zone, str):
            raise ValidationError("INVALID_STEP", f"Step {index} arguments must be strings")
        if skill == "home":
            if obj or zone:
                raise ValidationError("INVALID_STEP", "home takes no arguments")
            if held:
                raise ValidationError("INVALID_SEQUENCE", "Cannot home while holding a cube")
        elif skill == "pick":
            if obj not in VALID_OBJECTS:
                raise ValidationError("INVALID_OBJECT", f"Step {index}: {obj}")
            if zone:
                raise ValidationError("INVALID_STEP", "pick takes no zone")
            if held:
                raise ValidationError("INVALID_SEQUENCE", "Robot already holds a cube")
            held = obj
            locations[obj] = "held"
        else:
            if obj not in VALID_OBJECTS:
                raise ValidationError("INVALID_OBJECT", f"Step {index}: {obj}")
            if zone not in VALID_ZONES:
                raise ValidationError("INVALID_ZONE", f"Step {index}: {zone}")
            if held != obj:
                raise ValidationError("INVALID_SEQUENCE", f"Must pick {obj} before placing")
            if any(name != obj and loc == zone for name, loc in locations.items()):
                raise ValidationError("ZONE_OCCUPIED", f"Step {index}: {zone} is occupied")
            held = ""
            locations[obj] = zone
        normalized.append({"skill": skill, "object": obj, "zone": zone})
    if held:
        raise ValidationError("INVALID_SEQUENCE", "Plan must place the held cube")
    if not any(step["skill"] == "pick" for step in normalized):
        raise ValidationError("INVALID_SEQUENCE", "Plan must pick and place a cube")
    if normalized[-1]["skill"] != "home":
        raise ValidationError("INVALID_SEQUENCE", "Plan must finish with home")
    return normalized
