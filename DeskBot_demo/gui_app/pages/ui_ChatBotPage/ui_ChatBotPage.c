#include "ui_ChatBotPage.h"
#include "app_ChatBotPage.h"

///////////////////// VARIABLES ////////////////////

lv_obj_t * ui_EyesPanel;
lv_obj_t * ui_EyesVerMovePanel;
lv_obj_t * ui_QuestionImg;
lv_obj_t * ui_thinkImg;
lv_obj_t * ui_HandImg;
lv_obj_t * ui_EyeRight;
lv_obj_t * ui_EyeLeft;
lv_obj_t * ui_MouthPanel;
lv_obj_t * ui_Mouth;
lv_obj_t * ui_LabelInfo;
lv_timer_t * ui_ChatBot_timer;
lv_timer_t * ui_ChatBot_move_timer;

#define CHAT_BOT_UI_TEST 0

struct ui_chat_para_t{
    bool first_enter;
    bool anim_complete;
    int idle_random_anim_index;
    int last_state;
};

struct ui_chat_para_t ui_chat_para = {
    .first_enter = 0,
    .anim_complete = true,
    .idle_random_anim_index = 1,
    .last_state = -1,
};

///////////////////// FUNCTIONS ////////////////////

static void ui_ChatBotPage_Objs_reinit(void)
{
    lv_anim_delete_all();
    lv_obj_set_width(ui_EyesPanel, 210);
    lv_obj_set_height(ui_EyesPanel, 80);
    lv_obj_set_x(ui_EyesPanel, 0);
    lv_obj_set_y(ui_EyesPanel, -25);

    lv_obj_set_width(ui_EyesVerMovePanel, 210);
    lv_obj_set_height(ui_EyesVerMovePanel, 80);
    lv_obj_set_x(ui_EyesVerMovePanel, 0);
    lv_obj_set_y(ui_EyesVerMovePanel, 0);

    lv_obj_set_width(ui_EyeRight, 80);
    lv_obj_set_height(ui_EyeRight, 80);
    lv_obj_set_x(ui_EyeRight, 60);
    lv_obj_set_y(ui_EyeRight, 0);

    lv_obj_set_width(ui_EyeLeft, 80);
    lv_obj_set_height(ui_EyeLeft, 80);
    lv_obj_set_x(ui_EyeLeft, -60);
    lv_obj_set_y(ui_EyeLeft, 0);

    lv_obj_set_width(ui_EyeLeft, 80);
    lv_obj_set_height(ui_EyeLeft, 80);
    lv_obj_set_x(ui_EyeLeft, -60);
    lv_obj_set_y(ui_EyeLeft, 0);

    lv_obj_set_width(ui_Mouth, 60);
    lv_obj_set_height(ui_Mouth, 60);
    lv_obj_set_x(ui_Mouth, 0);
    lv_obj_set_y(ui_Mouth, -40);

    lv_obj_set_style_bg_opa(ui_Mouth, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MouthPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_opa(ui_QuestionImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_opa(ui_thinkImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_opa(ui_HandImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

///////////////////// ANIMATIONS ////////////////////

static void _anim_complete_cb(void)
{
    ui_chat_para.anim_complete = true;
}

static void _IdleMove1_Animation(void)
{
    ui_ChatBotPage_Objs_reinit();
    ui_chat_para.anim_complete = false;
    int16_t y_pos_now = -25;
    int16_t x_pos_now = 0;
    int16_t hight_now = 80;
    lv_lib_anim_user_animation(ui_EyesPanel, 0, 500, y_pos_now, y_pos_now-20, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_y, NULL);
    y_pos_now -= 20;
    lv_lib_anim_user_animation(ui_EyesPanel, 0, 500, x_pos_now, x_pos_now-20, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_x, NULL);
    x_pos_now -= 20;
    lv_lib_anim_user_animation(ui_EyesPanel, 1000, 100, hight_now, 10, 0, 100, 0, 2, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    // hight_now += 0;
    lv_lib_anim_user_animation(ui_EyesPanel, 1500, 500, x_pos_now, x_pos_now+20, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_x, NULL);
    x_pos_now += 20;
    lv_lib_anim_user_animation(ui_EyesPanel, 2000, 100, hight_now, 10, 0, 100, 0, 2, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    // hight_now += 0;
    lv_lib_anim_user_animation(ui_EyesPanel, 3000, 500, x_pos_now, x_pos_now+40, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_x, NULL);
    x_pos_now += 40;
    lv_lib_anim_user_animation(ui_EyesPanel, 3000, 500, y_pos_now, y_pos_now+40, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_y, NULL);
    y_pos_now += 40;
    lv_lib_anim_user_animation(ui_EyesPanel, 4000, 100, hight_now, 10, 0, 100, 0, 2, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    // hight_now += 0;
    lv_lib_anim_user_animation(ui_EyesPanel, 5000, 500, x_pos_now, x_pos_now-40, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_x, NULL);
    x_pos_now -= 40;
    lv_lib_anim_user_animation(ui_EyesPanel, 5000, 500, y_pos_now, y_pos_now-20, 0, 0, 0, 0, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_y, _anim_complete_cb);
    y_pos_now -= 20;
}

static void _IdleMove2_Animation(void)
{
    ui_ChatBotPage_Objs_reinit();
    ui_chat_para.anim_complete = false;
    int16_t VerPanel_y_pos_now = 0;
    int16_t MainPanel_y_pos_now = -25;
    lv_lib_anim_user_animation(ui_EyesVerMovePanel, 0, 500, VerPanel_y_pos_now, VerPanel_y_pos_now-20, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    VerPanel_y_pos_now -= 20;
    lv_lib_anim_user_animation(ui_EyesVerMovePanel, 2500, 500, VerPanel_y_pos_now, VerPanel_y_pos_now+20, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    VerPanel_y_pos_now += 20;

    lv_lib_anim_user_animation(ui_EyesPanel, 0, 500, MainPanel_y_pos_now, MainPanel_y_pos_now+20, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    MainPanel_y_pos_now += 20;
    lv_lib_anim_user_animation(ui_EyesPanel, 2500, 500, MainPanel_y_pos_now, MainPanel_y_pos_now-20, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    MainPanel_y_pos_now -= 20;

    lv_lib_anim_user_animation(ui_thinkImg, 0, 500, -100, 100, 0, 750, 0, 3, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_image_angle, NULL);
    lv_lib_anim_user_animation(ui_thinkImg, 0, 500, 0, 255, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);
    lv_lib_anim_user_animation(ui_thinkImg, 3000, 500, 255, 0, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, _anim_complete_cb);
}

static void _ListenMove_Animation(void)
{
    ui_ChatBotPage_Objs_reinit();
    ui_chat_para.anim_complete = false;
    int16_t eye_width_now = 80;
    int16_t eye_hight_now = 80;
    lv_lib_anim_user_animation(ui_EyeRight, 0, 100, eye_width_now, eye_width_now-30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 0, 100, eye_width_now, eye_width_now-30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    eye_width_now -= 30;
    lv_lib_anim_user_animation(ui_QuestionImg, 0, 500, 0, 255, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);
    lv_lib_anim_user_animation(ui_QuestionImg, 0, 500, -100, 100, 0, 750, 0, 4, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_image_angle, NULL);

    lv_lib_anim_user_animation(ui_EyeRight, 1000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 1000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeRight, 1000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 1000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);

    lv_lib_anim_user_animation(ui_EyeRight, 2000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 2000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeRight, 2000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 2000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);

    lv_lib_anim_user_animation(ui_QuestionImg, 3500, 500, 255, 0, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);

    lv_lib_anim_user_animation(ui_EyeRight, 3000, 100, eye_width_now, eye_width_now+30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 3000, 100, eye_width_now, eye_width_now+30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, _anim_complete_cb);
    eye_width_now += 30;
}

static void _ThinkingMove_Animation(void)
{
    ui_ChatBotPage_Objs_reinit();
    ui_chat_para.anim_complete = false;

    lv_lib_anim_user_animation(ui_HandImg, 0, 500, 0, 255, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);
    lv_lib_anim_user_animation(ui_HandImg, 0, 500, -250, -150, 0, 750, 0, 2, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_image_angle, NULL);
    lv_lib_anim_user_animation(ui_QuestionImg, 1500, 500, 255, 0, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);
    lv_lib_anim_user_animation(ui_HandImg, 1500, 500, 255, 0, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);

    int16_t eye_width_now = 80;
    int16_t eye_hight_now = 80;
    lv_lib_anim_user_animation(ui_EyeRight, 0, 100, eye_width_now, eye_width_now-30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 0, 100, eye_width_now, eye_width_now-30, 0, 0, 0, 0, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    eye_width_now -= 30;
    lv_lib_anim_user_animation(ui_QuestionImg, 0, 500, 0, 255, 0, 0, 0, 0, lv_anim_path_linear, lv_lib_anim_callback_set_opacity, NULL);
    lv_lib_anim_user_animation(ui_QuestionImg, 0, 500, -100, 100, 0, 750, 0, 4, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_image_angle, NULL);

    lv_lib_anim_user_animation(ui_EyeRight, 1000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 1000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeRight, 1000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 1000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);

    lv_lib_anim_user_animation(ui_EyeRight, 2000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 2000, 100, eye_width_now, eye_width_now+30, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_width, NULL);
    lv_lib_anim_user_animation(ui_EyeRight, 2000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, NULL);
    lv_lib_anim_user_animation(ui_EyeLeft, 2000, 100, eye_hight_now, eye_hight_now-70, 0, 100, 0, 1, lv_anim_path_ease_out, lv_lib_anim_callback_set_hight, _anim_complete_cb);
}

static void _SpeakMove_Animation(void)
{
    ui_ChatBotPage_Objs_reinit();
    ui_chat_para.anim_complete = false;
    int16_t mouth_panel_y_pos_now = 95;
    int16_t mouth_y_pos_now = -40;
    int16_t eye_panel_hight_now = 80;
    lv_obj_set_style_bg_opa(ui_Mouth, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_lib_anim_user_animation(ui_Mouth, 0, 150, mouth_y_pos_now, mouth_y_pos_now-10, 0, 150, 0, 6, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    lv_lib_anim_user_animation(ui_MouthPanel, 0, 150, mouth_panel_y_pos_now, mouth_panel_y_pos_now+10, 0, 150, 0, 6, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    // blink
    lv_lib_anim_user_animation(ui_EyesPanel, 500, 200, eye_panel_hight_now, 10, 0, 200, 0, 1, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_hight, NULL);
    lv_lib_anim_user_animation(ui_EyesPanel, 2000, 200, eye_panel_hight_now, 10, 0, 200, 0, 1, lv_anim_path_ease_in_out, lv_lib_anim_callback_set_hight, NULL);

    lv_lib_anim_user_animation(ui_Mouth, 1500, 150, mouth_y_pos_now, mouth_y_pos_now-10, 0, 150, 0, 4, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, NULL);
    lv_lib_anim_user_animation(ui_MouthPanel, 1500, 150, mouth_panel_y_pos_now, mouth_panel_y_pos_now+10, 0, 150, 0, 4, lv_anim_path_ease_out, lv_lib_anim_callback_set_y, _anim_complete_cb);

}

///////////////////// FUNCTIONS ////////////////////

static void ui_event_ChatBotPage(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
    }
    if(event_code == LV_EVENT_GESTURE &&  lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        ui_ChatBotPage_Objs_reinit();
        if(!CHAT_BOT_UI_TEST)
            stop_ai_chat();
        lv_lib_pm_OpenPrePage(&page_manager);
    }
}

static int ui_ai_chat_app_init(void)
{
    if(!CHAT_BOT_UI_TEST) 
    {
        int errno = start_ai_chat(ui_system_para.aichat_app_info.addr, ui_system_para.aichat_app_info.port, ui_system_para.aichat_app_info.token, ui_system_para.aichat_app_info.device_id, ui_system_para.aichat_app_info.aliyun_api_key, ui_system_para.aichat_app_info.protocol_version, ui_system_para.aichat_app_info.sample_rate, ui_system_para.aichat_app_info.channels, ui_system_para.aichat_app_info.frame_duration);
        if(errno)
        {
            // show msg box
            ui_msgbox_info("Error", "AIChat App init failed, wait for a moment and try again.");
            return -1;
        }
    }
    return 0;
}

static void _ChatBotTimer_cb(void)
{
    if(ui_chat_para.first_enter)
    {
        ui_chat_para.first_enter = false;
        // start AI Chat
        if(ui_ai_chat_app_init())
        {
            lv_lib_pm_OpenPrePage(&page_manager);
        }
    }
    
    // 0-fault, 1-startup, 2-stop, 3-idle, 4-listening, 5-thinking, 6-speaking
    int state;
    if(!CHAT_BOT_UI_TEST) 
        state = get_ai_chat_state();
    else
        state = 3;
    
    if(state != ui_chat_para.last_state)
    {
        ui_chat_para.last_state = state;
        ui_chat_para.anim_complete = true;
        ui_ChatBotPage_Objs_reinit();
    }
    if(state == -1 || state == 2)
    {
        // show msg box
        ui_msgbox_info("Error", "AIChat App Not exist.");
        lv_lib_pm_OpenPrePage(&page_manager);
    }
    else if(state==0)
    {
        lv_obj_remove_flag(ui_LabelInfo, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_LabelInfo, "Fault");
    }
    else if(state==1)
    {
        lv_obj_remove_flag(ui_LabelInfo, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_LabelInfo, "Starting ...");
    }
    else
    {
        lv_obj_add_flag(ui_LabelInfo, LV_OBJ_FLAG_HIDDEN);
        if(ui_chat_para.anim_complete)
        {
            // idle
            if(state == 3)
            {
                if(ui_chat_para.idle_random_anim_index == 1)
                {
                    _IdleMove1_Animation();
                    ui_chat_para.idle_random_anim_index += 1;
                }
                else if(ui_chat_para.idle_random_anim_index == 2)
                {
                    _IdleMove2_Animation();
                    ui_chat_para.idle_random_anim_index = 1;
                }  
            }
            // listening
            else if(state == 4)
            {
                _ListenMove_Animation();
            }
            //thinking
            else if(state == 5)
            {
                _ThinkingMove_Animation();
            }
            // speaking
            else if(state == 6)
            {
                _SpeakMove_Animation();
            }
        }
    }
}

static void _ChatBotMoveTimer_cb(void)
{
    if(!CHAT_BOT_UI_TEST)
        chat_bot_get_intent_process();
}

///////////////////// SCREEN init ////////////////////

void ui_ChatBotPage_init(void)
{
    ui_chat_para.first_enter = true;
    lv_obj_t * ui_ChatBotPage = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_ChatBotPage, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_EyesPanel = lv_obj_create(ui_ChatBotPage);
    lv_obj_set_width(ui_EyesPanel, 210);
    lv_obj_set_height(ui_EyesPanel, 80);
    lv_obj_set_x(ui_EyesPanel, 0);
    lv_obj_set_y(ui_EyesPanel, -25);
    lv_obj_set_align(ui_EyesPanel, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_EyesPanel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_EyesPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_EyesPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_EyesPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_EyesPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_EyesVerMovePanel = lv_obj_create(ui_EyesPanel);
    lv_obj_set_width(ui_EyesVerMovePanel, 210);
    lv_obj_set_height(ui_EyesVerMovePanel, 80);
    lv_obj_set_align(ui_EyesVerMovePanel, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_EyesVerMovePanel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_EyesVerMovePanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_EyesVerMovePanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_EyesVerMovePanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_EyesVerMovePanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_EyeRight = lv_button_create(ui_EyesVerMovePanel);
    lv_obj_set_width(ui_EyeRight, 80);
    lv_obj_set_height(ui_EyeRight, 80);
    lv_obj_set_x(ui_EyeRight, 60);
    lv_obj_set_y(ui_EyeRight, 0);
    lv_obj_set_align(ui_EyeRight, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_EyeRight, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_remove_flag(ui_EyeRight, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_EyeRight, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_EyeRight, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_EyeRight, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_EyeLeft = lv_button_create(ui_EyesVerMovePanel);
    lv_obj_set_width(ui_EyeLeft, 80);
    lv_obj_set_height(ui_EyeLeft, 80);
    lv_obj_set_x(ui_EyeLeft, -60);
    lv_obj_set_y(ui_EyeLeft, 0);
    lv_obj_set_align(ui_EyeLeft, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_EyeLeft, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_remove_flag(ui_EyeLeft, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_EyeLeft, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_EyeLeft, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_EyeLeft, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MouthPanel = lv_obj_create(ui_ChatBotPage);
    lv_obj_set_width(ui_MouthPanel, 80);
    lv_obj_set_height(ui_MouthPanel, 80);
    lv_obj_set_x(ui_MouthPanel, 0);
    lv_obj_set_y(ui_MouthPanel, 95);
    lv_obj_set_align(ui_MouthPanel, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_MouthPanel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_MouthPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MouthPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MouthPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MouthPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Mouth = lv_button_create(ui_MouthPanel);
    lv_obj_set_width(ui_Mouth, 60);
    lv_obj_set_height(ui_Mouth, 60);
    lv_obj_set_x(ui_Mouth, 0);
    lv_obj_set_y(ui_Mouth, -40);
    lv_obj_set_align(ui_Mouth, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Mouth, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_remove_flag(ui_Mouth, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Mouth, 80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Mouth, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Mouth, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_QuestionImg = lv_image_create(ui_ChatBotPage);
    lv_image_set_src(ui_QuestionImg, &ui_img_question60_png);
    lv_obj_set_width(ui_QuestionImg, LV_SIZE_CONTENT);   /// 60
    lv_obj_set_height(ui_QuestionImg, LV_SIZE_CONTENT);    /// 60
    lv_obj_set_x(ui_QuestionImg, 125);
    lv_obj_set_y(ui_QuestionImg, -80);
    lv_obj_set_align(ui_QuestionImg, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_QuestionImg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_opa(ui_QuestionImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_thinkImg = lv_image_create(ui_ChatBotPage);
    lv_image_set_src(ui_thinkImg, &ui_img_think60_png);
    lv_obj_set_width(ui_thinkImg, LV_SIZE_CONTENT);   /// 60
    lv_obj_set_height(ui_thinkImg, LV_SIZE_CONTENT);    /// 60
    lv_obj_set_x(ui_thinkImg, 120);
    lv_obj_set_y(ui_thinkImg, -80);
    lv_obj_set_align(ui_thinkImg, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_thinkImg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_obj_set_style_opa(ui_thinkImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_HandImg = lv_image_create(ui_ChatBotPage);
    lv_image_set_src(ui_HandImg, &ui_img_hand60_png);
    lv_obj_set_width(ui_HandImg, LV_SIZE_CONTENT);   /// 64
    lv_obj_set_height(ui_HandImg, LV_SIZE_CONTENT);    /// 64
    lv_obj_set_x(ui_HandImg, 0);
    lv_obj_set_y(ui_HandImg, 55);
    lv_obj_set_align(ui_HandImg, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_HandImg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);      /// Flags
    lv_image_set_rotation(ui_HandImg, -350);
    lv_obj_set_style_opa(ui_HandImg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_LabelInfo = lv_label_create(ui_ChatBotPage);
    lv_obj_set_width(ui_LabelInfo, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_LabelInfo, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_LabelInfo, 0);
    lv_obj_set_y(ui_LabelInfo, 10);
    lv_obj_set_align(ui_LabelInfo, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_LabelInfo, "Wait connect ...");
    lv_obj_set_style_text_color(ui_LabelInfo, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LabelInfo, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelInfo, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_LabelInfo, LV_OBJ_FLAG_HIDDEN);     /// Flags

    lv_obj_add_event_cb(ui_ChatBotPage, ui_event_ChatBotPage, LV_EVENT_ALL, NULL);

    ui_ChatBot_timer = lv_timer_create(_ChatBotTimer_cb, 250, NULL);
    ui_ChatBot_move_timer = lv_timer_create(_ChatBotMoveTimer_cb, 500, NULL);

    // load page
    lv_scr_load_anim(ui_ChatBotPage, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);

}

/////////////////// SCREEN deinit ////////////////////

void ui_ChatBotPage_deinit(void)
{
    if(ui_ChatBot_timer)
    {
        lv_timer_delete(ui_ChatBot_timer);
    }    
     if(ui_ChatBot_move_timer)
    {
        lv_timer_delete(ui_ChatBot_move_timer);
    }
    ui_chat_para.anim_complete = true;
    return;
}
