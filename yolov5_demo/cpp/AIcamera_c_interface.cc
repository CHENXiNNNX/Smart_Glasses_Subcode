// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolov5.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#include <unistd.h>   
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>

//opencv
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "AIcamera_c_interface.h"


static int disp_width = 320;
static int disp_height = 240;

rknn_app_context_t rknn_app_ctx;
uint8_t* yolo_pic_buf; // 添加的缓冲区指针
size_t yolo_pic_buf_size; // 缓冲区大小

static pthread_t ai_camera_thread;
static int ai_camera_running = 0;
static pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;  // 保护状态访问的互斥锁
static int ai_camera_stop = 0;

void mapCoordinates(cv::Mat input, cv::Mat output, int *x, int *y) {	
	float scaleX = (float)output.cols / (float)input.cols; 
	float scaleY = (float)output.rows / (float)input.rows;
    
    *x = (int)((float)*x / scaleX);
    *y = (int)((float)*y / scaleY);
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/

static void* _inference_loop(void* model_path) {

    clock_t start_time;
    clock_t end_time;
    char text[8];
    float fps = 0;
    int ret;

    // Model Input (Yolov5)
    int model_width    = 640;
    int model_height   = 640;
    int channels = 3;
    
    // object detect result
    object_detect_result_list od_results;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    // init yolov5
    init_yolov5_model((char*)model_path, &rknn_app_ctx);
    printf("model_path: %s\n", (char*)model_path);
    init_post_process();
    
    //fb paras
    int pixel_size = 2;

    // disp
    cv::Mat disp;
    if( pixel_size == 4 )//ARGB8888
        disp = cv::Mat(disp_height, disp_width, CV_8UC3);
    else if ( pixel_size == 2 ) //RGB565
        disp = cv::Mat(disp_height, disp_width, CV_16UC1); 
    
    //Init Opencv-mobile 
    cv::VideoCapture cap;
    cv::Mat bgr(disp_height, disp_width, CV_8UC3); 
    cv::Mat bgr_model_input(model_height, model_width, CV_8UC3, rknn_app_ctx.input_mems[0]->virt_addr);
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  disp_width*2);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, disp_height*2);
    cap.open(0); 

    while(!ai_camera_stop)
    {
        start_time = clock();
        cap >> bgr;

        //letterbox       
        cv::resize(bgr, bgr_model_input, cv::Size(model_width,model_height), 0, 0, cv::INTER_LINEAR);
        inference_yolov5_model(&rknn_app_ctx, &od_results);

        // Add rectangle and probability
        for (int i = 0; i < od_results.count; i++)
        {
            object_detect_result *det_result = &(od_results.results[i]); 
            mapCoordinates(bgr, bgr_model_input, &det_result->box.left,  &det_result->box.top);
            mapCoordinates(bgr, bgr_model_input, &det_result->box.right, &det_result->box.bottom);	
            
            printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
                   det_result->box.left, det_result->box.top,
                   det_result->box.right, det_result->box.bottom,
                   det_result->prop);
            
            cv::rectangle(bgr,cv::Point(det_result->box.left ,det_result->box.top),
                              cv::Point(det_result->box.right,det_result->box.bottom),cv::Scalar(0,255,0),3);

            sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
            cv::putText(bgr,text,cv::Point(det_result->box.left, det_result->box.top - 8),
                                         cv::FONT_HERSHEY_SIMPLEX,1.5,
                                         cv::Scalar(0,255,0),1.5); 
        }
        cv::resize(bgr, bgr, cv::Size(disp_width, disp_height), 0, 0, cv::INTER_LINEAR);
        //Fps Show
        sprintf(text,"fps=%.1f",fps); 
        cv::putText(bgr,text,cv::Point(0, 20),
                    cv::FONT_HERSHEY_SIMPLEX,0.5,
                    cv::Scalar(0,255,0),1);

        //LCD Show 
        if( pixel_size == 4 ) 
            cv::cvtColor(bgr, disp, cv::COLOR_BGR2BGRA);
        else if( pixel_size == 2 )
            cv::cvtColor(bgr, disp, cv::COLOR_BGR2BGR565);
        memcpy(yolo_pic_buf, disp.data, disp_width * disp_height * pixel_size);
        //Update Fps
        end_time = clock();
        fps= (float) (CLOCKS_PER_SEC / (end_time - start_time)) ;
        //printf("%s\n",text);
        memset(text,0,8); 
    }

    deinit_post_process();

    ret = release_yolov5_model(&rknn_app_ctx);
    if (ret != 0)
    {
        printf("release_yolov5_model fail! ret=%d\n", ret);
    }

    return 0;
}

int start_ai_camera(const char* model_path) {
    pthread_mutex_lock(&running_mutex);
    if (ai_camera_running) {
        pthread_mutex_unlock(&running_mutex);
        printf("AI camera is already running.\n");
        return -1;
    }
    ai_camera_running = 1;
    pthread_mutex_unlock(&running_mutex);
    
    yolo_pic_buf_size = disp_width * disp_height * 2; // BGR565格式，每个像素占2字节
    yolo_pic_buf = (uint8_t*)malloc(yolo_pic_buf_size);

    if(pthread_create(&ai_camera_thread, NULL, _inference_loop, (void*)model_path) != 0){
        printf("Failed to create thread.\n");
        free(yolo_pic_buf);
        stop_ai_camera();
        return -1;
    }

    return 0;
}

int stop_ai_camera() {
    pthread_mutex_lock(&running_mutex);
    if (!ai_camera_running) {
        pthread_mutex_unlock(&running_mutex);
        printf("AI camera is not running.\n");
        return -1;
    }
    pthread_mutex_unlock(&running_mutex);
    ai_camera_stop = 1;

    pthread_join(ai_camera_thread, NULL); // 等待线程结束
    ai_camera_running = 0;
    ai_camera_stop = 0;

    free(yolo_pic_buf); // 释放分配的缓冲区
    
    return 0;
}

void get_buf_data(uint8_t* buffer)
{
    if (yolo_pic_buf == NULL) {
        printf("Error: yolo_pic_buf is not initialized.\n");
        return;
    }
    memcpy(buffer, yolo_pic_buf, yolo_pic_buf_size);
}

