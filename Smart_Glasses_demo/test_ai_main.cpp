#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "app/app.h"

/**
 * @brief 无屏幕智能眼镜主程序
 * 
 * 这是从原始LVGL版本重构而来的无屏幕版本
 * 主要功能：
 * - AI语音对话
 * - 语音指令识别和执行
 * - 系统状态管理
 */
int main(void)
{
    // 运行应用程序主入口
    int result = app_main();
    
    return result;
}



