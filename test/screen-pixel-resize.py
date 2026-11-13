#!/usr/bin/env python3
# Copyright (c) 2026 Hiroki Sato <hrs@allbsd.org>

"""Check a real GNU screen's pixel-only SIGWINCH forwarding.

Run with SCREEN=/path/to/patched/screen python3 test/screen-pixel-resize.py.
Only an isolated test session and its PTYs are used.
"""
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


def winsize(fd):
    return list(struct.unpack('HHHH', fcntl.ioctl(fd, termios.TIOCGWINSZ, b'\0' * 8)))


if len(sys.argv) == 3 and sys.argv[1] == '--child':
    target = Path(sys.argv[2])
    records = []

    def record(*args):
        records.append(winsize(0))
        target.write_text(json.dumps(records))

    signal.signal(signal.SIGWINCH, record)
    record()
    while True:
        signal.pause()

screen = os.environ.get('SCREEN', 'screen')
session = 'xsixel-pixel-test-' + str(os.getpid())
with tempfile.TemporaryDirectory(prefix='xsixel-screen-pixels-') as tmp:
    target = Path(tmp) / 'sizes.json'
    pid, master = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm'
        os.environ.pop('STY', None)
        os.environ.pop('TMUX', None)
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 724, 524))
        os.execlp(screen, screen, '-c', '/dev/null', '-S', session,
                  sys.executable, str(Path(__file__).resolve()), '--child', str(target))

    def wait_for(expected):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                os.read(master, 65536)
            try:
                records = json.loads(target.read_text())
                if records[-1] == expected:
                    return records
            except (FileNotFoundError, json.JSONDecodeError):
                pass
        raise AssertionError(f'expected {expected}; recorded {target.read_text() if target.exists() else "nothing"}')

    try:
        wait_for([40, 120, 724, 524])
        for size in ([40, 120, 844, 524], [40, 120, 844, 644],
                     [40, 120, 724, 524], [45, 130, 784, 589]):
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', *size))
            records = wait_for(size)
        def command(*args):
            subprocess.run([screen, '-S', session, '-X', *args], check=True, timeout=5)

        command('screen', '-t', 'other', 'sleep', '60')
        command('select', '1')
        time.sleep(.2)
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 45, 130, 914, 589))
        time.sleep(.2)
        command('select', '0')
        records = wait_for([45, 130, 914, 589])
        print('PASS: width-only, height-only, restore, grid resize, and hidden-window return:', records)
    finally:
        subprocess.run([screen, '-S', session, '-X', 'quit'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
        os.close(master)
        os.waitpid(pid, 0)
