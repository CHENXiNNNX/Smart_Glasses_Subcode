#ifndef _DEV_CONF_H
#define _DEV_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define LV_USE_SIMULATOR 0

#if LV_USE_SIMULATOR
    #define LV_USE_LINUX_FBDEV 0
    #define LV_USE_EVDEV 0
    #define LV_USE_SDL 1
#else
    #define LV_USE_LINUX_FBDEV 1
    #define LV_USE_EVDEV 1
    #define LV_USE_SDL 0
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif