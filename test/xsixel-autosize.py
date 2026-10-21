#!/usr/bin/env python3
"""PTY integration tests. Run with XSIXEL=/path/to/Xsixel python3 test/xsixel-autosize.py.

Requires libX11, libXrandr, xdpyinfo and xsetroot. No real terminal is modified.
"""
import ctypes as C
import ctypes.util
import fcntl
import os
from pathlib import Path
import pty
import re
import resource
import select
import signal
import shutil
import struct
import subprocess
import tempfile
import termios
import threading
import time
import unittest


class ScreenChange(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", C.c_ulong), ("root", C.c_ulong),
                ("timestamp", C.c_ulong), ("config_timestamp", C.c_ulong),
                ("size_index", C.c_ushort), ("subpixel_order", C.c_ushort),
                ("rotation", C.c_ushort), ("width", C.c_int), ("height", C.c_int),
                ("mwidth", C.c_int), ("mheight", C.c_int)]


class KeyEvent(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", C.c_ulong), ("root", C.c_ulong),
                ("subwindow", C.c_ulong), ("time", C.c_ulong),
                ("x", C.c_int), ("y", C.c_int), ("x_root", C.c_int), ("y_root", C.c_int),
                ("state", C.c_uint), ("keycode", C.c_uint), ("same_screen", C.c_int)]


def library(name, functions):
    lib = C.CDLL(ctypes.util.find_library(name))
    for func, result, args in functions:
        getattr(lib, func).restype = result
        getattr(lib, func).argtypes = args
    return lib


class Server:
    def __init__(self, pixels=(0, 0), cells=(80, 30), args=(), memory_limit=False, env=None):
        self.closed = False
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ,
                    struct.pack('HHHH', cells[1], cells[0], *pixels))
        self.output = bytearray()
        self.log = tempfile.TemporaryFile()
        self.reader = threading.Thread(target=self.drain, daemon=True)
        self.reader.start()
        rfd, wfd = os.pipe()
        def limit_memory():
            resource.setrlimit(resource.RLIMIT_AS, (256 * 1024**2, 256 * 1024**2))
        self.process = subprocess.Popen(
            [os.environ['XSIXEL'], '-displayfd', str(wfd), '-terminate', '-nolisten', 'tcp', '-ac', *args],
            stdin=self.slave, stdout=self.slave, stderr=self.log, pass_fds=(wfd,),
            start_new_session=True, preexec_fn=limit_memory if memory_limit else None,
            env=env if env is not None else {k: v for k, v in os.environ.items()
                                             if k not in ("TMUX", "TMUX_PANE", "STY", "TERM_PROGRAM", "LC_TERMINAL")})
        os.close(wfd)
        self.display = None
        try:
            if not select.select([rfd], [], [], 10)[0]:
                raise RuntimeError('Xsixel startup timed out')
            number = os.read(rfd, 64).decode().strip()
            if not number:
                raise RuntimeError('Xsixel exited before readiness')
            self.name = ':' + number
            self.x11 = library('X11', [
                ('XOpenDisplay', C.c_void_p, [C.c_char_p]),
                ('XCreateSimpleWindow', C.c_ulong, [C.c_void_p, C.c_ulong, C.c_int, C.c_int,
                                                    C.c_uint, C.c_uint, C.c_uint, C.c_ulong, C.c_ulong]),
                ('XClearWindow', C.c_int, [C.c_void_p, C.c_ulong]),
                ('XMapWindow', C.c_int, [C.c_void_p, C.c_ulong]),
                ('XSelectInput', C.c_int, [C.c_void_p, C.c_ulong, C.c_long]),
                ('XSetInputFocus', C.c_int, [C.c_void_p, C.c_ulong, C.c_int, C.c_ulong]),
                ('XLookupString', C.c_int, [C.POINTER(KeyEvent), C.c_void_p, C.c_int,
                                          C.POINTER(C.c_ulong), C.c_void_p]),
                ('XDefaultRootWindow', C.c_ulong, [C.c_void_p]),
                ('XCloseDisplay', C.c_int, [C.c_void_p]),
                ('XSync', C.c_int, [C.c_void_p, C.c_int]),
                ('XPending', C.c_int, [C.c_void_p]),
                ('XNextEvent', C.c_int, [C.c_void_p, C.c_void_p]),
                ('XGetImage', C.c_void_p, [C.c_void_p, C.c_ulong, C.c_int, C.c_int,
                                          C.c_uint, C.c_uint, C.c_ulong, C.c_int]),
                ('XGetPixel', C.c_ulong, [C.c_void_p, C.c_int, C.c_int]),
                ('XDestroyImage', C.c_int, [C.c_void_p]),
                ('XWarpPointer', C.c_int, [C.c_void_p, C.c_ulong, C.c_ulong,
                                          C.c_int, C.c_int, C.c_uint, C.c_uint,
                                          C.c_int, C.c_int]),
                ('XQueryPointer', C.c_int, [C.c_void_p, C.c_ulong,
                                           C.POINTER(C.c_ulong), C.POINTER(C.c_ulong),
                                           C.POINTER(C.c_int), C.POINTER(C.c_int),
                                           C.POINTER(C.c_int), C.POINTER(C.c_int),
                                           C.POINTER(C.c_uint)]),
            ])
            self.display = self.x11.XOpenDisplay(self.name.encode())
            if not self.display:
                raise RuntimeError('Cannot connect to Xsixel')
            self.root = self.x11.XDefaultRootWindow(self.display)
            self.randr = library('Xrandr', [
                ('XRRSelectInput', None, [C.c_void_p, C.c_ulong, C.c_int]),
                ('XRRQueryExtension', C.c_int, [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]),
            ])
            event_base, error_base = C.c_int(), C.c_int()
            assert self.randr.XRRQueryExtension(self.display, C.byref(event_base), C.byref(error_base))
            self.event_base = event_base.value
            self.randr.XRRSelectInput(self.display, self.root, 1)
            self.events()  # Discard the initial subscription notification.
        except Exception:
            self.close()
            raise
        finally:
            os.close(rfd)

    def drain(self):
        try:
            while True:
                data = os.read(self.master, 65536)
                if not data:
                    return
                self.output.extend(data)
                if len(self.output) > 1024**2:
                    del self.output[:-1024**2]
        except OSError:
            pass

    def reply(self, data):
        os.write(self.master, data)

    def resize_terminal(self, pixels, cells=(80, 30)):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ,
                    struct.pack('HHHH', cells[1], cells[0], *pixels))
        # The test PTY is not the child's controlling terminal, so explicitly
        # deliver the signal a real terminal sends to its foreground process.
        os.kill(self.process.pid, signal.SIGWINCH)

    def wait_output(self, needle, start=0):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if needle in self.output[start:]:
                return
            time.sleep(0.01)
        raise AssertionError(f'Terminal query not received: {needle!r}')

    def dimensions(self):
        result = subprocess.run(['xdpyinfo', '-display', self.name],
                                capture_output=True, text=True, check=True, timeout=5)
        match = re.search(r'dimensions:\s+(\d+)x(\d+) pixels', result.stdout)
        return tuple(map(int, match.groups()))

    def wait_size(self, width, height):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.dimensions() == (width, height):
                return
            time.sleep(0.02)
        raise AssertionError(f'Expected {(width, height)}, got {self.dimensions()}')

    def events(self):
        self.x11.XSync(self.display, False)
        sizes = []
        while self.x11.XPending(self.display):
            event = (C.c_long * 24)()
            self.x11.XNextEvent(self.display, C.byref(event))
            change = C.cast(C.byref(event), C.POINTER(ScreenChange)).contents
            if change.type == self.event_base:
                sizes.append((change.width, change.height))
        return sizes

    def check_drawing_and_pointer(self, case, width, height):
        subprocess.run(['xsetroot', '-display', self.name, '-solid', 'red'],
                       check=True, timeout=5)
        self.x11.XSync(self.display, False)
        image = self.x11.XGetImage(self.display, self.root, width - 1, height - 1,
                                   1, 1, C.c_ulong(-1).value, 2)
        case.assertTrue(image)
        case.assertNotEqual(self.x11.XGetPixel(image, 0, 0), 0)
        self.x11.XDestroyImage(image)
        self.x11.XWarpPointer(self.display, 0, self.root, 0, 0, 0, 0, width - 1, height - 1)
        root, child, x, y, wx, wy, mask = (C.c_ulong(), C.c_ulong(), C.c_int(), C.c_int(),
                                          C.c_int(), C.c_int(), C.c_uint())
        self.x11.XQueryPointer(self.display, self.root, C.byref(root), C.byref(child),
                               C.byref(x), C.byref(y), C.byref(wx), C.byref(wy), C.byref(mask))
        case.assertEqual((x.value, y.value), (width - 1, height - 1))
        deadline = time.monotonic() + 3
        while b'\x1bP' not in self.output and time.monotonic() < deadline:
            time.sleep(0.02)
        case.assertIn(b'\x1bP', self.output, 'No SIXEL output')
        case.assertIsNone(self.process.poll())

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        os.close(self.slave)
        self.reader.join(timeout=1)
        os.close(self.master)
        self.log.seek(0)
        log = self.log.read().decode(errors='replace')
        self.log.close()
        if self.process.returncode != 0:
            raise AssertionError(f'Xsixel exit {self.process.returncode}: {log}')


class Autosize(unittest.TestCase):
    def server(self, **kwargs):
        server = Server(**kwargs)
        self.addCleanup(server.close)
        return server

    def mouse_events(self, server, reports, expected_buttons):
        server.x11.XSelectInput(server.display, server.root, 4 | 8 | 64)
        server.x11.XSync(server.display, False)
        server.events()
        for report in reports:
            server.reply(report)
            time.sleep(0.02)
        received = []
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            while server.x11.XPending(server.display):
                event = (C.c_long * 24)()
                server.x11.XNextEvent(server.display, C.byref(event))
                mouse = C.cast(C.byref(event), C.POINTER(KeyEvent)).contents
                if mouse.type in (4, 5):
                    received.append((mouse.type, mouse.keycode, mouse.x_root, mouse.y_root))
            if len(received) >= len(expected_buttons):
                break
            time.sleep(0.01)
        self.assertEqual(received, expected_buttons)

    def test_sgr_mouse_click_drag_and_scroll(self):
        server = self.server(pixels=(800, 600))
        server.wait_output(b'\x1b[?1016h\x1b[?1003h')
        self.assertNotIn(b'\x1b[5n', server.output)
        self.mouse_events(server,
            [b'\x1b[<35;121;81M', b'\x1b[<0;201;151M',
             b'\x1b[<32;221;171M', b'\x1b[<0;221;171m',
             b'\x1b[<64;221;171M', b'\x1b[<65;221;171M'],
            [(4, 1, 200, 150), (5, 1, 220, 170),
             (4, 4, 220, 170), (5, 4, 220, 170),
             (4, 5, 220, 170), (5, 5, 220, 170)])

    def test_sgr_mouse_fragmented_and_multiple_buttons(self):
        server = self.server(pixels=(800, 600))
        self.mouse_events(server,
            [b'\x1b[<0;20', b'1;151M', b'\x1b[<2;201;151M',
             b'\x1b[<0;201;151m', b'\x1b[<2;201;151m'],
            [(4, 1, 200, 150), (4, 3, 200, 150),
             (5, 1, 200, 150), (5, 3, 200, 150)])

    def test_dec_mouse_compatibility(self):
        server = self.server(pixels=(800, 600), args=('-mouse-protocol', 'dec'))
        server.wait_output(b"\x1b[1;1'z")
        self.mouse_events(server,
            [b'\x1b[10;0;81;121;0&w', b'\x1b[2;4;151;201;0&w',
             b'\x1b[6;5;151;201;0&w', b'\x1b[3;1;151;201;0&w',
             b'\x1b[7;0;151;201;0&w'],
            [(4, 1, 200, 150), (4, 3, 200, 150),
             (5, 1, 200, 150), (5, 3, 200, 150)])

    def test_terminal_keyboard_reaches_focused_client(self):
        server = self.server(pixels=(800, 600))
        window = server.x11.XCreateSimpleWindow(server.display, server.root, 0, 0,
                                                200, 100, 0, 0, 0)
        server.x11.XSelectInput(server.display, window, 3)
        server.x11.XMapWindow(server.display, window)
        server.x11.XSetInputFocus(server.display, window, 1, 0)
        server.x11.XSync(server.display, False)
        server.events()
        server.reply(b'aA\r\x1b[A')
        received = []
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            while server.x11.XPending(server.display):
                event = (C.c_long * 24)()
                server.x11.XNextEvent(server.display, C.byref(event))
                key = C.cast(C.byref(event), C.POINTER(KeyEvent)).contents
                if key.type == 2:
                    text = C.create_string_buffer(32)
                    sym = C.c_ulong()
                    server.x11.XLookupString(C.byref(key), text, 32, C.byref(sym), None)
                    if sym.value not in (0xffe1, 0xffe3):
                        received.append((sym.value, text.value))
            if len(received) >= 4:
                break
            time.sleep(0.01)
        self.assertEqual(received, [(ord('a'), b'a'), (ord('A'), b'A'),
                                     (0xff0d, b'\r'), (0xff52, b'')])

    def test_display_modes_and_bottom_edge_updates(self):
        server = self.server(pixels=(800, 600))
        server.wait_output(b'\x1b[?80s\x1b[?80h')
        server.wait_output(b'\x1b[>3p')
        server.wait_output(b'\x1bP')
        time.sleep(0.1)
        start = len(server.output)
        window = server.x11.XCreateSimpleWindow(server.display, server.root, 0, 580,
                                                800, 20, 0, 0, 0xff0000)
        server.x11.XMapWindow(server.display, window)
        server.x11.XClearWindow(server.display, window)
        server.x11.XSync(server.display, False)
        server.wait_output(b'\x1bP', start)
        time.sleep(0.1)
        frames = re.findall(rb'\x1bP[0-9;]*q"1;1;(\d+);(\d+)', server.output[start:])
        self.assertTrue(frames)
        self.assertEqual(set(frames), {(b'800', b'600')})
        self.assertNotIn(b'Filp;', server.output)
        server.check_drawing_and_pointer(self, 800, 600)
        server.close()
        self.assertIn(b'\x1b[?80r', server.output)
        self.assertIn(b'\x1b[>0p', server.output)
        self.assertIn(b'\x1b[?25h', server.output)
        self.assertLess(server.output.rfind(b'\x1bP'), server.output.rfind(b'\x1b[?80r'))

    def test_small_damage_uses_transparent_pixel_offset(self):
        server = self.server(pixels=(800, 600))
        server.wait_output(b'\x1bP')
        time.sleep(0.1)
        start = len(server.output)
        window = server.x11.XCreateSimpleWindow(server.display, server.root, 701, 563,
                                                13, 11, 0, 0, 0x0000ff)
        server.x11.XMapWindow(server.display, window)
        server.x11.XClearWindow(server.display, window)
        server.x11.XSync(server.display, False)
        # The RGB crop is only 13x18; preceding rows/columns are transparent
        # positioning commands, even when the coordinates are not cell-aligned.
        prefix = b'\x1bP0;1q"1;1;714;576' + b'-' * 93 + b'!701?'
        server.wait_output(prefix, start)
        server.wait_output(b'\x1b\\', start)
        time.sleep(0.03)
        packet = bytes(server.output[start:])
        self.assertNotIn(b'q"1;1;800;600', packet)
        self.assertNotIn(b'\x1b[?80l', packet)
        self.assertLess(len(packet), 10000)

    def test_iterm2_compat_preserves_pixels_before_damage(self):
        server = self.server(pixels=(800, 600), args=('-iterm2',))
        Autosize.check_iterm2_pixels(self, server)

    def test_iterm2_pointer_motion_keeps_image_bounds_fixed(self):
        server = self.server(pixels=(800, 600), args=('-iterm2',))
        server.x11.XCreateFontCursor.restype = C.c_ulong
        server.x11.XCreateFontCursor.argtypes = [C.c_void_p, C.c_uint]
        server.x11.XDefineCursor.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong]
        cursor = server.x11.XCreateFontCursor(server.display, 68)
        server.x11.XDefineCursor(server.display, server.root, cursor)
        server.x11.XSync(server.display, False)
        server.wait_output(b'q"1;1;800;600')
        time.sleep(.1)
        for x, y in ((121, 83), (701, 563), (211, 91)):
            start = len(server.output)
            server.x11.XWarpPointer(server.display, 0, server.root, 0, 0, 0, 0, x, y)
            server.x11.XSync(server.display, False)
            server.wait_output(b'q"1;1;800;600', start)
            time.sleep(.03)
            sizes = re.findall(rb'q"1;1;([0-9]+);([0-9]+)', bytes(server.output[start:]))
            self.assertTrue(sizes)
            self.assertTrue(all(size == (b'800', b'600') for size in sizes))

    def test_iterm2_startup_environment(self):
        for identity in ({'TERM_PROGRAM': 'iTerm.app'},
                         {'LC_TERMINAL': 'iTerm2'},
                         {'TERM_PROGRAM': 'tmux', 'LC_TERMINAL': 'iTerm2'}):
            with self.subTest(identity=identity):
                env = {k: v for k, v in os.environ.items() if k not in
                       ('TMUX', 'TMUX_PANE', 'STY', 'TERM_PROGRAM', 'LC_TERMINAL')}
                env.update(identity)
                server = self.server(pixels=(800, 600), env=env)
                Autosize.check_iterm2_pixels(self, server)
                server.close()

    def test_other_terminal_identity_retains_cropped_output(self):
        for identity in ({}, {'TERM_PROGRAM': 'Apple_Terminal'},
                         {'TERM_PROGRAM': 'tmux', 'LC_TERMINAL': ''},
                         {'TERM_PROGRAM': 'iTerm.app-extra', 'LC_TERMINAL': 'iTerm2-extra'}):
            with self.subTest(identity=identity):
                env = {k: v for k, v in os.environ.items() if k not in
                       ('TMUX', 'TMUX_PANE', 'STY', 'TERM_PROGRAM', 'LC_TERMINAL')}
                env.update(identity)
                server = self.server(pixels=(800, 600), env=env)
                server.wait_output(b'\x1bP')
                time.sleep(.05)
                start = len(server.output)
                window = server.x11.XCreateSimpleWindow(server.display, server.root,
                                                        101, 83, 13, 11, 0, 0, 0xff)
                server.x11.XMapWindow(server.display, window)
                server.x11.XSync(server.display, False)
                prefix = b'q"1;1;114;96' + b'-' * 13 + b'!101?'
                server.wait_output(prefix, start)
                server.close()

    def check_iterm2_pixels(self, server):
        subprocess.run(['xsetroot', '-display', server.name, '-solid', 'red'], check=True)
        time.sleep(.1)
        start = len(server.output)
        window = server.x11.XCreateSimpleWindow(server.display, server.root, 101, 83,
                                                13, 11, 0, 0, 0x0000ff)
        server.x11.XMapWindow(server.display, window)
        server.x11.XSync(server.display, False)
        server.wait_output(b'q"1;1;800;600', start)
        time.sleep(.05)
        data = bytes(server.output[start:])
        if b'\x1bPtmux;' in data:
            data = b''.join(re.findall(rb'\x1bPtmux;((?:\x1b\x1b|[^\x1b])*)\x1b\\', data, re.S))
            data = data.replace(b'\x1b\x1b', b'\x1b')
        elif b'\x1bP\x1b[H' in data:
            data = b''.join(re.findall(rb'\x1bP(.*?)\x1b\\', data, re.S))
        packets = re.findall(rb'\x1bP0;1q"1;1;800;600.*?\x1b\\', data, re.S)
        self.assertTrue(packets)
        # Decode the pixels, rather than checking just an origin/header change.
        lib = C.CDLL(ctypes.util.find_library('sixel'))
        lib.sixel_decode_raw.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_void_p),
            C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_void_p),
            C.POINTER(C.c_int), C.c_void_p]
        libc = C.CDLL(ctypes.util.find_library('c'))
        libc.free.argtypes = [C.c_void_p]
        pixels, palette = C.c_void_p(), C.c_void_p()
        width, height, colors = C.c_int(), C.c_int(), C.c_int()
        encoded = C.create_string_buffer(packets[-1])
        status = lib.sixel_decode_raw(encoded, len(packets[-1]), C.byref(pixels),
            C.byref(width), C.byref(height), C.byref(palette), C.byref(colors), None)
        try:
            self.assertEqual(status, 0)
            self.assertEqual((width.value, height.value), (800, 600))
            indexed = C.cast(pixels, C.POINTER(C.c_ubyte))
            rgb = C.cast(palette, C.POINTER(C.c_ubyte))
            def color(x, y):
                index = indexed[y * width.value + x]
                self.assertLess(index, colors.value)
                return tuple(rgb[index * 3 + i] for i in range(3))
            self.assertEqual(color(0, 0), (255, 0, 0))
            self.assertEqual(color(100, 82), (255, 0, 0))
            self.assertEqual(color(105, 88), (0, 0, 255))
            self.assertEqual(color(110, 95), (255, 0, 0))
            self.assertEqual(color(799, 599), (255, 0, 0))
        finally:
            libc.free(pixels)
            libc.free(palette)
        self.assertNotIn(b'q"1;1;114;96', data)

    def test_initial_ioctl_pixels(self):
        server = self.server(pixels=(800, 600))
        self.assertEqual(server.dimensions(), (800, 600))
        server.check_drawing_and_pointer(self, 800, 600)

    def test_iterm2_retina_framebuffer_and_mouse_use_separate_units(self):
        for identity in ({'TERM_PROGRAM': 'iTerm.app'}, {'LC_TERMINAL': 'iTerm2'}, {}):
            for screen in (False, True):
                with self.subTest(identity=identity, screen=screen):
                    env = {k: v for k, v in os.environ.items() if k not in
                           ('TMUX', 'TMUX_PANE', 'STY', 'TERM_PROGRAM', 'LC_TERMINAL')}
                    env.update(identity)
                    if screen:
                        env['STY'] = 'xsixel-screen-test'
                    server = self.server(pixels=(1120, 850), cells=(80, 25),
                                         env=env, args=() if identity else ('-iterm2',))
                    self.assertEqual(server.dimensions(), (1120, 850))
                    self.assertNotIn(b'\x1b[14t', server.output)
                    server.wait_output(b'\x1b[16t')
                    server.reply(b'\x1b[6;17;7t')
                    server.wait_size(1120, 850)
                    if screen:
                        Autosize.mouse_events(self, server,
                            [b'\x1b[<0;80;25M', b'\x1b[<0;80;25m'],
                            [(4, 1, 1113, 833), (5, 1, 1113, 833)])
                    else:
                        Autosize.mouse_events(self, server,
                            [b'\x1b[<0;557;417M', b'\x1b[<0;557;417m'],
                            [(4, 1, 1112, 832), (5, 1, 1112, 832)])
                    # A late window-size reply must not replace the backing pixel grid.
                    server.reply(b'\x1b[4;462;587t')
                    server.wait_size(1120, 850)
                    start = len(server.output)
                    server.resize_terminal((1400, 1020), cells=(100, 30))
                    server.wait_output(b'\x1b[16t', start)
                    server.reply(b'\x1b[6;17;7t')
                    server.wait_size(1400, 1020)
                    self.assertNotIn(b'\x1b[14t', server.output[start:])
                    server.close()

    def test_iterm2_without_ioctl_still_queries(self):
        server = self.server(pixels=(0, 0), args=('-iterm2',))
        server.wait_output(b'\x1b[16t')
        self.assertNotIn(b'\x1b[14t', server.output)
        server.reply(b'\x1b[6;17;7t')
        server.wait_size(560, 510)

    def test_terminal_reply_corrects_stale_ioctl_pixels(self):
        server = self.server(pixels=(640, 480), cells=(120, 30))
        self.assertEqual(server.dimensions(), (640, 480))
        server.wait_output(b'\x1b[14t\x1b[16t')
        server.reply(b'\x1b[4;600;1200t\x1b[6;20;10t')
        server.wait_size(1200, 600)
        server.check_drawing_and_pointer(self, 1200, 600)
        # A later cell report must not override a complete text-area report.
        server.reply(b'\x1b[6;21;11t')
        time.sleep(.05)
        self.assertEqual(server.dimensions(), (1200, 600))
        start = len(server.output)
        server.resize_terminal((640, 480), cells=(100, 25))
        server.wait_output(b'\x1b[14t\x1b[16t', start)
        server.reply(b'\x1b[4;500;1000t\x1b[6;20;10t')
        server.wait_size(1000, 500)

    def test_cell_reply_can_correct_stale_ioctl_without_text_reply(self):
        server = self.server(pixels=(640, 480), cells=(120, 30))
        server.wait_output(b'\x1b[14t\x1b[16t')
        server.reply(b'\x1b[6;20;10t')
        server.wait_size(1200, 600)
        server.reply(b'\x1b[4;610;1210t')
        server.wait_size(1210, 610)

    def test_no_reply_falls_back_without_waiting(self):
        server = self.server()
        self.assertEqual(server.dimensions(), (640, 480))
        self.assertIn(b'\x1b[14t', server.output)
        server.check_drawing_and_pointer(self, 640, 480)

    def test_late_pixels_grow_shrink_and_randr(self):
        server = self.server()
        for width, height in [(900, 700), (320, 240), (800, 600)]:
            server.reply(f'\x1b[4;{height};{width}t'.encode())
            server.wait_size(width, height)
            self.assertIn((width, height), server.events())
            server.check_drawing_and_pointer(self, width, height)
        server.reply(b'\x1b[4;600;800t')
        time.sleep(0.1)
        self.assertEqual(server.events(), [])

    def test_explicit_screen_wins(self):
        server = self.server(pixels=(800, 600), args=('-screen', '1024x768'))
        server.reply(b'\x1b[4;240;320t')
        time.sleep(0.1)
        self.assertEqual(server.dimensions(), (1024, 768))
        self.assertEqual(server.events(), [])
        server.check_drawing_and_pointer(self, 1024, 768)

    def test_cell_pixels_and_rows_in_either_order(self):
        for replies in [(b'\x1b[6;20;10t', b'\x1b[8;25;90t'),
                        (b'\x1b[8;25;90t', b'\x1b[6;20;10t')]:
            with self.subTest(replies=replies):
                server = self.server(cells=(0, 0))
                for reply in replies:
                    server.reply(reply)
                    time.sleep(0.05)
                server.wait_size(900, 500)
                server.reply(b'\x1b[4;600;1000t')
                server.wait_size(1000, 600)
                server.check_drawing_and_pointer(self, 1000, 600)

    def test_fragmented_reply_after_other_input(self):
        server = self.server()
        for fragment in [b'a\x1b[4;6', b'00;', b'8', b'00t']:
            server.reply(fragment)
            time.sleep(0.05)
        server.wait_size(800, 600)
        server.check_drawing_and_pointer(self, 800, 600)

    def test_invalid_replies_are_ignored(self):
        server = self.server()
        for reply in [b'\x1b[4t', b'\x1b[4;600t', b'\x1b[4;0;800t',
                      b'\x1b[4;600;999999999999999999999t', b'\x1b[4;600;800;1t',
                      b'\x1b[6;32767;32767t']:
            server.reply(reply)
            time.sleep(0.03)
        self.assertEqual(server.dimensions(), (640, 480))
        server.reply(b'\x1b[4;600;800t')
        server.wait_size(800, 600)
        server.check_drawing_and_pointer(self, 800, 600)

    def test_sigwinch_ioctl_grow_shrink_and_duplicate(self):
        server = self.server(pixels=(800, 600))
        for width, height in [(1024, 768), (320, 240), (800, 600)]:
            server.resize_terminal((width, height))
            server.wait_size(width, height)
            self.assertIn((width, height), server.events())
            server.check_drawing_and_pointer(self, width, height)
        server.resize_terminal((800, 600))
        time.sleep(0.2)
        self.assertEqual(server.events(), [])

    def test_sigwinch_burst_uses_final_size(self):
        server = self.server(pixels=(800, 600))
        for pixels in [(900, 600), (920, 620), (940, 640), (960, 660), (1000, 700)]:
            server.resize_terminal(pixels)
            time.sleep(0.01)
        server.wait_size(1000, 700)
        # RandR may send multiple notifications for one configuration change;
        # no intermediate dimensions should become visible to clients.
        sizes = server.events()
        self.assertTrue(sizes)
        self.assertEqual(set(sizes), {(1000, 700)})
        server.check_drawing_and_pointer(self, 1000, 700)

    def test_sigwinch_requeries_missing_pixels(self):
        server = self.server(pixels=(800, 600))
        start = len(server.output)
        server.resize_terminal((0, 0), cells=(100, 40))
        server.wait_output(b'\x1b[14t', start)
        self.assertEqual(server.dimensions(), (800, 600))
        server.reply(b'\x1b[4;720;1000t')
        server.wait_size(1000, 720)
        self.assertIn((1000, 720), server.events())
        server.check_drawing_and_pointer(self, 1000, 720)

    def test_sigwinch_font_change_does_not_reuse_old_cell_size(self):
        server = self.server(cells=(80, 30))
        server.reply(b'\x1b[6;20;10t')
        server.wait_size(800, 600)
        server.events()
        start = len(server.output)
        server.resize_terminal((0, 0), cells=(100, 40))
        server.wait_output(b'\x1b[16t', start)
        self.assertEqual(server.dimensions(), (800, 600))
        self.assertEqual(server.events(), [])
        server.reply(b'\x1b[6;16;8t')
        server.wait_size(800, 640)
        server.check_drawing_and_pointer(self, 800, 640)

    def test_sigwinch_explicit_screen_remains_fixed(self):
        server = self.server(pixels=(800, 600), args=('-screen', '1024x768'))
        server.resize_terminal((320, 240))
        time.sleep(0.2)
        self.assertEqual(server.dimensions(), (1024, 768))
        self.assertEqual(server.events(), [])
        server.check_drawing_and_pointer(self, 1024, 768)

    def test_sigwinch_pending_at_shutdown(self):
        server = self.server(pixels=(800, 600))
        server.resize_terminal((900, 700))
        # Cleanup must cancel the pending timer and restore the signal handler.

    def test_allocation_failure_keeps_old_screen(self):
        server = self.server(memory_limit=True)
        server.reply(b'\x1b[4;32767;32767t')
        time.sleep(0.1)
        self.assertEqual(server.dimensions(), (640, 480))
        server.check_drawing_and_pointer(self, 640, 480)
        server.reply(b'\x1b[4;600;800t')
        server.wait_size(800, 600)
        server.check_drawing_and_pointer(self, 800, 600)


