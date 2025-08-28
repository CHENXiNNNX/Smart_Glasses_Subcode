#include "ui_CalculatorPage.h"
#include "app_CalculatorPage.h"

///////////////////// VARIABLES ////////////////////

#define TEXT_FULL 10
StrStack_t CalStr;
NumStack_t NumStack;
SymStack_t SymStack;
static const char * ui_ComPageBtnmap[] ={"1", "2", "3", "+", "\n",
                                         "4", "5", "6", "-", "\n",
                                         "7", "8", "9", "×", "\n",
                                         ".", "0", "=", "÷", ""};
lv_obj_t * ui_CompageBtnM;
///////////////////// ANIMATIONS ////////////////////


///////////////////// FUNCTIONS ////////////////////

static void ui_enent_Gesture(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_GESTURE)
    {
        if(lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT || lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
        {
            lv_lib_pm_OpenPrePage(&page_manager);
        }
    }
}

void ui_CompageBtnM_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    lv_obj_t * ui_CompageTextarea = lv_event_get_user_data(e);
    if(code == LV_EVENT_DRAW_TASK_ADDED)
    {
        lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
        lv_draw_dsc_base_t * base_dsc = lv_draw_task_get_draw_dsc(draw_task);
        if(base_dsc->part == LV_PART_ITEMS)
        {
            if(base_dsc->id1 >= 0)
            {
                lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
                if(base_dsc->id1 == 3 || base_dsc->id1 == 7 || base_dsc->id1 == 11 || base_dsc->id1 == 14 || base_dsc->id1 == 15)
                {
                    if(fill_draw_dsc)
                    {
                        fill_draw_dsc->radius = LV_RADIUS_CIRCLE;
                        if (lv_btnmatrix_get_selected_btn(obj) == base_dsc->id1)
                        {
                            fill_draw_dsc->color = lv_palette_darken(LV_PALETTE_BLUE,3);
                            //lv_btnmatrix_set_selected_btn(ui_CompageBtnM,NULL);
                        }
                        else
                            fill_draw_dsc->color = lv_palette_main(LV_PALETTE_BLUE);
                    }
                }
            }
        }
    }
    if(code == LV_EVENT_VALUE_CHANGED)
    {
        uint16_t btn_id = lv_btnmatrix_get_selected_btn(obj); // 获取当前选中的按键的id
        const char * txt = lv_btnmatrix_get_btn_text(obj, btn_id); // 获取当前按键的文本

        if (txt != NULL)
        {
            if (ui_CompageTextarea != NULL)
            {
                if(lv_textarea_get_cursor_pos(ui_CompageTextarea) <= TEXT_FULL)
                {
                    lv_textarea_add_text(ui_CompageTextarea, txt); // 文本框追加字符
                    switch(btn_id)
                    {
                        case 0:
                                strput(&CalStr,'1');
                                break;
                        case 1:
                                strput(&CalStr,'2');
                                break;
                        case 2:
                                strput(&CalStr,'3');
                                break;
                        case 3:
                                strput(&CalStr,'+');
                                break;
                        case 4:
                                strput(&CalStr,'4');
                                break;
                        case 5:
                                strput(&CalStr,'5');
                                break;
                        case 6:
                                strput(&CalStr,'6');
                                break;
                        case 7:
                                strput(&CalStr,'-');
                                break;
                        case 8:
                                strput(&CalStr,'7');
                                break;
                        case 9:
                                strput(&CalStr,'8');
                                break;
                        case 10:
                                strput(&CalStr,'9');
                                break;
                        case 11:
                                strput(&CalStr,'*');
                                break;
                        case 12:
                                strput(&CalStr,'.');
                                break;
                        case 13:
                                strput(&CalStr,'0');
                                break;
                        case 14:
                                strput(&CalStr,'=');
                                lv_textarea_add_text(ui_CompageTextarea,"\n");
                                strput(&CalStr,'\n');
                                break;
                        case 15:
                                strput(&CalStr,'/');
                                break;
                    }
                }
            }
        }

        if(lv_btnmatrix_get_selected_btn(obj) == 14)
        {
            //calculate
            if(StrCalculate(CalStr.strque,&NumStack,&SymStack))
            {lv_textarea_add_text(ui_CompageTextarea,"erro");}
            else
            {
                char strout[10];
                if(isIntNumber(NumStack.data[NumStack.Top_Point-1]))
                {sprintf(strout,"%.0f",NumStack.data[NumStack.Top_Point-1]);}
                else
                {sprintf(strout,"%.4f",NumStack.data[NumStack.Top_Point-1]);}
                lv_textarea_add_text(ui_CompageTextarea,strout);
            }
            strclear(&CalStr);
            lv_obj_clear_flag(ui_CompageBtnM,LV_OBJ_FLAG_CLICKABLE);
        }
    }
}


