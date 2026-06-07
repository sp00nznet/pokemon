"""PyBoyShim -- a drop-in subset of the PyBoy 2.x API backed by our recompiled
Pokemon Red engine (red/build/Release/rom_headless.dll).

Only the methods PokemonRedExperiments touches are implemented:
    PyBoy(gb_path, window=...)      -> ctor (gb_path/window ignored; ROM is
                                       embedded in the DLL)
    .set_emulation_speed(n)         -> no-op (we run as fast as we tick)
    .tick(count=1, render=True)     -> advance `count` rendered frames
    .button_press(name)/.button_release(name)/.button(name)
    .send_input(event)              -> accepts PyBoy WindowEvent ints
    .memory[addr]                   -> read WRAM/IO/etc. byte (also slices)
    .screen.ndarray                 -> (144, 160, 3) uint8 RGB
    .stop(save=False)               -> destroy context

This lets the same driver code run against either real PyBoy or our engine,
which is the basis for the differential test harness (diff_harness.py).
"""
import ctypes
import os

import numpy as np

# DLL path is overridable via the GBROM_DLL env var (e.g. point at blue's).
_DLL_PATH = os.environ.get("GBROM_DLL") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "red", "build", "Release", "rom_headless.dll",
)

SCREEN_W, SCREEN_H = 160, 144

# active-low button masks (0 = pressed), matching platform_sdl.cpp / the bridge
_DPAD = {"right": 0x01, "left": 0x02, "up": 0x04, "down": 0x08}
_BTN = {"a": 0x01, "b": 0x02, "select": 0x04, "start": 0x08}

# Minimal PyBoy WindowEvent -> button-name map (press/release pairs) so
# send_input() works with the bot's `valid_actions` list. Values are the
# stable integer constants from pyboy.utils.WindowEvent.
_EVENT_TO_BTN = None


def _event_map():
    global _EVENT_TO_BTN
    if _EVENT_TO_BTN is None:
        from pyboy.utils import WindowEvent as W
        _EVENT_TO_BTN = {
            W.PRESS_ARROW_UP: ("up", True), W.RELEASE_ARROW_UP: ("up", False),
            W.PRESS_ARROW_DOWN: ("down", True), W.RELEASE_ARROW_DOWN: ("down", False),
            W.PRESS_ARROW_LEFT: ("left", True), W.RELEASE_ARROW_LEFT: ("left", False),
            W.PRESS_ARROW_RIGHT: ("right", True), W.RELEASE_ARROW_RIGHT: ("right", False),
            W.PRESS_BUTTON_A: ("a", True), W.RELEASE_BUTTON_A: ("a", False),
            W.PRESS_BUTTON_B: ("b", True), W.RELEASE_BUTTON_B: ("b", False),
            W.PRESS_BUTTON_START: ("start", True), W.RELEASE_BUTTON_START: ("start", False),
            W.PRESS_BUTTON_SELECT: ("select", True), W.RELEASE_BUTTON_SELECT: ("select", False),
        }
    return _EVENT_TO_BTN


class _Memory:
    def __init__(self, owner):
        self._o = owner

    def __getitem__(self, key):
        if isinstance(key, slice):
            start, stop = key.start or 0, key.stop or 0
            return [self._o._read(a) for a in range(start, stop)]
        return self._o._read(key)


class _Screen:
    def __init__(self, owner):
        self._o = owner

    @property
    def ndarray(self):
        return self._o._framebuffer()


