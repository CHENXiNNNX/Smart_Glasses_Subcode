#include "sys_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h> // For setting system time
#include <json-c/json.h>
#include <curl/curl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h> // for fcntl
#include <errno.h>
#if LV_USE_SIMULATOR == 0 
    #include <alsa/asoundlib.h>
    #define BRIGHTNESS_PATH "/sys/class/backlight/backlight/brightness"
    // 给定的亮度级别数组
    const int brightness_levels[] = {
        0, 3, 5, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 34, 37, 40, 42, 45, 48, 50,
        52, 54, 56, 58, 61, 64, 66, 69, 72, 74, 77, 80, 82, 85, 88, 90, 93, 96, 98,
        101, 104, 106, 109, 112, 114, 117, 120, 122, 125, 128, 130, 133, 136, 138,
        141, 144, 146, 149, 152, 154, 157, 160, 162, 165, 168, 170, 173, 176, 178,
        180, 182, 184, 186, 188, 190, 192, 194, 197, 200, 202, 205, 208, 210, 213,
        216, 218, 221, 224, 226, 229, 232, 234, 237, 240, 242, 245, 248, 250, 253, 255
    };
    const int num_brightness_levels = 100;
#endif

#define NTP_PORT 123
#define NTP_TIMESTAMP_DELTA 2208988800ull // 时间戳差值，从1900年到1970年的秒数

const char * sys_config_path = "./system_para.conf"; // 系统参数配置文件路径与可执行文件同目录
const char * city_adcode_path = "./gaode_adcode.json"; // 城市adcode对应表文件路径与可执行文件同目录

// 设置背光亮度
int sys_set_lcd_brightness(int brightness) {
#if LV_USE_SIMULATOR == 0
    if (brightness < 0 || brightness > 100) return -1;
    if (brightness < 10) brightness = 10; // 亮度太低可能导致屏幕无法显示
    if (brightness > 95) brightness = 95;
    int fd = open(BRIGHTNESS_PATH, O_WRONLY);
    if (fd == -1) {
        perror("Failed to open brightness file for writing");
        return -1;
    }

    char buffer[8]; // 应该足够存储任何可能的亮度值
    int n = snprintf(buffer, sizeof(buffer), "%d", brightness);

    if (write(fd, buffer, n) == -1) {
        perror("Failed to write brightness value");
        close(fd);
        return -1;
    }

    close(fd);
#endif
    return 0;
}


// 设置音量
int sys_set_volume(int level) {
    if (level < 0 || level > 100) return -1; // 音量级别应在0到100之间
    // 这里可以添加实际设置硬件音量的代码
#if LV_USE_SIMULATOR == 0
    const char *card = "hw:0";       // 声卡名称
    const char *selem_name = "DAC LINEOUT"; // 控件名称
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;
    snd_mixer_elem_t *elem;
    long min, max, mapped_volume;

    // 打开混音器
    if (snd_mixer_open(&handle, 0) < 0) {
        fprintf(stderr, "Error: Unable to open mixer.\n");
        return -1;
    }

    // 加载指定声卡
    if (snd_mixer_attach(handle, card) < 0) {
        fprintf(stderr, "Error: Unable to attach to card '%s'.\n", card);
        snd_mixer_close(handle);
        return -1;
    }

    // 注册混音器
    if (snd_mixer_selem_register(handle, NULL, NULL) < 0) {
        fprintf(stderr, "Error: Unable to register mixer.\n");
        snd_mixer_close(handle);
        return -1;
    }

    // 加载混音器元素
    if (snd_mixer_load(handle) < 0) {
        fprintf(stderr, "Error: Unable to load mixer.\n");
        snd_mixer_close(handle);
        return -1;
    }

    // 创建混音器元素 ID
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0); // 默认索引为 0
    snd_mixer_selem_id_set_name(sid, selem_name);

    // 查找对应元素
    elem = snd_mixer_find_selem(handle, sid);
    if (!elem) {
        fprintf(stderr, "Error: Unable to find element '%s'.\n", selem_name);
        snd_mixer_close(handle);
        return -1;
    }

    // 获取音量范围
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    printf("Volume range: %ld to %ld\n", min, max);

    // 映射音量值到实际范围
    mapped_volume = min + (long)((double)(max - min) * level / 100.0);

    // 设置音量
    if (snd_mixer_selem_set_playback_volume_all(elem, mapped_volume) < 0) {
        fprintf(stderr, "Error: Unable to set volume.\n");
        snd_mixer_close(handle);
        return -1;
    }

    printf("Set '%s' volume to %ld (mapped from %d%%)\n", selem_name, mapped_volume, level);

    // 关闭混音器
    snd_mixer_close(handle);
#endif
    return 0;
}

// 判断是否是闰年
int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

// 验证日期是否有效
int validate_date(int year, int month, int day) {
    // 每个月的最大天数
    int days_in_month[] = { 31, 28 + is_leap_year(year), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > days_in_month[month - 1]) {
        return -1;
    }
    return 0;
}

int sys_set_time(int year, int month, int day, int hour, int minute, int second) {

#if LV_USE_SIMULATOR == 0
    struct timeval tv;
    struct tm tm_set;

    // 验证日期有效性
    if (validate_date(year, month, day) != 0) {
        fprintf(stderr, "Invalid date.\n");
        return -1;
    }

    // 初始化tm结构体
    memset(&tm_set, 0, sizeof(struct tm));
    tm_set.tm_year = year - 1900; // 年份从1900年起计算
    tm_set.tm_mon = month - 1;    // 月份从0起计算
    tm_set.tm_mday = day;
    tm_set.tm_hour = hour;
    tm_set.tm_min = minute;
    tm_set.tm_sec = second;

    // 将tm转换为time_t类型
    time_t t = mktime(&tm_set);
    if (t == -1) {
        perror("mktime failed");
        return -1;
    }

    // 初始化timeval结构体
    tv.tv_sec = t;
    tv.tv_usec = 0; // 设置微秒部分为0

    // 设置系统时间
    if (settimeofday(&tv, NULL) == -1) {
        // 输出具体的错误信息
        perror("settimeofday failed");
        return -1;
    }
#endif
    printf("System time has been successfully updated.\n");
    return 0;
}

void sys_get_time(int *year, int *month, int *day, int *hour, int *minute, int *second) {
    time_t rawtime;
    struct tm * timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    if (year) *year = timeinfo->tm_year + 1900;
    if (month) *month = timeinfo->tm_mon + 1;
    if (day) *day = timeinfo->tm_mday;
    if (hour) *hour = timeinfo->tm_hour;
    if (minute) *minute = timeinfo->tm_min;
    if (second) *second = timeinfo->tm_sec;
}

// 使用蔡勒公式计算星期几，0代表周日，1代表周一，...，6代表周六
int sys_get_day_of_week(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year -= 1;
    }

    int K = year % 100;
    int J = year / 100;

    // 蔡勒公式
    int f = day + ((13 * (month + 1)) / 5) + K + (K / 4) + (J / 4) + (5 * J);
    int dayOfWeek = f % 7;

    // 根据蔡勒公式的定义调整返回值以匹配常见的一周起始日(0=周日, 1=周一, ..., 6=周六)
    return (dayOfWeek + 1) % 7;
}

bool is_internet_reachable(void) {
    int sockfd;
    struct sockaddr_in servaddr;

    // 创建socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return false;
    }

    // 设置socket为非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set socket as non-blocking");
        close(sockfd);
        return false;
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(53); // Google DNS服务端口
    servaddr.sin_addr.s_addr = inet_addr("8.8.8.8"); // Google Public DNS IP地址

    // 尝试连接
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sockfd);
            return false;
        }

        // 使用select等待连接完成或超时
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sockfd, &writefds);

        struct timeval timeout;
        timeout.tv_sec = 2; // 超时时间为2秒
        timeout.tv_usec = 0;

        int ret = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
        if (ret == 0) { // 超时
            fprintf(stderr, "Connection timed out\n");
            close(sockfd);
            return false;
        } else if (ret < 0) { // 错误发生
            perror("Select failed");
            close(sockfd);
            return false;
        }

        // 检查是否成功连接
        int so_error;
        socklen_t len = sizeof(so_error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            if (so_error != 0) {
                errno = so_error;
            }
            perror("Connect failed");
            close(sockfd);
            return false;
        }
    }

    close(sockfd);
    return true;
}

bool sys_get_wifi_status(void) {

    // 检查网络是否可达
    if (!is_internet_reachable()) {
        return false;
    }

    return true;
}

