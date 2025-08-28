#ifndef _UI_H
#define _UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"
#include "./common/lv_lib.h"
#include "../common/sys_manager/sys_manager.h"
#include "../common/gpio_manager/gpio_manager.h"

#define UI_SCREEN_WIDTH 320
#define UI_SCREEN_HEIGHT 240

typedef system_para_t ui_system_para_t;

// extern variables
extern lv_lib_pm_t page_manager;
extern ui_system_para_t ui_system_para;

// IMAGES AND IMAGE SETS
LV_IMG_DECLARE(ui_img_weather64_png);    // assets/weather64.png
LV_IMG_DECLARE(ui_img_calendar64_png);    // assets/calendar64.png
LV_IMG_DECLARE(ui_img_memo64_png);    // assets/Memo64.png
// P2级功能相关图像已移除 (GameMemory, Paint)
LV_IMG_DECLARE(ui_img_question60_png);    // assets/question60.png
LV_IMG_DECLARE(ui_img_think60_png);    // assets/think60.png
LV_IMG_DECLARE(ui_img_hand60_png);    // assets/hand60.png
// ui_img_muyu128_png 已移除 (木鱼游戏)
LV_IMG_DECLARE(ui_img_sun_png);    // assets/sun.png
LV_IMG_DECLARE(ui_img_clouds_png);    // assets/clouds.png

// FONTS
LV_FONT_DECLARE(ui_font_iconfont20);
LV_FONT_DECLARE(ui_font_iconfont26);
LV_FONT_DECLARE(ui_font_iconfont30);
LV_FONT_DECLARE(ui_font_iconfont36);
LV_FONT_DECLARE(ui_font_iconfont44);
LV_FONT_DECLARE(ui_font_iconfont48);
LV_FONT_DECLARE(ui_font_heiti14);
LV_FONT_DECLARE(ui_font_heiti22);
LV_FONT_DECLARE(ui_font_heiti24);
LV_FONT_DECLARE(ui_font_shuhei22);
LV_FONT_DECLARE(ui_font_NuberBig90);

// ui apps data
typedef lv_lib_pm_page_t ui_app_data_t;

// UI INIT
void ui_init(void);

// UI INFO MSGBOX
void ui_msgbox_info(const char * title, const char * text);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif