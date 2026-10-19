#!/data/data/com.termux/files/usr/bin/sh
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT="$BASE/IraqOS"
KERNEL="$PROJECT/Bootloader/boot.elf"
DISK="$BASE/vm/disk/iraqos.qcow2"

export DISPLAY="${DISPLAY:-:0}"
"$BASE/build.sh"
PIDFILE="$BASE/vm/iraqos.pid"
VM_PID=
RESIZER_PID=
BRIDGE_PID=
SERIAL_SOCKET="$BASE/vm/iraqos-serial.sock"

stop_previous() {
    [ -r "$PIDFILE" ] || return 0
    old_pid=$(cat "$PIDFILE" 2>/dev/null || true)
    case "$old_pid" in
        ''|*[!0-9]*) rm -f "$PIDFILE"; return 0 ;;
    esac
    if kill -0 "$old_pid" 2>/dev/null; then
        old_cmd=$(tr '\000' ' ' < "/proc/$old_pid/cmdline" 2>/dev/null || true)
        case "$old_cmd" in
            *"$KERNEL"*)
                kill "$old_pid" 2>/dev/null || true
                tries=0
                while kill -0 "$old_pid" 2>/dev/null && [ "$tries" -lt 30 ]; do
                    sleep 0.1
                    tries=$((tries + 1))
                done
                ;;
        esac
    fi
    rm -f "$PIDFILE"
}

cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "$BRIDGE_PID" ]; then
        kill "$BRIDGE_PID" 2>/dev/null || true
        wait "$BRIDGE_PID" 2>/dev/null || true
    fi
    if [ -n "$RESIZER_PID" ]; then
        kill "$RESIZER_PID" 2>/dev/null || true
        wait "$RESIZER_PID" 2>/dev/null || true
    fi
    if [ -n "$VM_PID" ]; then
        kill "$VM_PID" 2>/dev/null || true
        wait "$VM_PID" 2>/dev/null || true
    fi
    if [ -r "$PIDFILE" ] && [ "$(cat "$PIDFILE" 2>/dev/null || true)" = "$VM_PID" ]; then
        rm -f "$PIDFILE"
    fi
}

stop_previous
python -c 'import pathlib,sys; pathlib.Path(sys.argv[1]).unlink(missing_ok=True)' "$SERIAL_SOCKET"
trap cleanup EXIT INT TERM HUP
python "$BASE/vm/serial_bridge.py" "$SERIAL_SOCKET" "$BASE" < /dev/tty &
BRIDGE_PID=$!
qemu-system-aarch64 \
    -name TURTULE-IRAQOS \
    -machine virt \
    -cpu cortex-a57 \
    -m 512M \
    -smp 1 \
    -chardev "socket,id=iraqos-serial,path=$SERIAL_SOCKET,server=on,wait=off" \
    -serial chardev:iraqos-serial \
    -monitor none \
    -global virtio-mmio.force-legacy=false \
    -drive "if=none,id=iraqosdisk,file=$DISK,format=qcow2" \
    -device virtio-blk-device,drive=iraqosdisk \
    -device virtio-gpu-device \
    -device virtio-tablet-device \
    -display sdl,show-cursor=on \
    -full-screen \
    -kernel "$KERNEL" < /dev/tty &
VM_PID=$!
printf '%s\n' "$VM_PID" > "$PIDFILE"
python "$BASE/vm/resize_qemu_window.py" "$VM_PID" &
RESIZER_PID=$!
wait "$VM_PID"
