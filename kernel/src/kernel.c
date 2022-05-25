#include <FacelessBootProtocol.h>

void _start(struct RuntimeDataAndServices services) {
    services.display_terminal(250, 50);
    services.term_write("Hello from the kernel!", 0x00FF00);
    __asm__ __volatile__("cli; hlt");
}
