#!/usr/bin/env bash
set -euo pipefail

if [[ ! -t 0 ]]; then
  echo 'Run this script in an interactive terminal.' >&2
  exit 1
fi

config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/ur3_llm_control"
install -d -m 700 "$config_dir"
umask 077

printf 'Paste your 9Router API key, then press Enter: ' >&2
IFS= read -r -s api_key
printf '\n' >&2
if [[ -z "$api_key" ]]; then
  echo 'No key entered; nothing was saved.' >&2
  exit 1
fi

temp_file="$(mktemp "$config_dir/.llm.env.XXXXXX")"
trap 'rm -f -- "$temp_file"; unset api_key' EXIT
printf 'export NINEROUTER_API_KEY=%q\n' "$api_key" > "$temp_file"
chmod 600 "$temp_file"
mv -f -- "$temp_file" "$config_dir/llm.env"
echo "Saved 9Router key in $config_dir/llm.env (mode 600)."