// 递归查找 adcode 对应的城市名称
const char* find_city_name(struct json_object *districts, const char *target_adcode) {
    if (!districts || !json_object_is_type(districts, json_type_array)) {
        return NULL;
    }

    size_t array_len = json_object_array_length(districts);
    for (size_t i = 0; i < array_len; i++) {
        struct json_object *district = json_object_array_get_idx(districts, i);
        if (!district) continue;

        struct json_object *adcode_obj, *name_obj, *sub_districts_obj;

        // 获取 adcode 和 name
        if (json_object_object_get_ex(district, "adcode", &adcode_obj) &&
            json_object_object_get_ex(district, "name", &name_obj) &&
            json_object_is_type(adcode_obj, json_type_string) &&
            json_object_is_type(name_obj, json_type_string)) {
            
            const char *adcode_str = json_object_get_string(adcode_obj);
            if (strcmp(adcode_str, target_adcode) == 0) {
                return json_object_get_string(name_obj);
            }
        }

        // 递归查找子地区
        if (json_object_object_get_ex(district, "districts", &sub_districts_obj)) {
            const char *result = find_city_name(sub_districts_obj, target_adcode);
            if (result) {
                return result;
            }
        }
    }

    return NULL;
}

// 解析 JSON 文件并查找 adcode
const char* sys_get_city_name_by_adcode(const char *filepath, const char *target_adcode) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "Error opening file: %s\n", filepath);
        return NULL;
    }

    // 读取整个 JSON 文件内容
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    char *json_data = (char *)malloc(file_size + 1);
    if (!json_data) {
        fclose(fp);
        fprintf(stderr, "Memory allocation failed!\n");
        return NULL;
    }

    fread(json_data, 1, file_size, fp);
    json_data[file_size] = '\0';
    fclose(fp);

    // 解析 JSON
    struct json_object *root = json_tokener_parse(json_data);
    free(json_data);  // 释放 JSON 读取的内存
    if (!root) {
        fprintf(stderr, "Error parsing JSON file!\n");
        return NULL;
    }

    struct json_object *districts;
    if (!json_object_object_get_ex(root, "districts", &districts)) {
        fprintf(stderr, "Invalid JSON format: missing 'districts' array.\n");
        json_object_put(root);  // 释放 JSON 对象
        return NULL;
    }

    // 递归查找
    const char *result = find_city_name(districts, target_adcode);

    // 释放 JSON 对象
    json_object_put(root);
    
    return result;
}

// 回调函数用于处理CURL接收到的数据
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char** response_string = (char**)userp;

    char* new_string = realloc(*response_string, realsize + 1);
    if(new_string == NULL) {
        // 内存分配失败
        fprintf(stderr, "Failed to allocate memory\n");
        return 0;
    }

    *response_string = new_string;
    memcpy(*response_string + strlen(*response_string), contents, realsize);
    (*response_string)[realsize] = '\0';

    return realsize;
}

// 使用高德地图API根据IP地址获取自动定位信息
int sys_get_auto_location_by_ip(LocationInfo_t* location, const char *api_key) {
    CURL* curl_handle;
    CURLcode res;
    char url[256];
    snprintf(url, sizeof(url), "https://restapi.amap.com/v3/ip?key=%s", api_key);

    char* response_string = malloc(1); // 初始化为空字符串
    if (!response_string) {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }
    response_string[0] = '\0';

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (!curl_handle) {
        fprintf(stderr, "Failed to initialize CURL\n");
        free(response_string);
        curl_global_cleanup();
        return -1;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl_handle, CURLOPT_CAINFO, "cacert.pem");

    // 设置超时时间为5秒
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);

    res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(response_string);
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -1;
    }

    struct json_object *parsed_json = json_tokener_parse(response_string);
    if (!parsed_json) {
        printf("Failed to parse JSON\n");
        free(response_string);
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -1;
    }

    struct json_object *province_obj, *city_obj, *adcode_obj;
    json_object_object_get_ex(parsed_json, "province", &province_obj);
    json_object_object_get_ex(parsed_json, "city", &city_obj);
    json_object_object_get_ex(parsed_json, "adcode", &adcode_obj);

    // 检查province, city, adcode是否为空数组
    if(json_object_is_type(province_obj, json_type_array) && json_object_array_length(province_obj) == 0 &&
       json_object_is_type(city_obj, json_type_array) && json_object_array_length(city_obj) == 0 &&
       json_object_is_type(adcode_obj, json_type_array) && json_object_array_length(adcode_obj) == 0) {
        printf("Location information is empty. This might be due to an invalid or foreign IP address.\n");
        json_object_put(parsed_json); // 释放JSON对象
        free(response_string);
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -2; // 自定义错误码表示位置信息为空
    }

    strncpy(location->city, json_object_get_string(city_obj), sizeof(location->city) - 1);
    location->city[sizeof(location->city) - 1] = '\0'; // 确保字符串以null结尾
    strncpy(location->adcode, json_object_get_string(adcode_obj), sizeof(location->adcode) - 1);
    location->adcode[sizeof(location->adcode) - 1] = '\0'; // 确保字符串以null结尾

    printf("City: %s\n", location->city);
    printf("Adcode: %s\n", location->adcode);

    json_object_put(parsed_json); // 释放JSON对象

    free(response_string);
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return 0;
}

