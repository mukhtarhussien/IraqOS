#!/data/data/com.termux/files/usr/bin/sh
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT="$BASE/IraqOS"
python "$BASE/vm/read_x11_size.py" "$PROJECT/Drivers/display_size.h"
python "$BASE/vm/generate_sounds.py"
make -C "$PROJECT/Bootloader"
