#!/usr/bin/env python3
import ctypes
import os
import sys
from pathlib import Path

out = Path(sys.argv[1])
width, height = 1024, 768
display_name = os.environ.get("DISPLAY") or ":0"
try:
    x11 = ctypes.CDLL("libX11.so")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
    x11.XDefaultScreen.restype = ctypes.c_int
    x11.XDisplayWidth.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XDisplayWidth.restype = ctypes.c_int
    x11.XDisplayHeight.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XDisplayHeight.restype = ctypes.c_int
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    dpy = x11.XOpenDisplay(display_name.encode())
    if dpy:
        screen = x11.XDefaultScreen(dpy)
        detected_width = x11.XDisplayWidth(dpy, screen)
        detected_height = x11.XDisplayHeight(dpy, screen)
        x11.XCloseDisplay(dpy)
        if 320 <= detected_width <= 4096 and 240 <= detected_height <= 4096:
            width, height = detected_width, detected_height
except (OSError, AttributeError):
    pass

content = ("#ifndef IRAQOS_DISPLAY_SIZE_H\n#define IRAQOS_DISPLAY_SIZE_H\n"
           f"#define FB_WIDTH {width}\n#define FB_HEIGHT {height}\n"
           "#define UI_HEIGHT 768\n"
           "#define UI_WIDTH ((FB_WIDTH * UI_HEIGHT) / FB_HEIGHT)\n#endif\n")
if not out.exists() or out.read_text() != content:
    out.write_text(content)
print(f"IraqOS framebuffer: {width}x{height} (X11 display {display_name})")
