#include <stdio.h>
#include "AIchat_c_interface.h"

char dip[]="00:11:22:33:44:55";

int main() {
    void* app = create_aichat_app("192.168.211.1", 8765, "123456", "00:11:22:33:44:55", "sk-xxxx", 1, 16000, 1, 40);
    if (app == NULL) {
        printf("Failed to create application.\n");
        return -1;
    }

    run_aichat_app(app);

    destroy_aichat_app(app);

    return 0;
}