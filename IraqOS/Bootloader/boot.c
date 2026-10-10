#include "../Drivers/virtio_gpu.h"
#include "../Drivers/virtio_input.h"
#include "../Drivers/console.h"
#include "../Kernel/kernel.h"
#include "../Shell/gui.h"

void boot_main(void)
{
    char username[24] = "";

    console_init();
    virtio_gpu_init();
    virtio_input_init();
    kernel_main();
    boot_splash();

    for (;;) {
        gui_login(username, sizeof(username));
        gui_desktop(username);
    }
}
