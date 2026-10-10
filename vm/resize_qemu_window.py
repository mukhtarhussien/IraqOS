#!/usr/bin/env python3
import ctypes
import sys
import time

class ClassHint(ctypes.Structure):
    _fields_ = [("res_name", ctypes.c_void_p), ("res_class", ctypes.c_void_p)]

class XErrorEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("resourceid", ctypes.c_ulong),
        ("serial", ctypes.c_ulong),
        ("error_code", ctypes.c_ubyte),
        ("request_code", ctypes.c_ubyte),
        ("minor_code", ctypes.c_ubyte),
    ]

x11 = ctypes.CDLL("libX11.so")
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XDisplayWidth.argtypes = [ctypes.c_void_p, ctypes.c_int]
x11.XDisplayWidth.restype = ctypes.c_int
x11.XDisplayHeight.argtypes = [ctypes.c_void_p, ctypes.c_int]
x11.XDisplayHeight.restype = ctypes.c_int
x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
x11.XDefaultScreen.restype = ctypes.c_int
x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
x11.XGetClassHint.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ClassHint)]
x11.XGetClassHint.restype = ctypes.c_int
x11.XFree.argtypes = [ctypes.c_void_p]
x11.XFree.restype = ctypes.c_int
x11.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong), *([ctypes.POINTER(ctypes.c_uint)] * 6)]
x11.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
    ctypes.c_long, ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XInternAtom.restype = ctypes.c_ulong
x11.XMoveResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
    ctypes.c_int, ctypes.c_uint, ctypes.c_uint]
x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
x11.XFree.argtypes = [ctypes.c_void_p]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]


def handle_x_error(display, event_pointer):
    event = event_pointer.contents
    # SDL can destroy and recreate its fullscreen window between XQueryTree
    # and XGetWindowProperty. Ignore that stale-window race; report other X errors.
    if event.error_code != 3:  # BadWindow
        print(f"QEMU X11 helper: X error {event.error_code} on request "
              f"{event.request_code}", file=sys.stderr, flush=True)
    return 0


XErrorHandler = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(XErrorEvent))
x11.XSetErrorHandler.argtypes = [XErrorHandler]
x11.XSetErrorHandler.restype = ctypes.c_void_p
x_error_handler = XErrorHandler(handle_x_error)

try:
    target_pid = int(sys.argv[1])
    sys_display = __import__("os").environ.get("DISPLAY", ":0")
    dpy = x11.XOpenDisplay(sys_display.encode())
    if not dpy:
        raise SystemExit(0)
    x11.XSetErrorHandler(x_error_handler)
    screen = x11.XDefaultScreen(dpy)
    width, height = x11.XDisplayWidth(dpy, screen), x11.XDisplayHeight(dpy, screen)
    root = x11.XDefaultRootWindow(dpy)
    pid_atom = x11.XInternAtom(dpy, b"_NET_WM_PID", 0)
    last_notice = 0.0

    while True:
        try:
            os_kill = __import__("os").kill
            os_kill(target_pid, 0)
        except OSError:
            break
        root_ret, parent_ret = ctypes.c_ulong(), ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        x11.XQueryTree(dpy, root, ctypes.byref(root_ret), ctypes.byref(parent_ret),
                       ctypes.byref(children), ctypes.byref(count))
        candidates = []
        for i in range(count.value):
            window = children[i]
            hint = ClassHint()
            if not x11.XGetClassHint(dpy, window, ctypes.byref(hint)):
                continue
            klass = ctypes.string_at(hint.res_class).decode(errors="ignore") if hint.res_class else ""
            if hint.res_name: x11.XFree(hint.res_name)
            if hint.res_class: x11.XFree(hint.res_class)
            if klass != "qemu-system-aarch64":
                continue
            actual, fmt, nitems, after = ctypes.c_ulong(), ctypes.c_int(), ctypes.c_ulong(), ctypes.c_ulong()
            prop = ctypes.POINTER(ctypes.c_ubyte)()
            rc = x11.XGetWindowProperty(dpy, window, pid_atom, 0, 1, 0, 6,
                ctypes.byref(actual), ctypes.byref(fmt), ctypes.byref(nitems),
                ctypes.byref(after), ctypes.byref(prop))
            pid = int.from_bytes(bytes(prop[:4]), "little") if rc == 0 and prop and nitems.value else -1
            if prop: x11.XFree(prop)
            if pid != target_pid:
                continue
            root_geom = ctypes.c_ulong()
            dims = [ctypes.c_uint() for _ in range(6)]
            x11.XGetGeometry(dpy, window, ctypes.byref(root_geom),
                             *[ctypes.byref(value) for value in dims])
            candidates.append((dims[2].value * dims[3].value, window,
                               dims[0].value, dims[1].value, dims[2].value, dims[3].value))
        if children: x11.XFree(children)
        if candidates:
            _, window, wx, wy, ww, wh = max(candidates)
            if (wx, wy, ww, wh) != (0, 0, width, height):
                x11.XMoveResizeWindow(dpy, window, 0, 0, width, height)
                x11.XRaiseWindow(dpy, window)
                x11.XSetInputFocus(dpy, window, 2, 0)
                x11.XSync(dpy, 0)
                now = time.monotonic()
                if now - last_notice > 2:
                    print(f"QEMU X11 window set to {width}x{height}", flush=True)
                    last_notice = now
        time.sleep(0.2)
except (AttributeError, OSError, ValueError):
    pass
