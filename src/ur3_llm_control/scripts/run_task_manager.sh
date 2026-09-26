#!/usr/bin/env bash
set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_src="$(cd -- "$script_dir/../.." && pwd)"

source /opt/ros/humble/setup.bash
source "$workspace_src/install/setup.bash"

private_config="${XDG_CONFIG_HOME:-$HOME/.config}/ur3_llm_control/llm.env"
if [[ -f "$private_config" ]]; then
  source "$private_config"
fi

export ROBOT_LLM_MODEL="${ROBOT_LLM_MODEL:-ag/gemini-3.8-flash}"
export ROBOT_LLM_BASE_URL="${ROBOT_LLM_BASE_URL:-http://127.0.0.1:20128/v1}"

if [[ -z "${NINEROUTER_API_KEY:-}" ]]; then
  read -rsp '9Router API key: ' NINEROUTER_API_KEY
  echo
  export NINEROUTER_API_KEY
fi

exec ros2 run ur3_llm_control task_manager.py
