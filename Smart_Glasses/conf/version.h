#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif


/**********************************
 * CURRENT VERSION OF Echo-Mate UI
 *********************************/
/**
 * total mechanical structure change will show in major
 * hardware change will show in minor
 * software change will show in patch
 *
 */
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 1
#define VERSION_INFO ""

/**
 * 
 * 
 */
#define VERSION_AUTHOR_ENG_NAME    "Chicken"
#define VERSION_AUTHOR_CN_NAME     "油炸鸡"
#define VERSION_PROJECT_LINK       "https://github.com/No-Chicken/Echo-Mate"


/**
 * Wrapper functions for VERSION macros
 */

static inline int echo_ui_version_major(void)
{
    return VERSION_MAJOR;
}

static inline int echo_ui_version_minor(void)
{
    return VERSION_MINOR;
}

static inline int echo_ui_version_patch(void)
{
    return VERSION_PATCH;
}

static inline const char *echo_ui_version_info(void)
{
    return VERSION_INFO;
}

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
