#!/bin/sh
# The Ofir installer (ADR 0082). POSIX shell; needs tar and sha256sum or
# shasum.
#
#   sh install.sh [--prefix DIR] [--sums FILE] [--no-verify] [ARCHIVE]
#   sh install.sh [--prefix DIR] --use VERSION
#   sh install.sh [--prefix DIR] --list
#   sh install.sh [--prefix DIR] --remove VERSION
#   sh install.sh [--prefix DIR] --uninstall
#
# Each version is kept in <prefix>/toolchains/<version>; the active one is
# copied to the prefix itself. share/ofir/version names it and
# share/ofir/files lists the files of the copy. The prefix defaults to
# $OFIR_HOME, then ~/.ofir.

set -eu
LC_ALL=C
export LC_ALL

usage() {
    sed -n '5,9s/^#   //p' "$0" >&2
    exit 2
}

die() {
    echo "install.sh: $*" >&2
    exit 1
}

prefix=${OFIR_HOME:-${HOME:-}/.ofir}
action=install
version=
archive=
sums=
verify=yes
while [ $# -gt 0 ]; do
    case $1 in
        --prefix) [ $# -ge 2 ] || usage; prefix=$2; shift ;;
        --sums) [ $# -ge 2 ] || usage; sums=$2; shift ;;
        --no-verify) verify=no ;;
        --use|--remove) [ $# -ge 2 ] || usage; action=${1#--}; version=$2; shift ;;
        --list) action=list ;;
        --uninstall) action=uninstall ;;
        -h|--help) usage ;;
        -*) echo "install.sh: unknown option $1" >&2; usage ;;
        *) [ -z "$archive" ] || usage; archive=$1 ;;
    esac
    shift
done
[ -n "$prefix" ] || die "no prefix: set OFIR_HOME or pass --prefix"

checkName() {
    case $1 in
        ''|.*|*[!0-9A-Za-z.+-]*) die "\`$1\` is not a version" ;;
    esac
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        die "neither sha256sum nor shasum is installed"
    fi
}

# Removes the directory $1 (relative to the prefix) and its parents while
# they are empty.
rmdirs() {
    d=$1
    while :; do
        rmdir "$prefix/$d" 2>/dev/null || return 0
        case $d in
            */*) d=${d%/*} ;;
            *) return 0 ;;
        esac
    done
}

# Removes the files listed in $1 (relative to the prefix) and the
# directories they leave empty.
removeFiles() {
    while IFS= read -r f; do
        rm -f "$prefix/$f"
        case $f in
            */*) rmdirs "${f%/*}" ;;
        esac
    done < "$1"
}

active() {
    if [ -f "$prefix/share/ofir/version" ]; then
        cat "$prefix/share/ofir/version"
    fi
}

# Makes the installed version $1 the active one.
use() {
    src=$prefix/toolchains/$1
    [ -d "$src" ] || die "version $1 is not installed in $prefix"
    list=$prefix/.files.$$
    (cd "$src" && find bin lib share install.sh -type f 2>/dev/null) | sort > "$list"
    printf '%s\n' share/ofir/files share/ofir/version >> "$list"
    old=$prefix/share/ofir/files
    if [ -f "$old" ]; then
        # The files of the old copy that the new one does not have.
        sort "$old" | comm -23 - "$list" > "$list.old"
        removeFiles "$list.old"
        rm -f "$list.old"
    fi
    while IFS= read -r f; do
        case $f in
            share/ofir/files|share/ofir/version) continue ;;
        esac
        mkdir -p "$prefix/$(dirname "$f")"
        # Removed first: a signed executable must not be overwritten in place.
        rm -f "$prefix/$f"
        cp -p "$src/$f" "$prefix/$f"
    done < "$list"
    mkdir -p "$prefix/share/ofir"
    echo "$1" > "$prefix/share/ofir/version"
    mv "$list" "$prefix/share/ofir/files"
    echo "using ofir $1 in $prefix"
}

case $action in
    list)
        [ -d "$prefix/toolchains" ] || exit 0
        current=$(active)
        for d in "$prefix"/toolchains/*; do
            [ -d "$d" ] || continue
            v=${d##*/}
            if [ "$v" = "$current" ]; then
                echo "$v (active)"
            else
                echo "$v"
            fi
        done
        exit 0
        ;;
    use)
        checkName "$version"
        use "$version"
        exit 0
        ;;
    remove)
        checkName "$version"
        [ -d "$prefix/toolchains/$version" ] || die "version $version is not installed in $prefix"
        [ "$version" != "$(active)" ] || die "version $version is active: --use another one first"
        rm -rf "$prefix/toolchains/$version"
        rmdir "$prefix/toolchains" 2>/dev/null || true
        echo "removed ofir $version"
        exit 0
        ;;
    uninstall)
        if [ -f "$prefix/share/ofir/files" ]; then
            cp "$prefix/share/ofir/files" "$prefix/.files.$$"
            removeFiles "$prefix/.files.$$"
            rm -f "$prefix/.files.$$"
        fi
        rm -rf "$prefix/toolchains" "$prefix"/.install.* "$prefix"/.files.*
        rmdir "$prefix" 2>/dev/null || true
        echo "uninstalled ofir from $prefix"
        exit 0
        ;;
esac

# Install: check the archive before anything is written.
if [ -n "$archive" ]; then
    [ -f "$archive" ] || die "no archive $archive"
    if [ "$verify" = yes ]; then
        [ -n "$sums" ] || sums=$archive.sha256
        [ -f "$sums" ] || die "no checksum for $archive (looked for $sums; --no-verify skips it)"
        name=${archive##*/}
        expected=$(awk -v n="$name" '{ f = $2; sub(/^\*/, "", f); if (f == n) print $1 }' "$sums")
        [ -n "$expected" ] || die "$sums has no checksum for $name"
        actual=$(sha256 "$archive")
        [ "$actual" = "$expected" ] || die "the checksum of $name is $actual, not $expected: nothing was installed"
    fi
fi

mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
# A directory left by an interrupted installation goes first.
rm -rf "$prefix"/.install.* "$prefix"/.files.*
tmp=$prefix/.install.$$
mkdir "$tmp"
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT PIPE TERM

if [ -n "$archive" ]; then
    (cd "$tmp" && tar -xzf -) < "$archive" || die "could not extract $archive"
    set -- "$tmp"/ofir-*
    [ $# -eq 1 ] && [ -d "$1" ] || die "$archive does not hold one ofir-<version> directory"
    tree=$1
else
    # Run from an extracted archive: install its tree.
    here=$(cd "$(dirname "$0")" && pwd)
    [ -d "$here/lib/ofir/std" ] || die "no archive given, and $here is not an extracted archive"
    tree=$tmp/${here##*/}
    cp -Rp "$here" "$tree"
fi
version=${tree##*/}
version=${version#ofir-}
checkName "$version"
[ -f "$tree/bin/ofir" ] || [ -f "$tree/bin/ofir.exe" ] || die "the archive has no bin/ofir"

mkdir -p "$prefix/toolchains"
dest=$prefix/toolchains/$version
if [ -d "$dest" ]; then
    mv "$dest" "$tmp/previous"
fi
mv "$tree" "$dest"
use "$version"
case :${PATH:-}: in
    *:"$prefix/bin":*) ;;
    *) echo "add it to PATH: export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
