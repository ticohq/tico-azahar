#!/usr/bin/env bash
set -euo pipefail

mkdir -p patches

for path in $(git submodule status | awk '{print $2}'); do
  if ! git -C "$path" diff --quiet HEAD --; then
    name=$(echo "$path" | tr '/' '_')
    git -C "$path" diff HEAD > "patches/${name}.patch"
    echo "Created patches/${name}.patch"
  fi
done