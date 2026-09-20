set -eu
# The windows-arm64 build agent: create, install, start, stop, destroy.
#
# QEMU directly rather than through UTM. UTM is a GUI wrapper around this same
# QEMU (it ships the binaries and the edk2 firmware), and `utmctl` can start and
# stop a VM but cannot CREATE one -- creation is a dialog. A platform gate that
# needs somebody to click through a wizard is the hand-run measurement this
# whole matrix exists to replace, so the VM is defined here instead. UTM can
# still open the resulting qcow2 if you want a console.
#
# HVF, not TCG: the guest is aarch64 on an aarch64 host, so it runs at native
# speed. An x86_64 Windows guest here would be TCG emulation and take hours per
# suite run, which is why windows-x86_64 belongs on the x86_64 Linux box under
# KVM and not on this Mac.
#
#   sh vm.sh install   boot the unattended ISO and install Windows (~30-60 min)
#   sh vm.sh start     boot the installed disk, headless
#   sh vm.sh stop      power off
#   sh vm.sh ssh       ssh into the guest
#   sh vm.sh status    is it up, is it provisioned
#   sh vm.sh console   boot WITH A WINDOW, drivers CD attached (one-time setup)
#   sh vm.sh reprovision  re-run provision.ps1 from SOURCE on a live guest
#   sh vm.sh destroy   delete the disk

here="$(cd "$(dirname "$0")" && pwd)"
VMDIR="${TYCHO_WINVM_DIR:-$HOME/vm/win11arm}"
DISK="$VMDIR/win11arm64.qcow2"
NVRAM="$VMDIR/efi_vars.fd"
ISO="$VMDIR/win11arm64-unattend.iso"
TOOLS="$VMDIR/utm-guest-tools.iso"
PIDFILE="$VMDIR/qemu.pid"
MONITOR="$VMDIR/monitor.sock"
SSHPORT="${TYCHO_WINVM_SSHPORT:-2222}"
DISKSIZE="${TYCHO_WINVM_DISK:-64G}"
MEM="${TYCHO_WINVM_MEM:-8192}"
CPUS="${TYCHO_WINVM_CPUS:-4}"

# edk2 from UTM if present, else from brew's qemu share dir. ARM64 has no BIOS
# path at all, so without firmware the guest cannot boot -- fail loudly rather
# than hand qemu a missing file and let it print something opaque.
find_fw() {
    for c in /Applications/UTM.app/Contents/Resources/qemu/edk2-aarch64-code.fd \
             /opt/homebrew/share/qemu/edk2-aarch64-code.fd; do
        [ -f "$c" ] && { echo "$c"; return; }
    done
    echo "no edk2-aarch64-code.fd (install UTM or qemu)" >&2; exit 2
}
FW="$(find_fw)"

qemu_common() {
    # -M virt with HVF, a TPM-less config (setup's checks are bypassed in the
    # answer file), and user-mode networking with ONE forwarded port: ssh. No
    # bridge, no host services exposed -- the guest is a build agent, not a
    # host on the network.
    set -- \
        -machine virt,highmem=on \
        -accel hvf -cpu host -smp "$CPUS" -m "$MEM" \
        -drive "if=pflash,format=raw,readonly=on,file=$FW" \
        -drive "if=pflash,format=raw,file=$NVRAM" \
        -device qemu-xhci -device usb-kbd -device usb-tablet \
        -device virtio-gpu-pci -device ramfb \
        -device virtio-net-pci,netdev=n0 \
        -netdev "user,id=n0,hostfwd=tcp::$SSHPORT-:22" \
        -drive "if=none,id=hd,file=$DISK,format=qcow2,cache=writeback" \
        -device nvme,drive=hd,serial=tycho \
        -rtc base=utc \
        -monitor "unix:$MONITOR,server,nowait"
    printf '%s\n' "$@"
}

need_disk() { [ -f "$DISK" ] || { echo "no disk -- run: sh vm.sh install" >&2; exit 2; }; }

case "${1:-}" in
install)
    [ -f "$ISO" ] || { echo "no ISO at $ISO -- run build_iso.sh first" >&2; exit 2; }
    mkdir -p "$VMDIR"
    [ -f "$DISK" ] && { echo "disk already exists: $DISK (vm.sh destroy first)" >&2; exit 2; }
    qemu-img create -f qcow2 "$DISK" "$DISKSIZE" >/dev/null
    # NVRAM must be exactly 64MiB and is per-VM: it holds the boot entries
    # Windows Setup writes, so a shared or truncated one boots to the shell.
    dd if=/dev/zero of="$NVRAM" bs=1m count=64 2>/dev/null
    echo "installing Windows 11 ARM64, unattended. 30-60 min; no input needed."
    echo "  disk:    $DISK ($DISKSIZE)"
    echo "  ssh:     localhost:$SSHPORT once provisioning finishes"
    # shellcheck disable=SC2046
    qemu-system-aarch64 $(qemu_common) \
        -drive "if=none,id=cd,file=$ISO,media=cdrom,readonly=on" \
        -device usb-storage,drive=cd,bootindex=0 \
        -display none -serial null \
        -pidfile "$PIDFILE" -daemonize
    echo "started (pid $(cat "$PIDFILE")). Watch with: sh vm.sh status"
    ;;
