#!/usr/bin/env python3
"""Connect IraqOS's serial terminal to a persistent, real Termux shell."""
import os
import pty
import select
import shutil
import signal
import socket
import subprocess
import sys
import termios
import tty

EXEC_PREFIX = b"\x1eIRAQOS_EXEC:"
SOUND_PREFIX = b"\x1eIRAQOS_SOUND:"
EXEC_END = b"\x1f"
DONE_MARKER = b"\x1eIRAQOS_DONE_4c7a\x1f"
RESULT_PREFIX = b"\x1eIRAQOS_RESULT:"
ACK = b"\x06"
MAX_OUTPUT = 8192


def emit(data):
    if not data:
        return
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def play_ui_sound(project_dir, name):
    if name not in {"startup", "login", "open", "close", "click"}:
        return
    sound_path = os.path.join(project_dir, "assets", "sounds", name + ".wav")
    player = shutil.which("termux-media-player")
    if player and os.path.isfile(sound_path):
        try:
            subprocess.Popen(
                [player, "play", sound_path],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            return
        except OSError:
            pass
    # Termux's terminal bell is a lightweight fallback when its audio API is absent.
    emit(b"\a")


class LinuxShell:
    def __init__(self, cwd):
        self.cwd = cwd
        self.process = None
        self.master = None

    def start(self):
        shell = shutil.which("bash") or shutil.which("sh")
        if not shell:
            return False
        self.master, slave = pty.openpty()
        attrs = termios.tcgetattr(slave)
        attrs[3] &= ~(termios.ECHO | termios.ECHONL)
        termios.tcsetattr(slave, termios.TCSANOW, attrs)
        loop = (
            'while IFS= read -r __iraqos_line; do '
            'eval "$__iraqos_line"; '
            "printf '\\036IRAQOS_DONE_4c7a\\037'; "
            'done'
        )
        env = os.environ.copy()
        env["TERM"] = "dumb"
        env["NO_COLOR"] = "1"
        env["HISTFILE"] = "/dev/null"
        try:
            if os.path.basename(shell) == "bash":
                args = [shell, "--noprofile", "--norc", "-c", loop]
            else:
                args = [shell, "-c", loop]
            self.process = subprocess.Popen(
                args,
                stdin=slave,
                stdout=slave,
                stderr=slave,
                cwd=self.cwd,
                env=env,
                close_fds=True,
                start_new_session=True,
            )
        except OSError:
            os.close(self.master)
            self.master = None
            self.process = None
            os.close(slave)
            return False
        os.close(slave)
        return True

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process = None
        if self.master is not None:
            os.close(self.master)
            self.master = None

    def run(self, command):
        if not self.process or self.process.poll() is not None:
            self.stop()
            if not self.start():
                return b"No Termux shell found (install bash or sh).\n"
        try:
            os.write(self.master, command.encode("utf-8", "replace") + b"\n")
        except OSError:
            self.stop()
            return b"Could not send command to the Termux shell.\n"

        output = bytearray()
        pending = bytearray()
        while True:
            ready, _, _ = select.select([self.master], [], [], 0.25)
            if not ready:
                if self.process.poll() is not None:
                    self.stop()
                    return bytes(output) + b"\n[Termux shell exited]\n"
                continue
            try:
                block = os.read(self.master, 4096)
            except OSError:
                block = b""
            if not block:
                self.stop()
                return bytes(output) + b"\n[Termux shell exited]\n"
            pending.extend(block)
            marker_at = pending.find(DONE_MARKER)
            if marker_at >= 0:
                output.extend(pending[:marker_at])
                break
            keep = 0
            max_keep = min(len(pending), len(DONE_MARKER) - 1)
            for size in range(max_keep, 0, -1):
                if pending.endswith(DONE_MARKER[:size]):
                    keep = size
                    break
            if len(pending) > keep:
                output.extend(pending[:-keep] if keep else pending)
                if keep:
                    del pending[:-keep]
                else:
                    pending.clear()
            if len(output) > MAX_OUTPUT:
                del output[MAX_OUTPUT:]

        if len(output) > MAX_OUTPUT:
            del output[MAX_OUTPUT:]
        return bytes(output)


def printable_output(data):
    out = bytearray()
    in_escape = False
    for value in data:
        if in_escape:
            if 0x40 <= value <= 0x7e:
                in_escape = False
            continue
        if value == 0x1b:
            in_escape = True
        elif value in (9, 10, 13) or 32 <= value <= 126:
            out.append(value)
        elif value >= 128:
            out.append(ord('?'))
    truncation = b"\n[output truncated]\n"
    if len(out) > MAX_OUTPUT:
        del out[MAX_OUTPUT - len(truncation):]
        out.extend(truncation)
    return bytes(out)


def send_result(sock, shell, command):
    payload = printable_output(shell.run(command))
    header = RESULT_PREFIX + str(len(payload)).encode("ascii") + b"\n"
    sock.sendall(header)
    for offset in range(0, len(payload), 16):
        sock.sendall(payload[offset:offset + 16])
        while True:
            ack = sock.recv(1)
            if not ack:
                raise ConnectionError("QEMU serial socket closed")
            if ack == ACK:
                break


def parse_serial(sock, data, pending, shell, project_dir):
    pending.extend(data)
    markers = (EXEC_PREFIX, SOUND_PREFIX)
    while True:
        found = [(pending.find(marker), marker) for marker in markers]
        found = [(position, marker) for position, marker in found if position >= 0]
        if not found:
            keep = 0
            for marker in markers:
                max_keep = min(len(pending), len(marker) - 1)
                for size in range(max_keep, 0, -1):
                    if pending.endswith(marker[:size]):
                        keep = max(keep, size)
                        break
            if len(pending) > keep:
                emit(pending[:-keep] if keep else pending)
                if keep:
                    del pending[:-keep]
                else:
                    pending.clear()
            return

        start, marker = min(found, key=lambda item: item[0])
        if start:
            emit(pending[:start])
            del pending[:start]
        end = pending.find(EXEC_END, len(marker))
        if end < 0:
            if len(pending) > 2048:
                emit(pending)
                pending.clear()
            return

        payload = bytes(pending[len(marker):end])
        del pending[:end + len(EXEC_END)]
        if marker == SOUND_PREFIX:
            play_ui_sound(project_dir, payload.decode("ascii", "ignore"))
            continue
        command = payload.decode("utf-8", "replace")
        try:
            send_result(sock, shell, command)
        except (BrokenPipeError, ConnectionError, OSError):
            return


def main():
    if len(sys.argv) != 3:
        print("usage: serial_bridge.py SOCKET_PATH PROJECT_DIR", file=sys.stderr)
        return 2
    socket_path, project_dir = sys.argv[1:]
    def stop_on_signal(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop_on_signal)
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

    shell = LinuxShell(project_dir)
    pending = bytearray()
    stdin_open = True
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
                parse_serial(serial, block, pending, shell, project_dir)
            if stdin_open and sys.stdin.fileno() in ready:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data:
                    stdin_open = False
                else:
                    serial.sendall(data)
    except (KeyboardInterrupt, BrokenPipeError, ConnectionError, OSError):
        pass
    finally:
        shell.stop()
        serial.close()
        if old_term is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, old_term)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
