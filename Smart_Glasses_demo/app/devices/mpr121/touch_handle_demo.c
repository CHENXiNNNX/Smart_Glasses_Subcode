
#include<stdio.h>
#include"touch_handle.h"
int main(void)
{

    int ret=touch_handler_init();
    if(ret==-1)
    {
        perror("failed to init mpr121\n");
    }
    ret =touch_handler_run();
    if(ret==-1)
    {
        perror("touch_handle_run error\n");
    }
    touch_handler_deinit();
    return 0;

}