start)
    need_disk
    [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null && { echo "already running"; exit 0; }
    # shellcheck disable=SC2046
    qemu-system-aarch64 $(qemu_common) -display none -serial null \
        -pidfile "$PIDFILE" -daemonize
    echo "started (pid $(cat "$PIDFILE")), ssh on localhost:$SSHPORT"
    ;;
stop)
    [ -f "$PIDFILE" ] || { echo "not running"; exit 0; }
    printf 'system_powerdown\n' | nc -U "$MONITOR" >/dev/null 2>&1 || true
    sleep 5
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    rm -f "$PIDFILE"
    echo "stopped"
    ;;
ssh)
    shift
    exec ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
         -o LogLevel=ERROR -p "$SSHPORT" tycho@localhost "$@"
    ;;
status)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "qemu:        running (pid $(cat "$PIDFILE"))"
    else
        echo "qemu:        not running"; exit 0
    fi
    if ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
           -o LogLevel=ERROR -o BatchMode=yes -o ConnectTimeout=5 \
           -p "$SSHPORT" tycho@localhost 'exit' 2>/dev/null; then
        echo "ssh:         up"
        if ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
               -o LogLevel=ERROR -o BatchMode=yes -p "$SSHPORT" tycho@localhost \
               'test -f /c/provisioned' 2>/dev/null; then
            echo "provisioned: yes -- ready to be a platform runner"
        else
            echo "provisioned: NOT YET (toolchain still installing)"
        fi
    else
        echo "ssh:         not up yet (Windows still installing, or still booting)"
    fi
    ;;
console)
    # ONE-TIME BOOTSTRAP, and the only step here a human touches.
    #
    # Windows on ARM verifies driver signatures, and the ARM64 virtio drivers
    # exist only in UTM's guest-tools ISO, which is not WHQL-signed the way the
    # x64 virtio-win set is. An unattended DriverPaths injection therefore
    # cannot load them: the publisher-trust prompt has no one to answer it.
    # Two full unattended installs proved that the expensive way -- both
    # finished and both sat unreachable with no NIC.
    #
    # So the NIC gets installed once, by hand, here. Afterwards the VM is
    # headless forever: `vm.sh start` + `vm.sh reprovision` and the matrix
    # drives it over ssh like every other runner.
    #
    # ramfb ONLY, no virtio-gpu: with no display driver loaded Windows paints
    # on the UEFI GOP framebuffer, which is what ramfb exposes. Leaving
    # virtio-gpu attached makes it head 0 and the window shows an unclaimed
    # device -- which is why every screendump so far was blank.
    need_disk
    [ -f "$TOOLS" ] || { echo "no guest-tools ISO at $TOOLS" >&2; exit 2; }
    [ -f "$PIDFILE" ] && { kill "$(cat "$PIDFILE")" 2>/dev/null || true; rm -f "$PIDFILE"; sleep 2; }
    echo "opening a window. In the guest:"
    echo "  1. Device Manager -> the NIC with a warning triangle"
    echo "  2. Update driver -> Browse -> the CD -> Drivers\\NetKVM\\w11\\ARM64"
    echo "  3. accept the 'install this driver anyway / trust publisher' prompt"
    echo "  4. close the window when the NIC shows a network"
    echo "then: sh vm.sh start && sh vm.sh reprovision"
    qemu-system-aarch64 \
        -machine virt,highmem=on -accel hvf -cpu host -smp "$CPUS" -m "$MEM" \
        -drive "if=pflash,format=raw,readonly=on,file=$FW" \
        -drive "if=pflash,format=raw,file=$NVRAM" \
        -device qemu-xhci -device usb-kbd -device usb-tablet \
        -device ramfb \
        -device virtio-net-pci,netdev=n0 \
        -netdev "user,id=n0,hostfwd=tcp::$SSHPORT-:22" \
        -drive "if=none,id=hd,file=$DISK,format=qcow2,cache=writeback" \
        -device nvme,drive=hd,serial=tycho \
        -drive "if=none,id=tools,file=$TOOLS,media=cdrom,readonly=on" \
        -device usb-storage,drive=tools \
        -rtc base=utc -monitor "unix:$MONITOR,server,nowait" \
        -display cocoa
    ;;
reprovision)
    # The ISO carries whatever provision.ps1 looked like when it was built, and
    # rebuilding a 6GB ISO to change one line is not a workflow. This pushes the
    # CURRENT script and runs it, so the guest's setup tracks the repo instead
    # of the day the image was made. Safe to repeat: the script is idempotent.
    need_disk
    [ -f "$here/provision.ps1" ] || { echo "no provision.ps1" >&2; exit 2; }
    sh "$0" ssh 'cat > /c/provision.ps1' < "$here/provision.ps1"
    sh "$0" ssh 'powershell -ExecutionPolicy Bypass -NoProfile -File C:\\provision.ps1'
    sh "$0" ssh 'test -f /c/provisioned' \
        && echo "reprovisioned: toolchain verified" \
        || { echo "reprovision FAILED -- marker withheld; see /c/provision.log" >&2; exit 1; }
    ;;
destroy)
    [ -f "$PIDFILE" ] && { kill "$(cat "$PIDFILE")" 2>/dev/null || true; rm -f "$PIDFILE"; }
    rm -f "$DISK" "$NVRAM"
    echo "destroyed $DISK"
    ;;
*)
    sed -n '1,30p' "$0" | grep -E '^#   ' | sed 's/^#   //'
    exit 2 ;;
esac
