# IraqOS

IraqOS is an experimental ARM64 operating-system project. Its current bootable build runs on QEMU's virt machine and draws a small, Windows 7-inspired desktop directly into a VirtIO GPU framebuffer. This remains an early prototype: several desktop items are visual demos, and Turtle Terminal supports a small set of in-guest commands rather than Linux applications.

## Build and launch in Termux

Open Termux and start Termux:X11 first. IraqOS uses X11 display `:2` by default; set `DISPLAY` explicitly to use another display. Then run:

    cd ~/IraqOS
    ./build.sh
    ./run-vm.sh

The scripts expect clang, ld.lld, make, python, and qemu-system-aarch64 to be available. run-vm.sh builds again, detects the X11 display size, starts the serial bridge and QEMU fullscreen, and stops a previous IraqOS VM started by this runner. Press Enter in the attached Termux terminal at the login prompt to enter the desktop as admin.

For touch input, set Termux:X11's Pointer > Touchscreen input mode to Simulated touchscreen. A physical mouse can use Capture external pointer devices when possible. The guest has VirtIO tablet and keyboard devices. Open Turtle Terminal from the desktop, focus the QEMU window, then type commands in its in-window prompt; mouse movement and clicks remain available.

## Desktop controls

- Click the Start orb or type START to open the Start menu.
- Open Turtle Terminal from the taskbar, desktop icon, Start menu, or with Ctrl+Alt+T (Ctrl+T also works if the host terminal consumes Alt).
- Drag the terminal title bar; use its minimize, maximize/restore, and close buttons. The in-window keyboard prompt accepts guest commands and window controls such as MAXIMIZE, MINIMIZE, RESTORE, and CLOSE. Serial input remains available for desktop operation and recovery.
- Right-click the desktop for the context menu. Display settings expose font-size controls.
- COMPUTER and ABOUT open prototype windows; LOGOUT returns to the login screen.

The login page displays the preset account admin and a masked password decoration. These are not editable credentials and there is no authentication check yet. Start-menu search, Computer, and About are also visual prototypes.

## Turtle Terminal and current OS limits

Turtle Terminal is an in-guest command interpreter. Its commands run inside IraqOS and use the guest disk; they do not invoke a Termux shell. Built-ins include `pwd`, `ls`, `cd`, `cat`, `write`, `mkdir`, `rm`, `disk`, and `dijlah`. `dijlah repo` lists bundled script packages; `dijlah install hello` installs one on the guest disk, then run it with `hello`. The serial bridge only forwards keyboard input and optional UI sound cues.

The current guest boots as an ELF with QEMU's -kernel option. On QEMU virt, the Device Tree is at the start of RAM (0x40000000), so the kernel is linked at 0x40200000 and passes the tree to VirtIO drivers, which discover their MMIO regions instead of scanning hard-coded device addresses. It initializes a basic memory allocator, UART, VirtIO GPU, VirtIO tablet input, framebuffer console, login view, and desktop. The guest has a VirtIO block driver and IraqFS filesystem on the attached QCOW2 disk. It does not yet load Linux ELF applications or provide Linux system calls. EDK2 firmware files are staged in vm/firmware/, but the current runner does not boot through UEFI.

## Display and sounds

vm/read_x11_size.py reads the current X11 framebuffer size and regenerates IraqOS/Drivers/display_size.h. The framebuffer uses that full physical resolution; the GUI keeps its logical height at 768 pixels and calculates its width from the display aspect ratio. Do not hand-edit the generated header: rerun ./build.sh after changing display size.

Five original short effects live in assets/sounds/ (startup, login, open, close, and click). vm/generate_sounds.py recreates them during builds. The serial bridge plays them using termux-media-player when Termux:API is installed; if the player is unavailable, it sends the terminal bell instead.

## Project map

    IraqOS/
    ├── IraqOS/                 ARM64 kernel, drivers, shell, and framebuffer GUI
    │   ├── Bootloader/         startup code, linker script, Makefile, boot.elf
    │   ├── Drivers/            UART, framebuffer console, VirtIO GPU/input/block
    │   ├── Kernel/             kernel entry, IRQ, timer, exception code
    │   ├── Memory/             basic kernel allocator
    │   ├── FS/                 IraqFS for the VirtIO block disk
    │   └── Shell/              guest command interpreter and desktop/window manager
    ├── assets/sounds/          generated desktop sound effects
    ├── busybox/                vendored BusyBox source tree
    ├── vm/                     QEMU runner helpers, bridge, disk, and firmware
    ├── build.sh
    ├── run-vm.sh
    └── README-VM.md            additional VM notes

See README-VM.md for VM resource configuration and operational details. Repository-specific implementation guidance and the desktop change history are in AGENTS.md.

## Backup archive

IraqOS.zip is a copy of this project tree, stored with the project. It includes source, vendored source, assets, firmware, disk image, build outputs, and documentation. To avoid copying unrelated repository metadata or live runtime state, the archive omits .git directories, QEMU log files, vm/logs screenshots, the active VM PID file, and the serial socket. The original project files remain in place.
