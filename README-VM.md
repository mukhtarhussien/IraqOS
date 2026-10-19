# IraqOS virtual machine

Target: QEMU ARM64 `virt`.

## Build and run

From Termux: `cd ~/IraqOS && ./build.sh`, then `./run-vm.sh`.
The runner keeps the serial terminal attached to Termux, starts a fullscreen SDL display on Termux:X11 at the X server resolution, and stops the previous tracked IraqOS VM before starting another.

## VM resources

- 1 virtual CPU (SMP is not enabled in the kernel yet)
- 512 MiB RAM
- VirtIO GPU framebuffer sized to the current X11 display; the logical GUI height is 768 pixels and its width follows the display aspect ratio
- VirtIO tablet input for left/right clicks, terminal dragging, and desktop menus
- 8 GiB QCOW2 disk at `vm/disk/iraqos.qcow2`
- ARM64 EDK2 firmware staged in `vm/firmware/` for the future UEFI boot path

## Desktop

After sign-in, IraqOS shows a Windows 7-inspired blue glass desktop with a Start menu, pinned Turtle Terminal, desktop icons, display settings, and small Computer and About prototype windows. The Start menu and right-click menu open desktop actions; the terminal title bar can be dragged, minimized, maximized, restored, and closed. The login panel displays the preset username and a masked decorative password row, and accepts Enter or its arrow button; these are not editable credentials and no authentication check runs. Enter commands through the Termux serial terminal:

- `START` opens the Start menu
- `COMPUTER` and `ABOUT` open prototype windows
- `TERMINAL` opens TURTULE TERMINAL as an expanding desktop window; `MAXIMIZE`, `MINIMIZE`, `RESTORE`, and `CLOSE` control that window
- `SETTINGS` opens display settings; the plus and minus buttons change the font size from 2 to 5 for the current boot
- `LOGOUT` returns to the login screen

TURTULE TERMINAL runs a persistent Bash (or `sh` fallback) through `vm/serial_bridge.py`. Commands such as `pwd`, `ls`, `cd`, and `cat` execute in the real Termux shell, starting in `~/IraqOS`. They use the Termux account and filesystem; they do not run inside the custom IraqOS kernel. The current kernel does not yet implement Linux processes, system calls, or a guest filesystem, so running Linux ELF applications inside the IraqOS guest still requires those OS features. The GUI displays the latest terminal output and supports basic command-line programs.

Window redraws transfer only the changed framebuffer region. The boot stack is 64 KiB to accommodate the GUI and terminal call path.

A VirtIO tablet input driver is initialized for absolute mouse movement and left/right clicks. Click targets are wired for desktop icons, the Start menu, a right-click context menu, settings, and terminal titlebar controls. IraqOS creates five original short WAV cues under `assets/sounds`; the serial bridge plays them through `termux-media-player` when Termux:API is available, and otherwise sends the terminal bell. Keyboard input is read from the Termux serial terminal. On a touchscreen, Termux:X11 Pointer > Touchscreen input mode must be **Simulated touchscreen**; Direct touch forwards raw touch slots that this single-pointer guest does not consume. For a physical mouse, enable **Capture external pointer devices when possible**. The VM runner attaches a VirtIO tablet, and the guest polls its event queue while waiting for serial commands. The Computer window is a placeholder because the blank virtual disk is not mounted. Press Enter in the Termux serial terminal to enter the desktop as `admin`. From the desktop, Ctrl+Alt+T opens TURTULE TERMINAL; the terminal also accepts Ctrl+T if the host terminal consumes Alt.

## Boot and storage status

The kernel currently boots as an ELF with QEMU `-kernel`; the staged UEFI firmware is not used by this runner. The disk is attached but blank and not mounted until VirtIO block and filesystem drivers are implemented.
