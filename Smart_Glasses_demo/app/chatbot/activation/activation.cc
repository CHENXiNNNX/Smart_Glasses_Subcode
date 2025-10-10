#include "activation.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace glasses {
namespace chatbot {
namespace activation {

using json = nlohmann::json;

// CURL回调函数
size_t DeviceActivation::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int DeviceActivation::checkActivation(const std::string& mac, 
                                       const std::string& uuid,
                                       std::string& activation_code) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    // 激活URL
    std::string url = "https://api.tenclass.net/xiaozhi/ota/";

    // POST数据
    json post_json;
    post_json["platform"] = "linux";
    post_json["version"] = "1.0.0";
    post_json["board"]["type"] = "smart_glasses";
    post_json["board"]["name"] = "smart_glasses_board";
    std::string post_data = post_json.dump();

    // HTTP Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Device-Id: " + mac).c_str());
    headers = curl_slist_append(headers, "User-Agent: SmartGlasses/1.0");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN");

    std::cout << "[Activation] 检查设备激活状态..." << std::endl;
    std::cout << "[Activation]   Device-Id: " << mac << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (!curl) {
        std::cerr << "[Activation] ✗ CURL初始化失败" << std::endl;
        return -1;
    }

    // 设置CURL选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // 10秒超时

    // 执行请求
    res = curl_easy_perform(curl);

    int result = -1;
    if (res != CURLE_OK) {
        std::cerr << "[Activation] ✗ 请求失败: " << curl_easy_strerror(res) << std::endl;
    } else {
        try {
            json response = json::parse(readBuffer);
            
            if (response.contains("activation") && response["activation"].contains("code")) {
                // 未激活，返回激活码
                activation_code = response["activation"]["code"];
                std::cout << "[Activation] ⚠ 设备未激活" << std::endl;
                result = 1;
            } else {
                // 已激活
                std::cout << "[Activation] ✓ 设备已激活" << std::endl;
                result = 0;
            }
        } catch (const json::parse_error& e) {
            std::cerr << "[Activation] ✗ JSON解析失败: " << e.what() << std::endl;
            std::cerr << "[Activation]   响应: " << readBuffer << std::endl;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return result;
}

bool DeviceActivation::waitForActivation(const std::string& mac,
                                         const std::string& uuid,
                                         int timeout_seconds) {
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   设备未激活，请访问以下网址激活       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    // 获取激活码
    std::string activation_code;
    int status = checkActivation(mac, uuid, activation_code);
    
    if (status == 0) {
        return true;  // 已经激活
    } else if (status == -1) {
        return false;  // 检查失败
    }
    
    std::cout << "\n  激活网址: https://xiaozhi.me" << std::endl;
    std::cout << "  激活码:   " << activation_code << std::endl;
    std::cout << "\n提示：请在网站上输入激活码完成激活" << std::endl;
    std::cout << "程序将每5秒自动检查激活状态...\n" << std::endl;
    
    // 轮询检查激活状态
    int elapsed = 0;
    while (elapsed < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        elapsed += 5;
        
        std::cout << "[Activation] 检查激活状态... (" << elapsed << "s/" << timeout_seconds << "s)" << std::endl;
        
        status = checkActivation(mac, uuid, activation_code);
        if (status == 0) {
            std::cout << "\n✓ 设备激活成功！" << std::endl;
            return true;
        } else if (status == -1) {
            std::cerr << "⚠ 检查失败，继续等待..." << std::endl;
        }
    }
    
    std::cerr << "\n✗ 激活超时（" << timeout_seconds << "秒）" << std::endl;
    return false;
}

bool DeviceActivation::isActivated(const std::string& mac,
                                   const std::string& uuid) {
    std::string activation_code;
    int status = checkActivation(mac, uuid, activation_code);
    return (status == 0);
}

} // namespace activation
} // namespace chatbot
} // namespace glasses

