#include "app_WeatherPage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// 回调函数，用于处理libcurl接收到的数据
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char** response = (char**)userp;

    *response = realloc(*response, strlen(*response) + realsize + 1);
    if (*response == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 0;
    }

    strncat(*response, (char*)contents, realsize);
    return realsize;
}

int get_weather_info_by_adcode(const char* adcode, WeatherInfo_t* weather_info) {
    CURL* curl_handle;
    CURLcode res;
    char url[256];
    snprintf(url, sizeof(url), "https://restapi.amap.com/v3/weather/weatherInfo?city=%s&key=%s", adcode, ui_system_para.gaode_api_key);

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

    // 设置超时时间为5秒（5000毫秒）
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, 5000L);

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

    struct json_object *lives_array, *live_obj;
    json_object_object_get_ex(parsed_json, "lives", &lives_array);
    if(json_object_get_type(lives_array) == json_type_array && json_object_array_length(lives_array) > 0) {

        live_obj = json_object_array_get_idx(lives_array, 0);
        struct json_object *weather, *temperature, *humidity, *windpower;
        json_object_object_get_ex(live_obj, "weather", &weather);
        json_object_object_get_ex(live_obj, "temperature", &temperature);
        json_object_object_get_ex(live_obj, "humidity", &humidity);
        json_object_object_get_ex(live_obj, "windpower", &windpower);

        const char* weather_str = json_object_get_string(weather);
        const char* temperature_str = json_object_get_string(temperature);
        const char* humidity_str = json_object_get_string(humidity);
        const char* windpower_str = json_object_get_string(windpower);

        strncpy(weather_info->weather, weather_str ? weather_str : "N/A", sizeof(weather_info->weather) - 1);
        weather_info->weather[sizeof(weather_info->weather) - 1] = '\0';
        strncpy(weather_info->temperature, temperature_str ? temperature_str : "N/A", sizeof(weather_info->temperature) - 1);
        weather_info->temperature[sizeof(weather_info->temperature) - 1] = '\0';
        strncpy(weather_info->humidity, humidity_str ? humidity_str : "N/A", sizeof(weather_info->humidity) - 1);
        weather_info->humidity[sizeof(weather_info->humidity) - 1] = '\0';
        strncpy(weather_info->windpower, windpower_str ? windpower_str : "N/A", sizeof(weather_info->windpower) - 1);
        weather_info->windpower[sizeof(weather_info->windpower) - 1] = '\0';

        json_object_put(parsed_json); // 释放JSON对象

        free(response_string);
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();

        return 0;
    } else {
        json_object_put(parsed_json); // 释放JSON对象
        free(response_string);
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return -1;
    }
}