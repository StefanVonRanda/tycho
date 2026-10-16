#!/bin/sh
# make ci on Linux without leaving this machine: a throwaway debian:trixie
# container (native arch -- the amd64 release image is emulated and has no
# pkg-config) over a fresh clone of HEAD, so the host's binaries and build/ are
# never touched and an uncommitted change is deliberately NOT tested.
#
# Why this exists: every lane here is also green on Linux only when somebody
# runs it there. On 2026-10-16 that found a reference-compiler defect
# (FRICTION 146, one paren pair per `+`) that macOS had hidden for three weeks
# behind make's raised stack limit, plus a locale-sorted golden. There is no
# hosted CI for this repository and there will not be one; this is the Linux
# gate.
#
# Usage: sh scripts/ci_linux.sh [FUZZ_N]   (or `make ci-linux`)
# Needs: podman with a running machine. Takes ~15 minutes on an M-series Mac.
set -eu
cd "$(dirname "$0")/.."
N="${1:-200}"
command -v podman >/dev/null 2>&1 || { echo "ci_linux: podman not found" >&2; exit 2; }
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT HUP INT TERM
git clone -q . "$W/tree"
cat > "$W/inner.sh" <<'INNER'
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get -qq update >/dev/null 2>&1
# docs/platforms.md's list, plus clang (the sweep runs both compilers), gdb
# (debug-check), openssl (tls-verify's s_server), locales (the comma-decimal
# locale fixtures refuse to pass without one, by design).
apt-get -qq install -y --no-install-recommends build-essential clang gdb python3 git perl \
    zlib1g-dev libssl-dev libcurl4-openssl-dev libpng-dev libsqlite3-dev pkg-config \
    openssl locales >/dev/null 2>&1
sed -i 's/^# da_DK.UTF-8/da_DK.UTF-8/' /etc/locale.gen && locale-gen >/dev/null 2>&1
# The bind mount is owned by the host user; without this, git ls-files (which
# goldens-check and script-check shell out to) refuses with "dubious ownership".
git config --global --add safe.directory /w
cd /w
make ci "$@"
INNER
arch="$(uname -m)"; case "$arch" in arm64|aarch64) plat=linux/arm64 ;; *) plat=linux/amd64 ;; esac
echo "ci_linux: debian:trixie $plat, clone of $(git rev-parse --short HEAD), FUZZ_N=$N"
podman run --rm --platform "$plat" -v "$W/tree:/w" -v "$W/inner.sh:/inner.sh:ro" -w /w \
    docker.io/library/debian:trixie sh /inner.sh N="$N"
