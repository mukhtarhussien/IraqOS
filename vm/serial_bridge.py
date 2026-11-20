#!/usr/bin/env python3
"""Forward keyboard bytes to IraqOS and play optional UI sound cues."""
import os
import select
import signal
import socket
import subprocess
import sys
import termios
import tty

SOUND_PREFIX = b"\x1eIRAQOS_SOUND:"
MARKER_END = b"\x1f"


def emit(data):
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


def play_ui_sound(project_dir, name):
    if name not in {"startup", "login", "open", "close", "click"}:
        return
    path = os.path.join(project_dir, "assets", "sounds", name + ".wav")
    try:
        subprocess.Popen(
            ["termux-media-player", "play", path],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError:
        emit(b"\a")


def parse_sounds(data, pending, project_dir):
    pending.extend(data)
    while True:
        start = pending.find(SOUND_PREFIX)
        if start < 0:
            keep = 0
            for size in range(min(len(pending), len(SOUND_PREFIX) - 1), 0, -1):
                if pending.endswith(SOUND_PREFIX[:size]):
                    keep = size
                    break
            if len(pending) > keep:
                emit(pending[:-keep] if keep else pending)
                if keep:
                    del pending[:-keep]
                else:
                    pending.clear()
            return
        if start:
            emit(pending[:start])
            del pending[:start]
        end = pending.find(MARKER_END, len(SOUND_PREFIX))
        if end < 0:
            return
        name = bytes(pending[len(SOUND_PREFIX):end]).decode("ascii", "ignore")
        del pending[:end + len(MARKER_END)]
        play_ui_sound(project_dir, name)


def main():
    if len(sys.argv) != 3:
        print("usage: serial_bridge.py SOCKET_PATH PROJECT_DIR", file=sys.stderr)
        return 2
    socket_path, project_dir = sys.argv[1:]
    old_term = None
    if sys.stdin.isatty():
        old_term = termios.tcgetattr(sys.stdin.fileno())
        tty.setraw(sys.stdin.fileno())

    serial = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    serial.settimeout(0.5)
    try:
        while True:
            try:
                serial.connect(socket_path)
                break
            except (FileNotFoundError, ConnectionRefusedError, socket.timeout):
                select.select([], [], [], 0.1)
    except KeyboardInterrupt:
        if old_term is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, old_term)
        return 0
    serial.settimeout(None)

    pending = bytearray()
    stdin_open = True
    signal.signal(signal.SIGTERM, lambda signum, frame: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        while True:
            readers = [serial]
            if stdin_open:
                readers.append(sys.stdin.fileno())
            ready, _, _ = select.select(readers, [], [])
            if serial in ready:
                block = serial.recv(4096)
                if not block:
                    break
                parse_sounds(block, pending, project_dir)
            if stdin_open and sys.stdin.fileno() in ready:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data:
                    stdin_open = False
                else:
                    serial.sendall(data)
    except (KeyboardInterrupt, BrokenPipeError, ConnectionError, OSError):
        pass
    finally:
        serial.close()
        if old_term is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, old_term)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
