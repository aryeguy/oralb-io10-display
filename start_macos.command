#!/bin/zsh
set -e

PROJECT_DIR=${0:A:h}
ENV_DIR="$PROJECT_DIR/.venv"

cd "$PROJECT_DIR"

if [[ ! -x "$ENV_DIR/bin/python" ]]; then
  /usr/bin/python3 -m venv "$ENV_DIR"
fi

if ! "$ENV_DIR/bin/python" -c 'import aiohttp, bleak, ai_edge_litert' 2>/dev/null; then
  "$ENV_DIR/bin/python" -m pip install --quiet --disable-pip-version-check -r macos/requirements.txt
fi

exec "$ENV_DIR/bin/python" macos/backend.py --open