int sys_get_time_from_ntp(const char* ntp_server, int *year, int *month, int *day, int *hour, int *minute, int *second) {
    struct addrinfo hints, *res;
    int sockfd;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(ntp_server, "123", &hints, &res) != 0) {
        perror("getaddrinfo failed");
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        perror("socket creation failed");
        freeaddrinfo(res);
        return -1;
    }

    // 设置socket为非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set socket as non-blocking");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    // NTP packet structure
    unsigned char buf[48] = {0};
    buf[0] = 0x1b; // LI, Version, Mode

    // Send packet to NTP server
    ssize_t sent_bytes = sendto(sockfd, buf, sizeof(buf), 0, res->ai_addr, res->ai_addrlen);
    if (sent_bytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("sendto failed");
            close(sockfd);
            freeaddrinfo(res);
            return -1;
        }
    }

    // 使用select等待接收数据或超时
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 2; // 超时时间为2秒
    timeout.tv_usec = 0;

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    if (ret == 0) { // 超时
        fprintf(stderr, "Receive from NTP server timed out\n");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    } else if (ret < 0) { // 错误发生
        perror("Select failed");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    // Receive response from NTP server
    ssize_t received_bytes = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
    if (received_bytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recvfrom failed");
            close(sockfd);
            freeaddrinfo(res);
            return -1;
        }
    }

    // Close the socket and free address info
    close(sockfd);
    freeaddrinfo(res);

    // Convert received time to seconds since Jan 1, 1900
    uint32_t secsSince1900;
    memcpy(&secsSince1900, &buf[40], sizeof(secsSince1900));
    secsSince1900 = ntohl(secsSince1900); // Network byte order to host byte order

    // Convert to Unix time (seconds since Jan 1, 1970)
    time_t unixTime = (time_t)(secsSince1900 - NTP_TIMESTAMP_DELTA);

    // set time zone
    setenv("TZ", "CST-8", 1); // UTC+8
    tzset();                // 应用新的时区设置

    // Convert Unix time to broken-down time
    struct tm *timeinfo = localtime(&unixTime);
    if (!timeinfo) {
        perror("localtime failed");
        return -1;
    }

    if (year) *year = timeinfo->tm_year + 1900;
    if (month) *month = timeinfo->tm_mon + 1;
    if (day) *day = timeinfo->tm_mday;
    if (hour) *hour = timeinfo->tm_hour;
    if (minute) *minute = timeinfo->tm_min;
    if (second) *second = timeinfo->tm_sec;

    return 0;
}

int sys_save_system_parameters(const char *filepath, const system_para_t *params) {
    FILE *file = fopen(filepath, "w");
    if (!file) return -1;

    fprintf(file, "year=%d\n", params->year);
    fprintf(file, "month=%d\n", params->month);
    fprintf(file, "day=%d\n", params->day);
    fprintf(file, "hour=%u\n", params->hour);
    fprintf(file, "minute=%u\n", params->minute);
    fprintf(file, "brightness=%u\n", params->brightness);
    fprintf(file, "sound=%u\n", params->sound);
    fprintf(file, "wifi_connected=%s\n", params->wifi_connected ? "true" : "false");
    fprintf(file, "auto_time=%s\n", params->auto_time ? "true" : "false");
    fprintf(file, "auto_location=%s\n", params->auto_location ? "true" : "false");
    fprintf(file, "city=%s\n", params->location.city);
    fprintf(file, "adcode=%s\n", params->location.adcode);
    fprintf(file, "gaode_api_key=%s\n", params->gaode_api_key);
    fprintf(file, "AIChat_server_url=%s\n", params->aichat_app_info.addr);
    fprintf(file, "AIChat_server_port=%d\n", params->aichat_app_info.port);
    fprintf(file, "AIChat_server_token=%s\n", params->aichat_app_info.token);
    fprintf(file, "AIChat_Client_ID=%s\n", params->aichat_app_info.device_id);
    fprintf(file, "aliyun_api_key=%s\n", params->aichat_app_info.aliyun_api_key);
    fprintf(file, "AIChat_protocol_version=%d\n", params->aichat_app_info.protocol_version);
    fprintf(file, "AIChat_sample_rate=%d\n", params->aichat_app_info.sample_rate);
    fprintf(file, "AIChat_channels=%d\n", params->aichat_app_info.channels);
    fprintf(file, "AIChat_frame_duration=%d\n", params->aichat_app_info.frame_duration);

    fclose(file);
    return 0;
}