@unittest.skipUnless(shutil.which('tmux'), 'tmux not installed')
class TmuxPassthrough(unittest.TestCase):
    def setUp(self):
        import uuid
        self.socket = 'xsixel-test-' + uuid.uuid4().hex
        self.tmux('new-session', '-d', '-s', 'test')
        self.tmux('set-option', '-g', 'focus-events', 'on')
        self.addCleanup(self.tmux, 'kill-server')
        socket_path = self.tmux('display-message', '-p', '#{socket_path}').strip()
        pane = self.tmux('display-message', '-p', '#{pane_id}').strip()
        self.env = dict({k: v for k, v in os.environ.items()
                         if k not in ("TERM_PROGRAM", "LC_TERMINAL", "STY")}, TMUX=socket_path + ',0,0', TMUX_PANE=pane)

    def tmux(self, *args):
        return subprocess.check_output(['tmux', '-L', self.socket, '-f', '/dev/null',
                                        *args], text=True)

    def test_disabled_explains_setting_before_terminal_changes(self):
        self.tmux('set-option', '-p', 'allow-passthrough', 'off')
        result = subprocess.run([os.environ['XSIXEL'], '-displayfd', '1'],
                                env=self.env, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'tmux set-option -p allow-passthrough on', result.stderr)
        self.assertNotIn(b'\x1b', result.stdout)

    def test_disabled_focus_explains_setting_before_terminal_changes(self):
        self.tmux('set-option', '-p', 'allow-passthrough', 'on')
        self.tmux('set-option', '-g', 'focus-events', 'off')
        result = subprocess.run([os.environ['XSIXEL'], '-displayfd', '1'],
                                env=self.env, capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'tmux set-option -g focus-events on', result.stderr)
        self.assertNotIn(b'\x1b', result.stdout)

    def test_iterm2_identity_forwarded_through_tmux(self):
        self.tmux('set-option', '-p', 'allow-passthrough', 'on')
        env = dict(self.env, TERM_PROGRAM='tmux', LC_TERMINAL='iTerm2')
        server = Server(pixels=(800, 600), env=env)
        self.addCleanup(server.close)
        Autosize.check_iterm2_pixels(self, server)

    def test_pixel_size_is_not_rounded_for_tmux(self):
        self.tmux('set-option', '-p', 'allow-passthrough', 'on')
        server = Server(pixels=(604, 459), cells=(100, 35), env=self.env)
        self.addCleanup(server.close)
        self.assertEqual(server.dimensions(), (604, 459))
        self.assertNotIn(b'\x1b[14t', server.output)
        self.assertNotIn(b'\x1b[16t', server.output)

    def test_mouse_cells_cover_screen_and_follow_resize(self):
        self.tmux('set-option', '-g', 'allow-passthrough', 'on')
        server = Server(pixels=(800, 600), env=self.env)
        self.addCleanup(server.close)
        server.wait_output(b'\x1b[?1006h\x1b[?1003h')
        self.assertNotIn(b'\x1b[?1016h', server.output)
        Autosize.mouse_events(self, server,
            [b'\x1b[<0;1;1M', b'\x1b[<32;80;30M', b'\x1b[<0;80;30m',
             b'\x1b[<64;40;15M'],
            [(4, 1, 5, 10), (5, 1, 795, 590),
             (4, 4, 395, 290), (5, 4, 395, 290)])
        server.resize_terminal((1200, 800), cells=(100, 40))
        server.wait_size(1200, 800)
        Autosize.mouse_events(self, server,
            [b'\x1b[<0;100;40M', b'\x1b[<0;100;40m'],
            [(4, 1, 1194, 790), (5, 1, 1194, 790)])

    def test_enabled_wraps_images_and_mode_restore(self):
        for setting in ('on', 'all'):
            with self.subTest(setting=setting):
                self.tmux('set-option', '-g', 'allow-passthrough', setting)
                server = Server(pixels=(320, 240), env=self.env)
                try:
                    subprocess.run(['xsetroot', '-display', server.name, '-solid', 'red'],
                                   check=True)
                    deadline = time.monotonic() + 3
                    prefix = b'\x1bPtmux;\x1b\x1b[H\x1b\x1bP0;1q'
                    while prefix not in server.output and time.monotonic() < deadline:
                        time.sleep(.02)
                    self.assertIn(prefix, server.output)
                    self.assertNotIn(b'\x1b[5n', server.output)
                    self.assertIn(b'\x1b\x1b\\\x1b\\', server.output)
                    self.assertIn(b'\x1bPtmux;\x1b\x1b[?80s\x1b\x1b[?80h\x1b\\', server.output)
                finally:
                    server.close()
                self.assertIn(b'\x1bPtmux;\x1b\x1b[?80r\x1b\\', server.output)


