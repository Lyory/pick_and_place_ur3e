#!/usr/bin/env bash
set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_src="$(cd -- "$script_dir/../.." && pwd)"

source /opt/ros/humble/setup.bash
source "$workspace_src/install/setup.bash"

# The Python planner loads the ignored workspace .env automatically.
if [[ -f "$workspace_src/.env" || -n "${ROBOT_LLM_ENV_FILE:-}" ]]; then
  exec ros2 run ur3_llm_control task_manager.py
fi

private_config="${XDG_CONFIG_HOME:-$HOME/.config}/ur3_llm_control/llm.env"
if [[ -f "$private_config" ]]; then
  source "$private_config"
fi

if [[ -z "${ROBOT_LLM_MODEL:-}" ]]; then
  read -rp 'Full model ID from your 9Router dashboard: ' ROBOT_LLM_MODEL
  if [[ -z "$ROBOT_LLM_MODEL" ]]; then
    echo "Model is required. Fill in ROBOT_LLM_MODEL in $workspace_src/.env with the full model ID from 9Router."
    exit 1
  fi
fi
export ROBOT_LLM_MODEL
export ROBOT_LLM_BASE_URL="${ROBOT_LLM_BASE_URL:-http://127.0.0.1:20128/v1}"

if [[ -z "${NINEROUTER_API_KEY:-}" ]]; then
  read -rsp '9Router API key: ' NINEROUTER_API_KEY
  echo
  export NINEROUTER_API_KEY
fi

exec ros2 run ur3_llm_control task_manager.py
