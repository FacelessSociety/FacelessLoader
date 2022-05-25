#include <FacelessBootProtocol.h>

void _start(struct RuntimeDataAndServices services) {
    for (uint64_t i = 0; i < 99999999; ++i) {
        __asm__ __volatile__("cli");
    }

    services.refresh_wallpaper();
    services.display_terminal(250, 50);

    services.term_write("Hello from the kernel!", 0x00FF00);
    __asm__ __volatile__("cli; hlt");
}
