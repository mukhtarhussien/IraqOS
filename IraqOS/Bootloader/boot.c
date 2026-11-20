#include "../Drivers/virtio_gpu.h"
#include "../Drivers/virtio_input.h"
#include "../Drivers/virtio_blk.h"
#include "../Drivers/console.h"
#include "../FS/fs.h"
#include "../Kernel/kernel.h"
#include "../Kernel/printk.h"
#include "../Shell/gui.h"

void boot_main(const void *fdt)
{
    char username[24] = "";

    console_init();
    virtio_gpu_init(fdt);
    virtio_input_init(fdt);
    virtio_blk_init(fdt);
    if (fs_init()) printk("IraqFS: mounted\n");
    else printk("IraqFS: disk unavailable\n");
    kernel_main();
    boot_splash();

    for (;;) {
        gui_login(username, sizeof(username));
        gui_desktop(username);
    }
}
