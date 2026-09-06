#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
"""Exercise the real TUI in a PTY without opening a mining device or pool."""
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time

for action in ("q", "interrupt"):
    master, slave = pty.openpty()
    original = termios.tcgetattr(slave)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    process = subprocess.Popen(
        [sys.argv[1], "--tui-demo", "--seconds", "5"],
        stdin=slave, stdout=slave, stderr=slave,
        env=dict(os.environ, TERM="xterm-256color"),
    )
    output = b""
    started = time.monotonic()
    sent = False
    try:
        while time.monotonic() - started < 8:
            if select.select([master], [], [], .05)[0]:
                output += os.read(master, 65536)
            if not sent and time.monotonic() - started > .7:
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 10, 40, 0, 0))
                if action == "q":
                    os.write(master, b"jkq")
                else:
                    process.send_signal(signal.SIGINT)
                sent = True
            if process.poll() is not None:
                while select.select([master], [], [], 0)[0]:
                    output += os.read(master, 65536)
                break
        assert process.poll() == 0, (action, process.poll())
        assert b"\x1b[?1049h" in output and b"\x1b[?1049l" in output
        assert b"\x1b[?25h" in output
        assert termios.tcgetattr(slave) == original
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        os.close(master)
        os.close(slave)
print("PASS TUI PTY resize, keyboard quit, SIGINT and terminal restoration")
