set -eu
# The windows-x86_64 build agent, on the x86_64 Linux box under KVM.
#
# KVM here, not emulation on the Mac: an x86_64 Windows guest on Apple Silicon
# is TCG, which means hours per suite run. This box IS x86_64, so the guest is
# native speed and the lane measures real x86_64 Windows rather than an
# emulator -- the same reason the matrix refuses to count wine.
#
# Fully unattended, unlike the arm64 side: the amd64 virtio drivers are
# WHQL-signed, so Setup loads them with no publisher-trust prompt.
#
# BOOT ORDER: DISK FIRST (bootindex=0), CD SECOND. An empty disk has no
# bootloader so firmware falls through to the CD and the install starts; once
# Setup writes a bootloader the disk wins and the reboot continues into OOBE.
# Pinning the CD to bootindex=0 instead makes every reboot re-enter Setup,
# which stops on "It looks like you started an upgrade and booted from
# installation media" and waits forever -- 10.8 GB of copied files thrown away
# because the second boot went to the wrong device.
#
# STORAGE IS NVMe, NOT virtio-blk. Windows has an inbox NVMe driver and no
# inbox viostor, so a virtio disk is invisible to Setup unless viostor is
# injected alongside NetKVM -- which it was not, and the install sat on
# "Select location to install Windows" with an empty disk list while the
# qcow2 stayed at 196 KB. NVMe removes the dependency rather than adding a
# second driver to keep in sync. The NIC still needs NetKVM, which IS
# injected, because there is no inbox virtio-net either.
#
#   install | start | stop | status | ssh | destroy

VMDIR="${VMDIR:-$HOME/vm/win11x64}"
DISK="$VMDIR/win11x64.qcow2"
NVRAM="$VMDIR/efi_vars.fd"
ISO="$VMDIR/win11x64-unattend.iso"
PIDFILE="$VMDIR/qemu.pid"
MONITOR="$VMDIR/monitor.sock"
SSHPORT="${TYCHO_WINVM_SSHPORT:-2223}"
MEM="${TYCHO_WINVM_MEM:-8192}"
CPUS="${TYCHO_WINVM_CPUS:-8}"

fw() {
    for c in /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE.fd \
             /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
        [ -f "$c" ] && { echo "$c"; return; }
    done
    echo ""    # no UEFI firmware: fall back to BIOS boot, which x64 still has
}
fwvars() {
    for c in /usr/share/edk2/ovmf/OVMF_VARS.fd /usr/share/OVMF/OVMF_VARS.fd \
             /usr/share/edk2-ovmf/x64/OVMF_VARS.fd; do
        [ -f "$c" ] && { echo "$c"; return; }
    done
    echo ""
}

common() {
    set -- -machine q35,accel=kvm -cpu host -smp "$CPUS" -m "$MEM" \
        -device virtio-net-pci,netdev=n0 \
        -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$SSHPORT-:22" \
        -drive "if=none,id=hd,file=$DISK,format=qcow2,cache=writeback" \
        -device nvme,drive=hd,serial=tycho,bootindex=0 \
        -device virtio-rng-pci \
        -rtc base=utc -monitor "unix:$MONITOR,server,nowait" \
        -display none -serial null
    F="$(fw)"
    if [ -n "$F" ]; then
        set -- "$@" -drive "if=pflash,format=raw,readonly=on,file=$F" \
                    -drive "if=pflash,format=raw,file=$NVRAM"
    fi
    printf '%s\n' "$@"
}

case "${1:-}" in
install)
    [ -f "$ISO" ] || { echo "no ISO at $ISO -- run build_iso.sh" >&2; exit 2; }
    [ -f "$DISK" ] && { echo "disk exists (destroy first)" >&2; exit 2; }
    mkdir -p "$VMDIR"
    qemu-img create -f qcow2 "$DISK" 64G >/dev/null
    V="$(fwvars)"; [ -n "$V" ] && cp "$V" "$NVRAM" || true
    # shellcheck disable=SC2046
    qemu-system-x86_64 $(common) \
        -drive "if=none,id=cd,file=$ISO,media=cdrom,readonly=on" \
        -device ide-cd,drive=cd,bootindex=1 \
        -pidfile "$PIDFILE" -daemonize
    echo "installing (pid $(cat "$PIDFILE")); unattended, ssh on 127.0.0.1:$SSHPORT"
    ;;
start)
    [ -f "$DISK" ] || { echo "no disk" >&2; exit 2; }
    [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null && { echo running; exit 0; }
    # shellcheck disable=SC2046
    qemu-system-x86_64 $(common) -pidfile "$PIDFILE" -daemonize
    echo "started (pid $(cat "$PIDFILE"))"
    ;;
stop)
    [ -f "$PIDFILE" ] || { echo "not running"; exit 0; }
    printf 'system_powerdown\n' | nc -U "$MONITOR" >/dev/null 2>&1 || true
    sleep 5; kill "$(cat "$PIDFILE")" 2>/dev/null || true; rm -f "$PIDFILE"; echo stopped ;;
ssh) shift; exec ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -p "$SSHPORT" tycho@127.0.0.1 "$@" ;;
status)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "qemu: running (pid $(cat "$PIDFILE"))"
    else echo "qemu: not running"; exit 0; fi
    if ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
           -o BatchMode=yes -o ConnectTimeout=5 -p "$SSHPORT" tycho@127.0.0.1 'echo ok' >/dev/null 2>&1
    then echo "ssh:  up"; else echo "ssh:  not up yet"; fi ;;
destroy)
    [ -f "$PIDFILE" ] && { kill "$(cat "$PIDFILE")" 2>/dev/null || true; rm -f "$PIDFILE"; }
    rm -f "$DISK" "$NVRAM"; echo "destroyed" ;;
*) echo "usage: vm.sh install|start|stop|status|ssh|destroy" >&2; exit 2 ;;
esac
