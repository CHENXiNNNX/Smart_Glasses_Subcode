#include "ui_DrawPage.h"

///////////////////// VARIABLES ////////////////////

static uint8_t buf_raw[UI_SCREEN_WIDTH * UI_SCREEN_HEIGHT * 2];
lv_obj_t * canvas;

typedef enum {
    UI_DRAW_COLOR_RED,
    UI_DRAW_COLOR_BLUE,
    UI_DRAW_COLOR_BLACK,
    UI_DRAW_COLOR_MAX // 用于验证颜色范围
} ui_draw_color_t;

struct ui_Draw_para_t{
    uint32_t line_width; // 2, 3, 4, 5
    lv_color_t line_color; //RED, BLUE, BLACK
    uint8_t line_color_index;
};

struct ui_Draw_para_t ui_Draw_para;

///////////////////// ANIMATIONS ////////////////////


///////////////////// FUNCTIONS ////////////////////

static void _ui_Draw_clear(void)
{
    lv_canvas_fill_bg(canvas, lv_color_hex(0xffffff), LV_OPA_100);
} 

static void ui_canvas_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    static lv_coord_t last_x = -65535, last_y = -65535;

    if(code == LV_EVENT_PRESSING)
    {
        lv_indev_t * indev = lv_indev_active();
        if(indev == NULL)  return;
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        if(last_x != -65535 && last_y != -65535)
        {
            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);
            lv_draw_line_dsc_t dsc;
            lv_draw_line_dsc_init(&dsc);   
            dsc.color = ui_Draw_para.line_color; 
            dsc.width = ui_Draw_para.line_width;
            dsc.round_end = 1;
            dsc.round_start = 1;
            dsc.p1.x = last_x;
            dsc.p1.y = last_y;
            dsc.p2.x = point.x;
            dsc.p2.y = point.y;
            lv_draw_line(&layer, &dsc);
            lv_canvas_finish_layer(canvas, &layer);
        }

        last_x = point.x;
        last_y = point.y;
    }
    else if(code == LV_EVENT_RELEASED)
    {
        last_x = -32768;
        last_y = -32768;
    }
}

static void ui_event_clear_btn(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        _ui_Draw_clear();
    }
}

static void ui_event_back_btn(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_lib_pm_OpenPrePage(&page_manager);
    }
}

static void ui_event_color_btn(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * color_btn = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED)
    {
        ui_Draw_para.line_color_index += 1;
        if(ui_Draw_para.line_color_index >= UI_DRAW_COLOR_MAX)
        {
            ui_Draw_para.line_color_index = UI_DRAW_COLOR_RED;
        }
        switch(ui_Draw_para.line_color_index)
        {
            case UI_DRAW_COLOR_RED:
                ui_Draw_para.line_color = lv_palette_main(LV_PALETTE_RED);
                break;
            case UI_DRAW_COLOR_BLUE:
                ui_Draw_para.line_color = lv_palette_main(LV_PALETTE_BLUE);
                break;
            case UI_DRAW_COLOR_BLACK:
                ui_Draw_para.line_color = lv_color_black();
                break;
            default:
                break;
        }
        lv_obj_set_style_bg_color(color_btn, ui_Draw_para.line_color, 0);
    }
}

static void ui_event_width_btn(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        ui_Draw_para.line_width++;
        if(ui_Draw_para.line_width > 5)
        {
            ui_Draw_para.line_width = 2;
        }
        lv_obj_t * width_btn = lv_event_get_target(e);
        lv_obj_t * width_btn_label = lv_obj_get_child(width_btn, NULL);
        char * width_str[5];
        sprintf(width_str, "W: %d", ui_Draw_para.line_width);
        lv_label_set_text(width_btn_label, width_str);
    }
}

///////////////////// SCREEN init ////////////////////

void ui_DrawPage_init()
{
    lv_obj_t * DrawPage = lv_obj_create(NULL);

    ui_Draw_para.line_color_index = UI_DRAW_COLOR_RED;
    ui_Draw_para.line_color = lv_palette_main(LV_PALETTE_RED);
    ui_Draw_para.line_width = 2;

    canvas = lv_canvas_create(DrawPage);
    lv_canvas_set_buffer(canvas, buf_raw, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT, LV_COLOR_FORMAT_NATIVE);
    lv_obj_center(canvas);
    _ui_Draw_clear();

    lv_obj_add_event_cb(DrawPage, ui_canvas_event, LV_EVENT_ALL, NULL);

    lv_obj_t * clear_btn = lv_btn_create(DrawPage);
    lv_obj_align(clear_btn, LV_ALIGN_TOP_RIGHT, -5, 10);
    lv_obj_set_size(clear_btn, 75,40);
    lv_obj_t * clear_btn_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_btn_label,"Clear");
    lv_obj_align(clear_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(clear_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(clear_btn, ui_event_clear_btn, LV_EVENT_ALL, NULL);

    lv_obj_t * back_btn = lv_btn_create(DrawPage);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 5, 10);
    lv_obj_set_size(back_btn, 75,40);
    lv_obj_t * back_btn_label = lv_label_create(back_btn);
    lv_label_set_text(back_btn_label,"Back");
    lv_obj_align(back_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(back_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(back_btn, ui_event_back_btn, LV_EVENT_ALL, NULL);

    lv_obj_t * color_btn = lv_btn_create(DrawPage);
    lv_obj_align(color_btn, LV_ALIGN_TOP_MID, -35, 10);
    lv_obj_set_size(color_btn, 60, 40);
    lv_obj_set_style_bg_color(color_btn, ui_Draw_para.line_color, 0);
    lv_obj_t * color_btn_label = lv_label_create(color_btn);
    lv_label_set_text(color_btn_label,"Color");
    lv_obj_align(color_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(color_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(color_btn, ui_event_color_btn, LV_EVENT_ALL, NULL);

    lv_obj_t * width_btn = lv_btn_create(DrawPage);
    lv_obj_align(width_btn, LV_ALIGN_TOP_MID, 35, 10);
    lv_obj_set_size(width_btn, 60, 40);
    lv_obj_set_style_bg_color(width_btn, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_t * width_btn_label = lv_label_create(width_btn);
    char * width_str[5];
    sprintf(width_str, "W: %d", ui_Draw_para.line_width);
    lv_label_set_text(width_btn_label, width_str);
    lv_obj_align(width_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(width_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(width_btn, ui_event_width_btn, LV_EVENT_ALL, NULL);

    // load page
    lv_scr_load_anim(DrawPage, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);
}

/////////////////// SCREEN deinit ////////////////////

void ui_DrawPage_deinit()
{
    // deinit
}