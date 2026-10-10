# IraqOS

IraqOS is an experimental ARM64 operating-system project. Its current bootable build runs on QEMU's virt machine and draws a small, Windows 7-inspired desktop directly into a VirtIO GPU framebuffer. This remains an early prototype: several desktop items are visual demos, storage is not mounted, and Turtle Terminal executes commands in the Termux host shell.

## Build and launch in Termux

Open Termux and start Termux:X11 first. Then run:

    cd ~/IraqOS
    ./build.sh
    ./run-vm.sh

The scripts expect clang, ld.lld, make, python, and qemu-system-aarch64 to be available. run-vm.sh builds again, detects the X11 display size, starts the serial bridge and QEMU fullscreen, and stops a previous IraqOS VM started by this runner. Press Enter in the attached Termux terminal at the login prompt to enter the desktop as admin.

For touch input, set Termux:X11's Pointer > Touchscreen input mode to Simulated touchscreen. A physical mouse can use Capture external pointer devices when possible. The guest has a VirtIO tablet driver and handles absolute pointer movement plus left and right clicks.

## Desktop controls

- Click the Start orb or type START to open the Start menu.
- Open Turtle Terminal from the taskbar, desktop icon, Start menu, or with Ctrl+Alt+T (Ctrl+T also works if the host terminal consumes Alt).
- Drag the terminal title bar; use its minimize, maximize/restore, and close buttons. The serial prompt also accepts MAXIMIZE, MINIMIZE, RESTORE, and CLOSE.
- Right-click the desktop for the context menu. Display settings expose font-size controls.
- COMPUTER and ABOUT open prototype windows; LOGOUT returns to the login screen.

The login page displays the preset account admin and a masked password decoration. These are not editable credentials and there is no authentication check yet. Start-menu search, Computer, and About are also visual prototypes.

## Turtle Terminal and current OS limits

Turtle Terminal is a desktop window connected through vm/serial_bridge.py to a persistent Termux bash (or sh fallback). Commands start in ~/IraqOS and run with the Termux user's permissions. They do not execute as processes inside the IraqOS guest.

The current guest boots as an ELF with QEMU's -kernel option. It initializes a basic memory allocator, UART, VirtIO GPU, VirtIO tablet input, framebuffer console, login view, and desktop. It does not yet have Linux system calls/process execution, a guest shell, VirtIO block support, a mounted guest filesystem, or real login authentication. The attached QCOW2 disk is blank and unused. EDK2 firmware files are staged in vm/firmware/, but the current runner does not boot through UEFI.

## Display and sounds

vm/read_x11_size.py reads the current X11 framebuffer size and regenerates IraqOS/Drivers/display_size.h. The framebuffer uses that full physical resolution; the GUI keeps its logical height at 768 pixels and calculates its width from the display aspect ratio. Do not hand-edit the generated header: rerun ./build.sh after changing display size.

Five original short effects live in assets/sounds/ (startup, login, open, close, and click). vm/generate_sounds.py recreates them during builds. The serial bridge plays them using termux-media-player when Termux:API is installed; if the player is unavailable, it sends the terminal bell instead.

## Project map

    IraqOS/
    ├── IraqOS/                 ARM64 kernel, drivers, shell, and framebuffer GUI
    │   ├── Bootloader/         startup code, linker script, Makefile, boot.elf
    │   ├── Drivers/            UART, framebuffer console, VirtIO GPU/input
    │   ├── Kernel/             kernel entry, IRQ, timer, exception code
    │   ├── Memory/             basic kernel allocator
    │   └── Shell/              text shell and desktop/window manager
    ├── assets/sounds/          generated desktop sound effects
    ├── busybox/                vendored BusyBox source tree
    ├── vm/                     QEMU runner helpers, bridge, disk, and firmware
    ├── build.sh
    ├── run-vm.sh
    └── README-VM.md            additional VM notes

See README-VM.md for VM resource configuration and operational details. Repository-specific implementation guidance and the desktop change history are in AGENTS.md.

## Backup archive

IraqOS.zip is a copy of this project tree, stored with the project. It includes source, vendored source, assets, firmware, disk image, build outputs, and documentation. To avoid copying unrelated repository metadata or live runtime state, the archive omits .git directories, QEMU log files, vm/logs screenshots, the active VM PID file, and the serial socket. The original project files remain in place.
