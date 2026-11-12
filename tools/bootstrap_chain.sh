#!/usr/bin/env bash
# The auditable path of the bootstrap (docs/bootstrap.md): stage 0 of the
# root release builds that release, each later release is built by the one
# before it, and each one rebuilds itself to a fixed point. Only a C
# compiler and clang are used; no seed is read.
#
# The root is the newest release whose stage 0 builds it: 0.2.0 cannot
# build 0.5.0, whose std uses `where` bounds.
#
# The current sources are built by the last release, or by their own
# stage 0 while they use features newer than it (`current_from=stage0`).
# Either way the result must write the same LLVM IR for compiler/ as stage 2
# of `make verify`, and pass the tests.
#
# Usage: tools/bootstrap_chain.sh [work-dir]   (default: build/chain)
# Needs the release tags (`git fetch --tags`).
set -euo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work=$(mkdir -p "${1:-build/chain}" && cd "${1:-build/chain}" && pwd)
exe=""
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exe=".exe" ;;
esac

# The current sources use `const` and `let`, which no release has shipped
# yet: set this to `release` once one has.
current_from=stage0

# The releases from the root on, oldest first, as listed in seed/SEEDS.md.
root_tag=v0.5.0
tags=$(sed -n 's/^| \[\([0-9][0-9.]*\)\].*/v\1/p' seed/SEEDS.md \
    | sed -n "/^$root_tag\$/,\$p")
if [ -z "$tags" ]; then
    echo "bootstrap_chain: no release from $root_tag on in seed/SEEDS.md"
    exit 1
fi

# build <compiler> <src> <std> <runtime> <out>: <out>/ofir from <src>, then
# <out>/ofir rebuilt by itself (<out>/self) writes the same LLVM IR.
build() {
    local cc=$1 src=$2 std=$3 rt=$4 out=$5
    (cd "$src" && OFIR_STD="$std" OFIR_RUNTIME="$rt" \
        "$cc" build compiler/main.of -o "$out/ofir$exe")
    (cd "$src" && OFIR_STD="$std" OFIR_RUNTIME="$rt" \
        "$out/ofir$exe" build compiler/main.of -o "$out/self$exe")
    (cd "$src" && OFIR_STD="$std" OFIR_RUNTIME="$rt" \
        "$out/ofir$exe" build compiler/main.of --emit=llvm -o "$out/a")
    (cd "$src" && OFIR_STD="$std" OFIR_RUNTIME="$rt" \
        "$out/self$exe" build compiler/main.of --emit=llvm -o "$out/b")
    cmp "$out/a.ll" "$out/b.ll"
    "$out/ofir$exe" version
}

prev=""
for tag in $tags; do
    src="$work/src-$tag"
    out="$work/$tag"
    rm -rf "$src" "$out"
    git worktree prune
    git worktree add --detach "$src" "$tag" > /dev/null
    mkdir -p "$out"
    make -C "$src" runtime BUILD="$out" > "$work/$tag.log" 2>&1 \
        || { tail -20 "$work/$tag.log"; exit 1; }
    cc=$prev
    if [ -z "$cc" ]; then
        echo "bootstrap_chain: $tag built by its stage 0"
        make -C "$src" stage0 BUILD="$out" >> "$work/$tag.log" 2>&1 \
            || { tail -20 "$work/$tag.log"; exit 1; }
        cc="$out/stage0/ofir$exe"
    else
        echo "bootstrap_chain: $tag built by the previous release"
    fi
    build "$cc" "$src" "$src/std" "$out/libofir_rt.a" "$out"
    prev="$out/ofir$exe"
done
for tag in $tags; do
    git worktree remove --force "$work/src-$tag"
done

# The current sources.
make verify > "$work/verify.log" 2>&1 || { tail -20 "$work/verify.log"; exit 1; }
std="$root/std"
rt="$root/build/libofir_rt.a"
out="$work/current"
rm -rf "$out"
mkdir -p "$out"
if [ "$current_from" = release ]; then
    echo "bootstrap_chain: current sources built by the last release"
    cc=$prev
else
    echo "bootstrap_chain: current sources built by their stage 0" \
        "(they need features newer than the last release)"
    cc="$root/build/stage0/ofir$exe"
fi
build "$cc" "$root" "$std" "$rt" "$out"
cmp "$out/a.ll" build/stage2/self.ll
echo "bootstrap_chain: the chain gives the same LLVM IR for compiler/ as make verify"

OFIR_STD="$std" OFIR_RUNTIME="$rt" \
    python3 tests/runner/run_tests.py --compiler "$out/ofir$exe" --stage 1
