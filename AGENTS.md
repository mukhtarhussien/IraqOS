# Agent instructions for IraqOS

## Working boundaries

- Treat this directory (~/IraqOS) as the project root. Keep project edits, generated assets, archives, and temporary project files inside it.
- Preserve existing source, vendored code, firmware, disk images, and build outputs unless the user explicitly asks to replace or remove something. The user specifically asked to keep the original project when creating a ZIP copy.
- The top-level busybox/ and IraqOS/busybox/ trees are vendored source. Do not bulk-reformat, delete, or replace them as part of unrelated kernel/GUI work. Preserve their license files.
- Do not install packages globally in Termux for a project change. Check whether a tool exists first. If an optional runtime component is missing, document the fallback or ask only when the missing component blocks the requested result.
- Read this file and the relevant source before editing. Make the smallest focused change that meets the user's request. Do not invent or fix errors without evidence.
- Do not run tests unless the user asks for tests or verification. A normal build needed to produce a requested runnable kernel is part of implementation; distinguish it from a test suite.

## Build and runtime map

- build.sh regenerates IraqOS/Drivers/display_size.h from the active X11 display, recreates the five WAV cues with vm/generate_sounds.py, then invokes the Bootloader Makefile.
- run-vm.sh calls the build, starts vm/serial_bridge.py, and launches qemu-system-aarch64 for the virt machine with 512 MiB RAM, VirtIO GPU, VirtIO tablet, and a blank QCOW2 disk. It uses QEMU's -kernel ELF boot path; the staged EDK2 firmware is not currently used.
- The VM runner's serial bridge passes keyboard/serial bytes, executes terminal commands in a persistent Termux bash/sh, and handles UI sound markers. This is a host shell with the Termux user's privileges, not a guest Linux process environment. Keep that distinction clear in code and user-facing documentation.
- QEMU uses the host X11 display size for the framebuffer. The guest UI uses UI_HEIGHT=768 and derives UI_WIDTH from framebuffer aspect ratio. display_size.h is generated; change vm/read_x11_size.py rather than editing the header by hand.
- Use ./build.sh from the project root to rebuild. Keep project-specific runtime commands within this directory. Do not terminate a VM the user is actively using unless a rebuild/restart is necessary for the requested change.

## Source map

- IraqOS/Bootloader/: ARM64 entry assembly, boot entry, linker script, Makefile, generated objects, and boot.elf.
- IraqOS/Kernel/: kernel startup, interrupts, exceptions, timer, and printk.
- IraqOS/Drivers/: UART, VirtIO GPU/framebuffer, VirtIO input, console/font rendering, and generated display dimensions.
- IraqOS/Memory/: simple kernel allocator used by the boot-time memory smoke check.
- IraqOS/Shell/gui.c: boot splash, login view, desktop wallpaper/icons/taskbar, Start and context menus, app/settings windows, terminal window, pointer drawing, hit-testing, animation, and UI sound markers.
- IraqOS/Shell/shell.c: current text shell flow.
- vm/serial_bridge.py: host-side serial protocol, Termux shell PTY, output framing, and optional audio playback.
- vm/read_x11_size.py, vm/resize_qemu_window.py, vm/generate_sounds.py: display sizing, fullscreen/window sizing, and WAV generation.
- assets/sounds/: generated mono 16-bit PCM WAV cues.
- README.md and README-VM.md: user overview and VM details.

## Desktop work already implemented

The current desktop is a custom framebuffer UI inspired by Windows 7/Aero. It is not a Windows port and does not use XFCE or a hosted desktop environment. The GUI renders through the IraqOS console and VirtIO GPU.

- Reworked the boot splash, login panel, wallpaper, desktop icons, taskbar, Start orb/menu, window title bars, settings panel, and Turtle Terminal frame to use a blue glass/Aero-like visual palette.
- Made the logical desktop width follow the physical display's aspect ratio while keeping a 768-pixel logical height; the UI fills the X11 framebuffer without stretching the old 1024:768 layout horizontally.
- Added framebuffer cursor drawing and VirtIO tablet hit-testing for left/right click, absolute/relative pointer movement, terminal dragging, window controls, desktop icons, Start menu actions, context menu, and font-size controls.
- Added terminal window open animation, close/minimize/maximize/restore handling, and settings that change the console font scale for the current boot.
- Added original WAV cues for startup, login, open, close, and click. The serial bridge plays these through termux-media-player if present and otherwise emits a terminal bell. Do not claim phone-speaker WAV playback works when the optional Termux:API media player is absent.
- Added or updated the Turtle Terminal host bridge. It starts a persistent Termux shell in this project directory, frames command output back to the guest, strips terminal escape sequences, and limits displayed output. Be explicit that this is host execution.

## Known limitations; do not describe prototypes as complete features

- The login view pre-fills admin and draws a masked password row, but the fields are not editable and do not authenticate.
- Start-menu search is decorative. Computer/About are prototype information windows. The guest has no mounted filesystem or functioning storage browser.
- The terminal's real commands execute on the Termux host. IraqOS does not yet implement Linux process loading, a complete syscall ABI, a guest shell, VirtIO block, or a guest filesystem.
- The kernel currently boots as an ELF via QEMU -kernel; EDK2 firmware is staged but unused. The QCOW2 disk is attached but blank/unmounted.
- UI cues are WAV files, but audible playback depends on termux-media-player; fallback is only the terminal bell.

When extending the UI, preserve keyboard operation in addition to mouse input, keep hit regions aligned with the drawn controls, scale geometry from UI_WIDTH/UI_HEIGHT, and redraw the full/affected framebuffer before showing the cursor so stale cursor pixels do not remain. When changing serial protocol markers, update both guest code and vm/serial_bridge.py together.