void ui_CompageBackBtn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    lv_obj_t * ui_CompageTextarea = lv_event_get_user_data(e);
    if(code == LV_EVENT_CLICKED)
    {
        if (ui_CompageTextarea != NULL)
        {
            if(!strstack_isEmpty(&CalStr))
            {
                lv_textarea_delete_char(ui_CompageTextarea);
                strdel(&CalStr);
            }
            else
            {
                int i = 0;
                for (i = 0; i < (TEXT_FULL*2); i++)
                {
                    lv_textarea_delete_char(ui_CompageTextarea);
                }
                lv_obj_add_flag(ui_CompageBtnM,LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
		if(code == LV_EVENT_LONG_PRESSED)
		{
			if (ui_CompageTextarea != NULL)
			{
				if(!strstack_isEmpty(&CalStr))
				{
                    strclear(&CalStr);
                    int i = 0;
                    for (i = 0; i < (TEXT_FULL*2); i++)
                    {
                        lv_textarea_delete_char(ui_CompageTextarea);
                    }
				}
			}
		}
}

///////////////////// SCREEN init ////////////////////

void ui_CalculatorPage_init(void)
{
    strclear(&CalStr);
    NumStackClear(&NumStack);
    SymStackClear(&SymStack);
    lv_obj_t * ui_CalculatorPage = lv_obj_create(NULL);

    lv_obj_clear_flag(ui_CalculatorPage,LV_OBJ_FLAG_SCROLLABLE);
    ui_CompageBtnM = lv_btnmatrix_create(ui_CalculatorPage);
    lv_btnmatrix_set_map(ui_CompageBtnM, ui_ComPageBtnmap);

    lv_obj_set_style_text_font(ui_CompageBtnM, &ui_font_heiti24, 0);
    lv_btnmatrix_set_one_checked(ui_CompageBtnM,true);
    int i = 0;
    for (i = 0; i < 16; i++)
    {
        lv_btnmatrix_set_btn_ctrl(ui_CompageBtnM, i, LV_BTNMATRIX_CTRL_NO_REPEAT); // 长按按钮时禁用重复
    }
    lv_obj_clear_flag(ui_CompageBtnM, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_border_width(ui_CompageBtnM,0,0);
    lv_obj_set_style_bg_opa(ui_CompageBtnM,0,0);
    lv_obj_set_size(ui_CompageBtnM,240,240);
    lv_obj_set_align(ui_CompageBtnM,LV_ALIGN_LEFT_MID);


    lv_obj_t * ui_CompageTextarea = lv_textarea_create(ui_CalculatorPage);
    lv_textarea_set_one_line(ui_CompageTextarea, false); // 将文本区域配置为一行
    //lv_textarea_set_password_mode(obj_text_area, true); // 将文本区域配置为密码模式
    lv_textarea_set_max_length(ui_CompageTextarea, TEXT_FULL*2); // 设置文本区域可输入的字符长度最大值
    lv_obj_add_state(ui_CompageTextarea, LV_STATE_FOCUSED); // 显示光标
    lv_obj_set_style_radius(ui_CompageTextarea, 0, 0); // 设置样式的圆角弧度
    lv_obj_set_style_border_width(ui_CompageTextarea, 0, 0); //设置边框宽度
    lv_obj_set_style_bg_color(ui_CompageTextarea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_CompageTextarea, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(ui_CompageTextarea, 100, 240); // 设置对象大小
    lv_obj_align(ui_CompageTextarea, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_clear_flag(ui_CompageTextarea,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(ui_CompageTextarea, &ui_font_heiti24, 0);
    lv_textarea_set_align(ui_CompageTextarea, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t * ui_CompageBackBtn = lv_btn_create(ui_CalculatorPage);
    lv_obj_align(ui_CompageBackBtn,LV_ALIGN_RIGHT_MID,-10,-110);
    lv_obj_set_width(ui_CompageBackBtn,50);
    lv_obj_set_height(ui_CompageBackBtn,50);
    lv_obj_set_style_radius(ui_CompageBackBtn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_CompageBackBtn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_CompageBackBtn, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    lv_obj_set_style_bg_color(ui_CompageBackBtn, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btnlabel = lv_label_create(ui_CompageBackBtn);
    lv_label_set_text(btnlabel, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(btnlabel, &lv_font_montserrat_24, 0);
    lv_obj_center(btnlabel);
    
    // event
    lv_obj_add_event_cb(ui_CalculatorPage, ui_enent_Gesture, LV_EVENT_ALL, NULL);

    lv_obj_add_flag(ui_CompageBtnM, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(ui_CompageBtnM, ui_CompageBtnM_event_cb, LV_EVENT_ALL, ui_CompageTextarea);
    lv_obj_add_event_cb(ui_CompageBackBtn, ui_CompageBackBtn_event_cb, LV_EVENT_ALL, ui_CompageTextarea);

    // load page
    lv_scr_load_anim(ui_CalculatorPage, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);

}

/////////////////// SCREEN deinit ////////////////////

void ui_CalculatorPage_deinit(void)
{
    
}