int sys_load_system_parameters(const char *filepath, system_para_t *params) {
    FILE *file = fopen(filepath, "r");
    if (!file) return -1;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char key[128], value[128];
        if (sscanf(line, "%[^=]=%s", key, value) != 2) continue;

        if (strcmp(key, "year") == 0) {
            params->year = atoi(value);
        } else if (strcmp(key, "month") == 0) {
            params->month = atoi(value);
        } else if (strcmp(key, "day") == 0) {
            params->day = atoi(value);
        } else if (strcmp(key, "hour") == 0) {
            params->hour = (uint8_t)atoi(value);
        } else if (strcmp(key, "minute") == 0) {
            params->minute = (uint8_t)atoi(value);
        } else if (strcmp(key, "brightness") == 0) {
            params->brightness = (uint16_t)atoi(value);
        } else if (strcmp(key, "sound") == 0) {
            params->sound = (uint16_t)atoi(value);
        } else if (strcmp(key, "wifi_connected") == 0) {
            params->wifi_connected = strcmp(value, "true") == 0;
        } else if (strcmp(key, "auto_time") == 0) {
            params->auto_time = strcmp(value, "true") == 0;
        } else if (strcmp(key, "auto_location") == 0) {
            params->auto_location = strcmp(value, "true") == 0;
        } else if (strcmp(key, "city") == 0) {
            strncpy(params->location.city, value, sizeof(params->location.city)-1);
            params->location.city[sizeof(params->location.city)-1] = '\0';
        } else if (strcmp(key, "adcode") == 0) {
            strncpy(params->location.adcode, value, sizeof(params->location.adcode)-1);
            params->location.adcode[sizeof(params->location.adcode)-1] = '\0';
        } else if (strcmp(key, "gaode_api_key") == 0) {
            strncpy(params->gaode_api_key, value, sizeof(params->gaode_api_key)-1);
            params->gaode_api_key[sizeof(params->gaode_api_key)-1] = '\0';
        } else if(strcmp(key, "AIChat_server_url") == 0) {
            strncpy(params->aichat_app_info.addr, value, sizeof(params->aichat_app_info.addr)-1);
            params->aichat_app_info.addr[sizeof(params->aichat_app_info.addr)-1] = '\0';
        } else if(strcmp(key, "AIChat_server_port") == 0) {
            params->aichat_app_info.port = atoi(value);
        } else if(strcmp(key, "AIChat_server_token") == 0) {
            strncpy(params->aichat_app_info.token, value, sizeof(params->aichat_app_info.token)-1);
            params->aichat_app_info.token[sizeof(params->aichat_app_info.token)-1] = '\0';
        } else if(strcmp(key, "AIChat_Client_ID") == 0) {
            strncpy(params->aichat_app_info.device_id, value, sizeof(params->aichat_app_info.device_id)-1);
            params->aichat_app_info.device_id[sizeof(params->aichat_app_info.device_id)-1] = '\0';
        } else if(strcmp(key, "aliyun_api_key") == 0) {
            strncpy(params->aichat_app_info.aliyun_api_key, value, sizeof(params->aichat_app_info.aliyun_api_key)-1);
            params->aichat_app_info.aliyun_api_key[sizeof(params->aichat_app_info.aliyun_api_key)-1] = '\0';
        } else if(strcmp(key, "AIChat_protocol_version") == 0) {
            params->aichat_app_info.protocol_version = atoi(value);
        } else if(strcmp(key, "AIChat_sample_rate") == 0) {
            params->aichat_app_info.sample_rate = atoi(value);
        } else if(strcmp(key, "AIChat_channels") == 0) {
            params->aichat_app_info.channels = atoi(value);
        } else if(strcmp(key, "AIChat_frame_duration") == 0) {
            params->aichat_app_info.frame_duration = atoi(value);
        }
    }

    fclose(file);
    return 0;
}