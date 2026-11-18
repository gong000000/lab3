#!/usr/bin/env python3
"""PTY-based signal test: Ctrl-C (SIGINT) and Ctrl-Z (SIGQUIT mapping)."""
import os
import pty
import select
import sys
import time

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "myshell")


def read_until(fd, needle, timeout=5.0):
    data = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
        if needle in data:
            return data
    raise AssertionError("timeout waiting for %r; got %r" % (needle, data))


def main():
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(BIN, [BIN])

    out = read_until(fd, b"shell> ")
    sys.stdout.write(out.decode(errors="replace"))

    # Normal command.
    os.write(fd, b"echo signal-test\n")
    out = read_until(fd, b"shell> ")
    assert b"signal-test" in out, out

    # Ctrl-C while a foreground child is running: shell must survive.
    os.write(fd, b"sleep 30\n")
    time.sleep(0.3)
    os.write(fd, b"\x03")
    out = read_until(fd, b"shell> ")
    assert b"shell> " in out, out

    # Ctrl-C while cat is blocked on terminal input.
    os.write(fd, b"cat\n")
    time.sleep(0.3)
    os.write(fd, b"\x03")
    out = read_until(fd, b"shell> ")
    assert b"shell> " in out, out

    # Ctrl-Z is remapped to SIGQUIT by myshell: shell must survive.
    os.write(fd, b"sleep 30\n")
    time.sleep(0.3)
    os.write(fd, b"\x1a")
    out = read_until(fd, b"shell> ")
    assert b"shell> " in out, out

    # Ctrl-Z while cat is blocked on terminal input.
    os.write(fd, b"cat\n")
    time.sleep(0.3)
    os.write(fd, b"\x1a")
    out = read_until(fd, b"shell> ")
    assert b"shell> " in out, out

    # Ctrl-C at the prompt must not terminate the shell.
    os.write(fd, b"\x03")
    out = read_until(fd, b"shell> ")
    assert b"shell> " in out, out

    os.write(fd, b"exit 0\n")
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.5)
            if not r:
                break
            chunk = os.read(fd, 4096)
            if not chunk:
                break
    except OSError:
        pass

    _, status = os.waitpid(pid, 0)
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, status
    print("\nSIGNAL TEST PASSED")


if __name__ == "__main__":
    main()
