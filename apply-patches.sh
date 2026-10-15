#!/usr/bin/env bash
# Apply every patch in patches/ to its submodule: the reverse of
# make-patches.sh, which names a submodule's patch after its path with '/'
# turned into '_' (externals/dynarmic -> patches/externals_dynarmic.patch).
# Run after "git submodule update --init --recursive". A patch already
# applied is left as it is.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="$ROOT/patches"
failed=0

while read -r path; do
    name=$(echo "$path" | tr '/' '_')
    patch="$PATCH_DIR/$name.patch"
    [ -f "$patch" ] || continue
    dir="$ROOT/$path"

    if [ ! -e "$dir/.git" ]; then
        echo "[skip]    $name: $path is not checked out (git submodule update --init)"
        failed=1
    elif git -C "$dir" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "[ok]      $name: already applied to $path"
    elif git -C "$dir" apply --check "$patch" >/dev/null 2>&1; then
        git -C "$dir" apply "$patch"
        echo "[applied] $name -> $path"
    else
        echo "[error]   $name: does not apply cleanly to $path"
        failed=1
    fi
done < <(git -C "$ROOT" submodule status | awk '{print $2}')

exit "$failed"
