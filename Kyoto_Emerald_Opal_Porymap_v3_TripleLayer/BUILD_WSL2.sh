#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sudo apt update
sudo apt install -y git python3 build-essential qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools
python3 build_kyoto_porymap.py --qmake qmake6 --make make --jobs "$(nproc)"
echo
echo "Built. Look in: $(pwd)/build/porymap-build"