class ScreenPassthrough(unittest.TestCase):
    def server(self, **kwargs):
        env = {k: v for k, v in os.environ.items() if k not in ('TMUX', 'TMUX_PANE', 'TERM_PROGRAM', 'LC_TERMINAL')}
        env['STY'] = 'xsixel-screen-test'
        server = Server(env=env, **kwargs)
        self.addCleanup(server.close)
        return server

    def test_bounded_packets_preserve_images_and_terminal_restore(self):
        server = self.server(pixels=(800, 600))
        subprocess.run(['xsetroot', '-display', server.name, '-solid', 'red'], check=True)
        server.wait_output(b'\x1bP\\\x1b\\')
        server.close()
        packets = re.findall(rb'\x1bP(.*?)\x1b\\', bytes(server.output), re.S)
        self.assertGreater(len(packets), 10)
        self.assertTrue(all(len(packet) <= 252 for packet in packets))
        decoded = b''.join(packets)
        self.assertIn(b'\x1b[?80s\x1b[?80h', decoded)
        self.assertIn(b'\x1b[H\x1bP0;1q"1;1;800;600', decoded)
        self.assertIn(b'\x1b\\', decoded)
        self.assertIn(b'\x1b[?80r', decoded)
        self.assertNotIn(b'tmux;', decoded)
        # Each image must retain a complete header and terminator after unpacking.
        images = re.findall(rb'\x1bP0;1q(.*?)\x1b\\', decoded, re.S)
        self.assertTrue(images)
        self.assertTrue(all(image.startswith(b'"1;1;') for image in images))

    def test_iterm2_identity_forwarded_through_screen(self):
        env = {k: v for k, v in os.environ.items() if k not in
               ('TMUX', 'TMUX_PANE', 'TERM_PROGRAM', 'LC_TERMINAL')}
        env.update(STY='xsixel-screen-test', LC_TERMINAL='iTerm2')
        server = Server(pixels=(800, 600), env=env)
        self.addCleanup(server.close)
        Autosize.check_iterm2_pixels(self, server)

    def test_iterm2_compat_through_screen(self):
        Autosize.test_iterm2_compat_preserves_pixels_before_damage(self)

    def test_keyboard(self):
        Autosize.test_terminal_keyboard_reaches_focused_client(self)

    def test_periodic_redraw_needs_no_terminal_response(self):
        server = self.server(pixels=(800, 600))
        full = b'\x1bP\x1b[H\x1bP0;1q"1;1;800;600'
        server.wait_output(full)
        initial = bytes(server.output).count(full)
        deadline = time.monotonic() + 3
        # Never send a response: auto redraw must not depend on terminal input.
        while bytes(server.output).count(full) <= initial and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertGreater(bytes(server.output).count(full), initial)
        self.assertNotIn(b'\x1b[5n', server.output)

    def test_ioctl_padding_is_excluded_on_startup_and_resize(self):
        server = self.server(pixels=(604, 459), cells=(100, 35))
        self.assertEqual(server.dimensions(), (600, 455))
        server.check_drawing_and_pointer(self, 600, 455)
        server.resize_terminal((724, 524), cells=(120, 40))
        server.wait_size(720, 520)
        server.check_drawing_and_pointer(self, 720, 520)
        # A font change must infer the new cell size rather than reuse it.
        server.resize_terminal((964, 644), cells=(120, 40))
        server.wait_size(960, 640)
        server.resize_terminal((604, 459), cells=(100, 35))
        server.wait_size(600, 455)
        Autosize.mouse_events(self, server,
            [b'\x1b[<0;100;35M', b'\x1b[<0;100;35m'],
            [(4, 1, 597, 448), (5, 1, 597, 448)])

    def test_size_queries_pass_through_screen_with_stale_ioctl(self):
        server = self.server(pixels=(640, 480), cells=(120, 30))
        query = b'\x1bP\x1b[14t\x1b[16t\x1b\\'
        server.wait_output(query)
        server.reply(b'\x1b[4;600;1200t\x1b[6;20;10t')
        server.wait_size(1200, 600)
        start = len(server.output)
        server.resize_terminal((640, 480), cells=(100, 25))
        server.wait_output(query, start)
        server.reply(b'\x1b[6;20;10t')
        server.wait_size(1000, 500)

    def test_padding_correction_keeps_explicit_screen(self):
        server = self.server(pixels=(604, 459), cells=(100, 35),
                             args=('-screen', '800x600'))
        self.assertEqual(server.dimensions(), (800, 600))
        server.resize_terminal((724, 524), cells=(120, 40))
        time.sleep(.2)
        self.assertEqual(server.dimensions(), (800, 600))

    def test_padding_reports_in_either_order_keep_original_pixels(self):
        server = self.server(pixels=(0, 0), cells=(80, 30))
        server.reply(b'\x1b[4;459;604t')
        server.wait_size(560, 450)
        server.reply(b'\x1b[8;35;100t')
        server.wait_size(600, 455)
        server.resize_terminal((0, 0), cells=(120, 40))
        time.sleep(.2)
        server.reply(b'\x1b[8;35;100t\x1b[4;459;604t')
        server.wait_size(600, 455)

    def test_cell_mouse_and_resize(self):
        server = self.server(pixels=(800, 600))
        server.wait_output(b'\x1b[?1006h\x1b[?1003h')
        self.assertNotIn(b'\x1b[?1016h', server.output)
        Autosize.mouse_events(self, server,
            [b'\x1b[<0;1;1M', b'\x1b[<32;80;30M', b'\x1b[<0;80;30m'],
            [(4, 1, 5, 10), (5, 1, 795, 590)])
        server.resize_terminal((1200, 800), cells=(100, 40))
        server.wait_size(1200, 800)
        Autosize.mouse_events(self, server,
            [b'\x1b[<0;100;40M', b'\x1b[<0;100;40m', b'\x1b[<65;100;40M'],
            [(4, 1, 1194, 790), (5, 1, 1194, 790),
             (4, 5, 1194, 790), (5, 5, 1194, 790)])


