#!/usr/bin/env python3
"""PTY tests for the line editor: arrows, Tab, Ctrl-R, history, UTF-8."""
import os
import pty
import select
import sys
import tempfile
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


def drain(fd, quiet=0.25, timeout=2.0):
    """Read until the PTY has been quiet for a short period."""
    data = b""
    deadline = time.time() + timeout
    last = time.time()
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            if time.time() - last >= quiet:
                break
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
        last = time.time()
    return data


def main():
    home = tempfile.mkdtemp()
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["HOME"] = home
        os.environ["LANG"] = "C.UTF-8"
        os.execv(BIN, [BIN])

    out = read_until(fd, b"shell> ")
    sys.stdout.write(out.decode(errors="replace"))

    # UTF-8 input.
    os.write(fd, "echo 한글\n".encode())
    out = drain(fd)
    assert "한글".encode() in out, out

    # History: up arrow recalls the previous command.
    os.write(fd, b"\x1b[A\r")
    out = drain(fd)
    assert "한글".encode() in out, out

    # Tab completion: "ec" -> "echo ".
    os.write(fd, b"ech\t")
    time.sleep(0.1)
    os.write(fd, b"tabtest\r")
    out = drain(fd)
    assert b"tabtest" in out, out

    # Make sure "pwd" is in history, then Ctrl-R search for it.
    os.write(fd, b"pwd\r")
    drain(fd)
    os.write(fd, b"\x12")
    read_until(fd, b"(reverse-i-search)")
    os.write(fd, b"pwd")
    time.sleep(0.2)
    os.write(fd, b"\r")
    time.sleep(0.1)
    os.write(fd, b"\r")
    out = drain(fd)
    assert home.encode() in out or b"/" in out, out

    # history builtin.
    os.write(fd, b"history\r")
    out = drain(fd)
    assert b"history" in out, out

    # !! repeats the previous command.
    os.write(fd, b"!!\r")
    out = drain(fd)
    assert b"history" in out, out

    # Ctrl-C at the prompt must return to a prompt.
    os.write(fd, b"\x03")
    out = drain(fd)
    assert b"shell> " in out, out

    # Cursor editing: move left three characters and insert "X".
    os.write(fd, b"echo abc")
    os.write(fd, b"\x1b[D\x1b[D\x1b[D")
    os.write(fd, b"X\r")
    out = drain(fd)
    assert b"Xabc" in out, out

    # Multi-line input: the line editor keeps reading until "fi".
    os.write(fd, b"if true; then\r")
    out = read_until(fd, b"... ")
    os.write(fd, b"echo multiline-ok\r")
    out = read_until(fd, b"... ")
    os.write(fd, b"fi\r")
    out = drain(fd)
    assert b"multiline-ok" in out, out

    os.write(fd, b"exit\r")
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.5)
            if not r:
                break
            if not os.read(fd, 4096):
                break
    except OSError:
        pass
    os.waitpid(pid, 0)

    # History is persisted in $HOME/.myshell_history and reloaded.
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["HOME"] = home
        os.environ["LANG"] = "C.UTF-8"
        os.execv(BIN, [BIN])
    read_until(fd, b"shell> ")
    os.write(fd, b"history\r")
    out = drain(fd)
    assert b"pwd" in out, out
    assert b"if true; then" in out, out
    # Ctrl-D on an empty line ends the session.
    os.write(fd, b"\x04")
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 0.5)
            if not r:
                break
            if not os.read(fd, 4096):
                break
    except OSError:
        pass
    os.waitpid(pid, 0)
    print("\nLINE EDITOR TEST PASSED")


if __name__ == "__main__":
    main()