class PyBoyShim:
    def __init__(self, gb_path=None, window="null", **kwargs):
        path = os.path.abspath(_DLL_PATH)
        if not os.path.exists(path):
            raise FileNotFoundError(
                f"rom_headless.dll not found at {path}. Build it with:\n"
                "  cmake --build red/build --target rom_headless --config Release"
            )
        self._dll = ctypes.CDLL(path)
        d = self._dll
        d.gbrom_create.restype = ctypes.c_void_p
        d.gbrom_destroy.argtypes = [ctypes.c_void_p]
        d.gbrom_step.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d.gbrom_read.restype = ctypes.c_uint8
        d.gbrom_read.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        d.gbrom_write.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint8]
        d.gbrom_set_buttons.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8]
        d.gbrom_framebuffer.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        d.gbrom_snapshot_size.restype = ctypes.c_int
        d.gbrom_snapshot.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        d.gbrom_restore.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        # Optional bulk read; older DLLs lack it -> transparent per-byte fallback.
        try:
            d.gbrom_read_range.argtypes = [ctypes.c_void_p, ctypes.c_uint16,
                                           ctypes.c_int, ctypes.c_char_p]
            self._has_range = True
        except AttributeError:
            self._has_range = False

        self._ctx = d.gbrom_create()
        if not self._ctx:
            raise RuntimeError("gbrom_create() failed")
        self._dpad = 0xFF
        self._btn = 0xFF
        self._fbuf = ctypes.create_string_buffer(SCREEN_W * SCREEN_H * 3)
        # WRAM (0xC000-0xDFFF) + HRAM (0xFF00-0xFFFF) snapshot, refreshed lazily
        # once per tick. Turns an env's hundreds of per-byte reads/step into
        # array lookups behind a single bulk FFI call.
        self._wram = b"\x00" * 0x2000
        self._hram = b"\x00" * 0x100
        self._wbuf = ctypes.create_string_buffer(0x2000)
        self._hbuf = ctypes.create_string_buffer(0x100)
        self._cache_valid = False
        # Optional per-frame capture for video recording. When set to a list,
        # tick() steps one frame at a time and appends each framebuffer. Left
        # None during training so tick() stays a single bulk FFI call.
        self._frame_sink = None
        self.memory = _Memory(self)
        self.screen = _Screen(self)

    # --- PyBoy-compatible surface -------------------------------------------
    def set_emulation_speed(self, _n):
        pass

    def tick(self, count=1, render=True):
        count = int(count)
        if self._frame_sink is None:
            self._dll.gbrom_step(self._ctx, count)
        else:
            # one frame at a time so every intra-step frame can be recorded
            for _ in range(count):
                self._dll.gbrom_step(self._ctx, 1)
                self._frame_sink.append(self._framebuffer().copy())
        self._cache_valid = False
        return True

    def button_press(self, name):
        self._apply(name, True)

    def button_release(self, name):
        self._apply(name, False)

    def button(self, name, delay=1):
        self._apply(name, True)
        self.tick(delay)
        self._apply(name, False)

    def send_input(self, event):
        name, pressed = _event_map()[event]
        self._apply(name, pressed)

    def save_state(self, f):
        """Write our snapshot blob to file object `f` (NOT PyBoy .state format)."""
        n = self._dll.gbrom_snapshot_size()
        buf = ctypes.create_string_buffer(n)
        self._dll.gbrom_snapshot(self._ctx, buf)
        f.write(buf.raw)

    def load_state(self, f):
        """Restore a snapshot blob previously written by save_state()."""
        n = self._dll.gbrom_snapshot_size()
        data = f.read()
        if len(data) != n:
            raise ValueError(
                f"snapshot size mismatch: file has {len(data)} bytes, "
                f"DLL expects {n}. Regenerate the start state with this DLL."
            )
        buf = ctypes.create_string_buffer(data, n)
        self._dll.gbrom_restore(self._ctx, buf)
        self._cache_valid = False

    def stop(self, save=False):
        if getattr(self, "_ctx", None):
            self._dll.gbrom_destroy(self._ctx)
            self._ctx = None

    # --- internals ----------------------------------------------------------
    def _apply(self, name, pressed):
        name = name.lower()
        if name in _DPAD:
            bit = _DPAD[name]
            self._dpad = (self._dpad & ~bit) if pressed else (self._dpad | bit)
        elif name in _BTN:
            bit = _BTN[name]
            self._btn = (self._btn & ~bit) if pressed else (self._btn | bit)
        else:
            raise KeyError(f"unknown button {name!r}")
        self._dll.gbrom_set_buttons(self._ctx, self._dpad & 0xFF, self._btn & 0xFF)

    def _refresh_cache(self):
        self._dll.gbrom_read_range(self._ctx, 0xC000, 0x2000, self._wbuf)
        self._dll.gbrom_read_range(self._ctx, 0xFF00, 0x100, self._hbuf)
        self._wram = self._wbuf.raw
        self._hram = self._hbuf.raw
        self._cache_valid = True

    def _read(self, addr):
        addr &= 0xFFFF
        if self._has_range:
            if not self._cache_valid:
                self._refresh_cache()
            if 0xC000 <= addr < 0xE000:
                return self._wram[addr - 0xC000]
            if 0xFF00 <= addr <= 0xFFFF:
                return self._hram[addr - 0xFF00]
        return self._dll.gbrom_read(self._ctx, addr)

    def _framebuffer(self):
        self._dll.gbrom_framebuffer(self._ctx, self._fbuf)
        return np.frombuffer(self._fbuf.raw, dtype=np.uint8).reshape(SCREEN_H, SCREEN_W, 3)

    def __del__(self):
        try:
            self.stop()
        except Exception:
            pass


# Alias so callers can do `from pyboy_shim import PyBoy`
PyBoy = PyBoyShim