@unittest.skipUnless(shutil.which('xprop'), 'xprop not installed')
class RenderModeControl(unittest.TestCase):
    property = '_XSIXEL_RENDER_MODE'

    def server(self, **kwargs):
        server = Server(pixels=(800, 600), **kwargs)
        self.addCleanup(server.close)
        return server

    def xprop(self, server, *args, check=True):
        return subprocess.run(['xprop', '-display', server.name, '-root', *args],
                              capture_output=True, text=True, check=check, timeout=5)

    def mode(self, server):
        return self.xprop(server, self.property).stdout.strip()

    def set_mode(self, server, mode):
        return self.xprop(server, '-f', self.property, '8s', '-set', self.property, mode)

    def test_round_trip_redraw_and_subsequent_damage(self):
        server = self.server()
        self.assertIn('"fast"', self.mode(server))
        time.sleep(.1)
        start = len(server.output)
        self.set_mode(server, 'fast')
        time.sleep(.05)
        self.assertNotIn(b'q"1;1;800;600', bytes(server.output[start:]))
        start = len(server.output)
        self.set_mode(server, 'iterm2')
        self.assertIn('"iterm2"', self.mode(server))
        server.wait_output(b'\x1bP0;1q"1;1;800;600', start)
        Autosize.check_iterm2_pixels(self, server)
        start = len(server.output)
        self.set_mode(server, 'fast')
        self.assertIn('"fast"', self.mode(server))
        server.wait_output(b'\x1bP0;1q"1;1;800;600', start)
        time.sleep(.05)
        start = len(server.output)
        window = server.x11.XCreateSimpleWindow(server.display, server.root,
                                                201, 143, 10, 10, 0, 0, 0xff)
        server.x11.XMapWindow(server.display, window)
        server.x11.XSync(server.display, False)
        server.wait_output(b'q"1;1;211;156' + b'-' * 23 + b'!201?', start)

    def test_initial_mode_matches_flag_and_environment(self):
        explicit = self.server(args=('-iterm2',))
        self.assertIn('"iterm2"', self.mode(explicit))
        explicit.close()
        env = {k: v for k, v in os.environ.items() if k not in
               ('TMUX', 'TMUX_PANE', 'STY', 'TERM_PROGRAM', 'LC_TERMINAL')}
        env['LC_TERMINAL'] = 'iTerm2'
        detected = self.server(env=env)
        self.assertIn('"iterm2"', self.mode(detected))
        self.set_mode(detected, 'fast')
        self.assertIn('"fast"', self.mode(detected))

    def test_invalid_values_type_and_deletion_preserve_mode(self):
        server = self.server()
        for value in ('unknown', 'FAST', '', 'iterm2-extra'):
            with self.subTest(value=value):
                result = self.xprop(server, '-f', self.property, '8s', '-set',
                                    self.property, value, check=False)
                self.assertIn('BadValue', result.stderr)
                self.assertIn('"fast"', self.mode(server))
        result = self.xprop(server, '-f', self.property, '32c', '-set',
                            self.property, '1', check=False)
        self.assertIn('BadMatch', result.stderr)
        result = self.xprop(server, '-remove', self.property, check=False)
        self.assertIn('BadAccess', result.stderr)
        self.assertIn('"fast"', self.mode(server))
        # A property with the same name on an ordinary window is just client data.
        window = server.x11.XCreateSimpleWindow(server.display, server.root,
                                                0, 0, 10, 10, 0, 0, 0)
        server.x11.XSync(server.display, False)
        subprocess.run(['xprop', '-display', server.name, '-id', str(window),
                        '-f', self.property, '8s', '-set', self.property, 'iterm2'],
                       check=True, capture_output=True)
        self.assertIn('"fast"', self.mode(server))


if __name__ == '__main__':
    if not os.environ.get('XSIXEL') or not Path(os.environ['XSIXEL']).is_file():
        raise SystemExit('Set XSIXEL to the built Xsixel executable')
    for program in ['xdpyinfo', 'xsetroot']:
        if not shutil.which(program):
            raise SystemExit(f'Required program not found: {program}')
    unittest.main(verbosity=2)
