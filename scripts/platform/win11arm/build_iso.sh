set -eu
# Turn Microsoft's ARM64 ESD into a bootable ISO that installs UNATTENDED.
#
# The ESD is not an ISO: it is a WIM archive holding the setup file tree, two
# boot images and the editions, and it has to be reassembled. Doing that by
# hand is how a VM becomes unreproducible, so the whole recipe lives here --
# including which index is which, because those numbers are the part a reader
# cannot guess.
#
#   1  Windows Setup Media        the ISO's own file tree
#   2  Microsoft Windows PE       boot.wim image 1
#   3  Microsoft Windows Setup    boot.wim image 2 (the bootable one)
#   6  Windows 11 Pro             install.wim   <- the edition we install
#
# Usage: sh build_iso.sh [ESD] [OUT.iso]

here="$(cd "$(dirname "$0")" && pwd)"
ESD="${1:-$HOME/vm/win11arm/win11arm64.esd}"
OUT="${2:-$HOME/vm/win11arm/win11arm64-unattend.iso}"
PRO_INDEX=6

[ -f "$ESD" ] || { echo "no ESD at $ESD" >&2; exit 2; }
for t in wimlib-imagex xorriso; do
    command -v "$t" >/dev/null || { echo "missing $t (brew install wimlib xorriso)" >&2; exit 2; }
done

# The edition index is checked by NAME, never trusted as a number: Microsoft
# reorders these between builds, and installing "Home" because the index moved
# would be a silent substitution that only shows up much later.
got="$(wimlib-imagex info "$ESD" "$PRO_INDEX" 2>/dev/null | awk -F': *' '/^Name:/{print $2; exit}')"
[ "$got" = "Windows 11 Pro" ] || {
    echo "index $PRO_INDEX is '$got', not 'Windows 11 Pro' -- the ESD layout moved." >&2
    wimlib-imagex info "$ESD" 2>/dev/null | awk '/^Index:/{i=$2} /^Name:/{$1="";print "  "i": "$0}' >&2
    exit 1
}

T="$(mktemp -d)"; trap 'chmod -R u+w "$T" 2>/dev/null; rm -rf "$T"' EXIT
iso="$T/iso"; mkdir -p "$iso/sources"

echo ">>> [1/5] setup media tree"
wimlib-imagex apply "$ESD" 1 "$iso" --no-acls >/dev/null

echo ">>> [2/5] boot.wim (WinPE + Setup)"
wimlib-imagex export "$ESD" 2 "$iso/sources/boot.wim" --compress=LZX >/dev/null
wimlib-imagex export "$ESD" 3 "$iso/sources/boot.wim" --compress=LZX --boot >/dev/null

echo ">>> [3/5] install.wim (Windows 11 Pro)"
wimlib-imagex export "$ESD" "$PRO_INDEX" "$iso/sources/install.wim" --compress=LZX >/dev/null

echo ">>> [4/5] answer file + provisioning + ARM64 virtio drivers"
# The drivers go ON THE INSTALL ISO, not on a second CD. A second CD means the
# drive letter has to be guessed in the answer file, and a guess that lands on
# the wrong letter fails silently -- Setup just installs with no NIC, which is
# a VM you cannot reach and cannot diagnose (no network, no display, both
# devices being virtio). On the install media the path is fixed and known.
UTMTOOLS="${UTMTOOLS:-$HOME/vm/win11arm/utm-guest-tools.iso}"
if [ ! -f "$UTMTOOLS" ]; then
    echo "    fetching UTM guest tools (the only source of ARM64 virtio drivers)"
    curl -sL --retry 3 -o "$UTMTOOLS" \
        "https://getutm.app/downloads/utm-guest-tools-latest.iso" \
      || { echo "cannot fetch UTM guest tools" >&2; exit 1; }
fi
mkdir -p "$iso/Drivers"
for d in NetKVM viogpudo vioinput vioserial; do
    xorriso -osirrox on:auto_chmod_on -indev "$UTMTOOLS" \
        -extract "/Drivers/$d/w11/ARM64" "$iso/Drivers/$d" >/dev/null 2>&1 || true
done
[ -f "$iso/Drivers/NetKVM/netkvm.inf" ] || {
    echo "NetKVM ARM64 driver did not extract -- the guest would have no network." >&2
    exit 1
}
echo "    drivers: $(ls "$iso/Drivers" | tr '\n' ' ')"
# autounattend.xml is read from the ROOT of any attached media by Windows Setup.
cp "$here/autounattend.xml" "$iso/autounattend.xml"
# provision.ps1 is referenced as C:\provision.ps1 by FirstLogonCommands, so it
# has to be ON the system drive by then. Setup copies $OEM$\$1\ to C:\.
mkdir -p "$iso/sources/\$OEM\$/\$1"
cp "$here/provision.ps1" "$iso/sources/\$OEM\$/\$1/provision.ps1"

echo ">>> [5/5] xorriso (UEFI only -- ARM64 has no BIOS boot path)"
# efisys_NOPROMPT, not efisys: the ordinary image stops at "Press any key to
# boot from CD", which on an unattended build means the VM sits at a prompt
# forever and the lane times out instead of installing.
efi="efi/microsoft/boot/efisys_noprompt.bin"
[ -f "$iso/$efi" ] || efi="efi/microsoft/boot/efisys.bin"
# cdrtools mkisofs spells EFI El Torito differently and rejected -e; xorriso
# takes the genisoimage spelling and is what every current recipe assumes.
xorriso -as mkisofs -quiet \
    -iso-level 3 -J -joliet-long -rational-rock \
    -volid "WIN11ARM64" \
    -e "$efi" -no-emul-boot \
    -o "$OUT" "$iso"

echo
echo "ISO: $OUT  ($(du -h "$OUT" | cut -f1))"
echo "It installs Windows 11 Pro ARM64 unattended, creates the 'tycho' admin"
echo "account, enables OpenSSH with the host key, and drops C:\\provisioned."
