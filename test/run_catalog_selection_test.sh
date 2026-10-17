#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
export PYTHONDONTWRITEBYTECODE=1
python3 "$repo/docs/catalog-payload/1.0.0/test_validate.py"
python3 "$repo/docs/catalog-selection/1.0.0/test_select.py"
