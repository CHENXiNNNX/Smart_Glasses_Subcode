#ifndef AICAMERA_C_INTERFACE_H
#define AICAMERA_C_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

int start_ai_camera(const char* model_path);
int stop_ai_camera();

void get_buf_data(uint8_t* buffer);

#ifdef __cplusplus
}
#endif

#endif // _C_INTERFACE_H