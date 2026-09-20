set -eu
# Unattended Windows 11 x64 install media, built ON the x86_64 box.
#
# Same shape as the arm64 recipe with one decisive difference: the amd64 virtio
# drivers come from the official virtio-win project and are WHQL-signed, so
# Setup loads them with no publisher-trust prompt. That is why this side is
# fully unattended while arm64 needed a console once.
#
# Runs on the Linux host, not the Mac -- the ESD is already there and the VM
# will be there.
here="$(cd "$(dirname "$0")" && pwd)"
VMDIR="${VMDIR:-$HOME/vm/win11x64}"
ESD="$VMDIR/win11x64.esd"
OUT="$VMDIR/win11x64-unattend.iso"
VIRTIO="$VMDIR/virtio-win.iso"

[ -f "$ESD" ] || { echo "no ESD at $ESD" >&2; exit 2; }
for t in wimlib-imagex xorriso; do command -v "$t" >/dev/null || { echo "missing $t" >&2; exit 2; }; done

# The Pro index is verified BY NAME, never trusted as a number -- Microsoft
# reorders editions between builds and installing Home by accident is a silent
# substitution that surfaces much later.
idx=$(wimlib-imagex info "$ESD" 2>/dev/null \
      | awk '/^Index:/{i=$2} /^Name:/{ $1=""; sub(/^ /,""); if ($0=="Windows 11 Pro") {print i; exit} }')
[ -n "$idx" ] || { echo "no 'Windows 11 Pro' image in $ESD" >&2; exit 1; }
echo ">>> Windows 11 Pro is index $idx"

[ -f "$VIRTIO" ] || {
    echo ">>> fetching virtio-win (WHQL-signed amd64 drivers)"
    curl -sL --retry 3 -o "$VIRTIO" \
      https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso
}

T="$(mktemp -d)"; trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf "$T"' EXIT
iso="$T/iso"; mkdir -p "$iso/sources"

echo ">>> [1/5] setup media tree";      wimlib-imagex apply "$ESD" 1 "$iso" --no-acls >/dev/null
echo ">>> [2/5] boot.wim"
wimlib-imagex export "$ESD" 2 "$iso/sources/boot.wim" --compress=LZX >/dev/null
wimlib-imagex export "$ESD" 3 "$iso/sources/boot.wim" --compress=LZX --boot >/dev/null
echo ">>> [3/5] install.wim (Pro)";     wimlib-imagex export "$ESD" "$idx" "$iso/sources/install.wim" --compress=LZX >/dev/null

echo ">>> [4/5] answer file + drivers"
cp "$here/autounattend.xml" "$iso/autounattend.xml"
mkdir -p "$iso/sources/\$OEM\$/\$1" "$iso/Drivers"
cp "$here/provision.ps1" "$iso/sources/\$OEM\$/\$1/provision.ps1"
xorriso -osirrox on:auto_chmod_on -indev "$VIRTIO" \
    -extract /NetKVM/w11/amd64 "$iso/Drivers/NetKVM" >/dev/null 2>&1 || true
[ -f "$iso/Drivers/NetKVM/netkvm.inf" ] || {
    echo "NetKVM amd64 driver did not extract -- the guest would have no network." >&2; exit 1; }

echo ">>> [5/5] xorriso (BIOS + UEFI)"
xorriso -as mkisofs -quiet -iso-level 3 -J -joliet-long -rational-rock -volid WIN11X64 \
    -b boot/etfsboot.com -no-emul-boot -boot-load-size 8 \
    -eltorito-alt-boot -e efi/microsoft/boot/efisys_noprompt.bin -no-emul-boot \
    -o "$OUT" "$iso"
echo; echo "ISO: $OUT ($(du -h "$OUT" | cut -f1))"
