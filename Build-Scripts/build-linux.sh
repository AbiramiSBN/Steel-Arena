#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build -j"$(nproc)"
./build/SteelArenaShooterLocal
