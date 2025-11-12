/**
 * @file test_signaling_main.cpp
 * @brief Signaling客户端完整测试程序
 * @details 测试信令客户端的各项功能，包括：
 *          - 连接管理
 *          - 房间管理
 *          - 消息收发
 *          - 回调处理
 *          - 状态管理
 */

#include "app/protocol/webrtc/signaling.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <string>

using namespace app::protocol::webrtc;

// ============================================================================
// 全局变量
// ============================================================================

// 用于控制程序运行
std::atomic<bool> g_running(true);
std::atomic<bool> g_paired(false);
std::condition_variable g_cv;
std::mutex g_cv_mutex;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 打印分隔线
 */
void printSeparator(const std::string& title = "") {
    std::cout << "\n";
    std::cout << "========================================";
    if (!title.empty()) {
        std::cout << "\n  " << title;
        std::cout << "\n========================================";
    }
    std::cout << std::endl;
}

/**
 * @brief 等待一段时间
 */
void waitSeconds(int seconds) {
    std::cout << "[等待] " << seconds << " 秒..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

/**
 * @brief 等待特定条件
 */
bool waitForCondition(std::function<bool()> condition, int timeout_seconds) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (!condition()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (elapsed >= timeout_seconds) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return true;
}

/**
 * @brief 打印房间信息
 */
void printRoomInfo(const RoomInfo& info) {
    std::cout << "  房间ID: " << info.roomId << std::endl;
    std::cout << "  人数: " << info.num << std::endl;
    std::cout << "  状态: " << info.roomStatus << std::endl;
}

/**
 * @brief 打印状态信息
 */
void printStatus(const std::string& prefix, SignalingStatus status) {
    std::cout << prefix << Signaling::statusToString(status) << std::endl;
}

// ============================================================================
// 回调函数实现
// ============================================================================

/**
 * @brief 状态变化回调
 */
void onStatusChanged(SignalingStatus status) {
    printSeparator("状态变化");
    std::cout << "新状态: " << Signaling::statusToString(status) << std::endl;
    
    // 如果已配对，通知主线程
    if (status == SignalingStatus::PAIRED) {
        g_paired.store(true);
        g_cv.notify_one();
    }
}

/**
 * @brief WebRTC就绪回调
 */
void onWebRTCReady(const std::string& role, const std::string& peer_device_id) {
    printSeparator("WebRTC就绪");
    std::cout << "  角色: " << role << std::endl;
    std::cout << "  对端设备ID: " << peer_device_id << std::endl;
    std::cout << "  [信息] 可以开始WebRTC连接了" << std::endl;
}

/**
 * @brief Offer接收回调
 */
void onOfferReceived(const nlohmann::json& msg) {
    printSeparator("收到Offer");
    
    if (msg.contains("from")) {
        std::cout << "  来自: " << msg["from"].get<std::string>() << std::endl;
    }
    
    if (msg.contains("data") && msg["data"].contains("sdp")) {
        std::string sdp = msg["data"]["sdp"].get<std::string>();
        std::cout << "  SDP长度: " << sdp.length() << " 字节" << std::endl;
        std::cout << "  SDP前100字符: " << sdp.substr(0, 100) << "..." << std::endl;
    }
}

/**
 * @brief Answer接收回调
 */
void onAnswerReceived(const nlohmann::json& msg) {
    printSeparator("收到Answer");
    
    if (msg.contains("from")) {
        std::cout << "  来自: " << msg["from"].get<std::string>() << std::endl;
    }
    
    if (msg.contains("data") && msg["data"].contains("sdp")) {
        std::string sdp = msg["data"]["sdp"].get<std::string>();
        std::cout << "  SDP长度: " << sdp.length() << " 字节" << std::endl;
        std::cout << "  SDP前100字符: " << sdp.substr(0, 100) << "..." << std::endl;
    }
}

/**
 * @brief ICE候选接收回调
 */
void onIceCandidateReceived(const nlohmann::json& msg) {
    printSeparator("收到ICE候选");
    
    if (msg.contains("from")) {
        std::cout << "  来自: " << msg["from"].get<std::string>() << std::endl;
    }
    
    if (msg.contains("data") && msg["data"].contains("candidate")) {
        std::string candidate = msg["data"]["candidate"].get<std::string>();
        std::cout << "  候选: " << candidate << std::endl;
    }
}

/**
 * @brief 房间信息变化回调
 */
void onRoomInfoChanged(const RoomInfo& info) {
    printSeparator("房间信息变化");
    printRoomInfo(info);
}

/**
 * @brief 错误回调
 */
void onError(SignalingError error, const std::string& message) {
    printSeparator("错误");
    std::cout << "  错误码: " << Signaling::errorToString(error) << std::endl;
    std::cout << "  错误信息: " << message << std::endl;
}

// ============================================================================
// 测试场景
// ============================================================================

/**
 * @brief 测试场景1：基本连接流程
 */
bool testBasicConnection(Signaling& signaling) {
    printSeparator("测试场景1：基本连接流程");
    
    // 1. 连接到服务器
    std::cout << "[步骤1] 连接到信令服务器..." << std::endl;
    if (!signaling.connect()) {
        std::cerr << "[错误] 连接失败" << std::endl;
        return false;
    }
    
    // 等待连接成功
    std::cout << "[步骤2] 等待连接成功..." << std::endl;
    if (!waitForCondition([&]() { 
        return signaling.getStatus() == SignalingStatus::CONNECTED; 
    }, 10)) {
        std::cerr << "[错误] 连接超时" << std::endl;
        return false;
    }
    
    std::cout << "[成功] 已连接到服务器" << std::endl;
    printStatus("  当前状态: ", signaling.getStatus());
    
    return true;
}

/**
 * @brief 测试场景2：加入房间
 */
bool testJoinRoom(Signaling& signaling) {
    printSeparator("测试场景2：加入房间");
    
    std::cout << "[步骤1] 加入房间..." << std::endl;
    if (!signaling.joinRoom()) {
        std::cerr << "[错误] 加入房间失败" << std::endl;
        return false;
    }
    
    std::cout << "[成功] 已发送加入房间请求" << std::endl;
    printStatus("  当前状态: ", signaling.getStatus());
    
    // 请求房间信息
    waitSeconds(1);
    std::cout << "\n[步骤2] 请求房间信息..." << std::endl;
    signaling.requestRoomInfo();
    
    waitSeconds(2);
    
    // 显示房间信息
    RoomInfo room_info = signaling.getRoomInfo();
    std::cout << "\n[房间信息]" << std::endl;
    printRoomInfo(room_info);
    
    return true;
}

/**
 * @brief 测试场景3：等待配对
 */
bool testWaitForPairing(Signaling& signaling) {
    printSeparator("测试场景3：等待配对");
    
    std::cout << "[信息] 等待另一个设备加入房间..." << std::endl;
    std::cout << "[提示] 请启动另一个客户端连接到同一房间" << std::endl;
    
    // 等待配对（最多等待60秒）
    std::unique_lock<std::mutex> lock(g_cv_mutex);
    if (g_cv.wait_for(lock, std::chrono::seconds(60), []() { 
        return g_paired.load(); 
    })) {
        std::cout << "[成功] 配对成功！" << std::endl;
        printStatus("  当前状态: ", signaling.getStatus());
        std::cout << "  角色: " << signaling.getRole() << std::endl;
        std::cout << "  对端设备: " << signaling.getPeerDeviceId() << std::endl;
        return true;
    } else {
        std::cout << "[超时] 未能在60秒内完成配对" << std::endl;
        return false;
    }
}

/**
 * @brief 测试场景4：发送消息（作为Offerer）
 */
bool testSendAsOfferer(Signaling& signaling) {
    printSeparator("测试场景4：发送消息（作为Offerer）");
    
    if (signaling.getRole() != "offerer") {
        std::cout << "[跳过] 当前角色不是offerer" << std::endl;
        return true;
    }
    
    std::string peer_device_id = signaling.getPeerDeviceId();
    
    // 发送模拟的SDP Offer
    std::cout << "[步骤1] 发送SDP Offer..." << std::endl;
    std::string mock_sdp = "v=0\r\no=- 123456789 1 IN IP4 127.0.0.1\r\ns=Test Session\r\n";
    
    if (signaling.sendOffer(mock_sdp, peer_device_id)) {
        std::cout << "[成功] SDP Offer已发送" << std::endl;
    } else {
        std::cerr << "[错误] 发送SDP Offer失败" << std::endl;
        return false;
    }
    
    waitSeconds(2);
    
    // 发送模拟的ICE候选
    std::cout << "\n[步骤2] 发送ICE候选..." << std::endl;
    std::string mock_candidate = "candidate:1 1 UDP 2130706431 192.168.1.100 54321 typ host";
    
    if (signaling.sendIceCandidate(mock_candidate, peer_device_id)) {
        std::cout << "[成功] ICE候选已发送" << std::endl;
    } else {
        std::cerr << "[错误] 发送ICE候选失败" << std::endl;
        return false;
    }
    
    return true;
}

/**
 * @brief 测试场景5：接收并回复消息（作为Answerer）
 */
bool testSendAsAnswerer(Signaling& signaling) {
    printSeparator("测试场景5：接收并回复消息（作为Answerer）");
    
    if (signaling.getRole() != "answerer") {
        std::cout << "[跳过] 当前角色不是answerer" << std::endl;
        return true;
    }
    
    std::string peer_device_id = signaling.getPeerDeviceId();
    
    std::cout << "[信息] 等待接收Offer..." << std::endl;
    waitSeconds(3);
    
    // 发送模拟的SDP Answer
    std::cout << "\n[步骤1] 发送SDP Answer..." << std::endl;
    std::string mock_sdp = "v=0\r\no=- 987654321 1 IN IP4 127.0.0.1\r\ns=Test Answer Session\r\n";
    
    if (signaling.sendAnswer(mock_sdp, peer_device_id)) {
        std::cout << "[成功] SDP Answer已发送" << std::endl;
    } else {
        std::cerr << "[错误] 发送SDP Answer失败" << std::endl;
        return false;
    }
    
    waitSeconds(2);
    
    // 发送模拟的ICE候选
    std::cout << "\n[步骤2] 发送ICE候选..." << std::endl;
    std::string mock_candidate = "candidate:1 1 UDP 2130706431 192.168.1.101 54322 typ host";
    
    if (signaling.sendIceCandidate(mock_candidate, peer_device_id)) {
        std::cout << "[成功] ICE候选已发送" << std::endl;
    } else {
        std::cerr << "[错误] 发送ICE候选失败" << std::endl;
        return false;
    }
    
    return true;
}

/**
 * @brief 测试场景6：状态查询
 */
void testStatusQuery(Signaling& signaling) {
    printSeparator("测试场景6：状态查询");
    
    std::cout << "[当前状态]" << std::endl;
    std::cout << "  状态: " << Signaling::statusToString(signaling.getStatus()) << std::endl;
    std::cout << "  设备ID: " << signaling.getDeviceId() << std::endl;
    std::cout << "  对端设备ID: " << signaling.getPeerDeviceId() << std::endl;
    std::cout << "  角色: " << signaling.getRole() << std::endl;
    std::cout << "  是否已连接: " << (signaling.isConnected() ? "是" : "否") << std::endl;
    std::cout << "  是否已配对: " << (signaling.isPaired() ? "是" : "否") << std::endl;
    
    std::cout << "\n[房间信息]" << std::endl;
    RoomInfo room_info = signaling.getRoomInfo();
    printRoomInfo(room_info);
}

/**
 * @brief 测试场景7：断开连接
 */
void testDisconnect(Signaling& signaling) {
    printSeparator("测试场景7：断开连接");
    
    std::cout << "[步骤1] 离开房间..." << std::endl;
    signaling.leaveRoom();
    waitSeconds(1);
    
    std::cout << "[步骤2] 断开连接..." << std::endl;
    signaling.disconnect();
    waitSeconds(1);
    
    printStatus("  当前状态: ", signaling.getStatus());
    std::cout << "[成功] 已断开连接" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[]) {
    printSeparator("Signaling客户端测试程序");
    
    // 解析命令行参数
    std::string device_id = "glasses_123456";
    std::string server_url = "ws://192.168.2.17:8000";
    
    if (argc >= 2) {
        device_id = argv[1];
    }
    if (argc >= 3) {
        server_url = argv[2];
    }
    
    std::cout << "[配置]" << std::endl;
    std::cout << "  设备ID: " << device_id << std::endl;
    std::cout << "  服务器地址: " << server_url << std::endl;
    
    // 创建信令客户端配置
    SignalingConfig config;
    config.deviceId = device_id;
    config.serverUrl = server_url;
    config.auto_reconnect = true;
    config.reconnect_interval_ms = 5000;
    config.max_reconnect_attempts = 3;
    
    try {
        // 创建信令客户端
        Signaling signaling(config);
        
        // 设置回调
        printSeparator("设置回调函数");
        signaling.onStatusChanged(onStatusChanged);
        signaling.onWebRTCReady(onWebRTCReady);
        signaling.onOfferReceived(onOfferReceived);
        signaling.onAnswerReceived(onAnswerReceived);
        signaling.onIceCandidateReceived(onIceCandidateReceived);
        signaling.onRoomInfoChanged(onRoomInfoChanged);
        signaling.onError(onError);
        std::cout << "[成功] 所有回调已设置" << std::endl;
        
        // 执行测试场景
        bool success = true;
        
        // 场景1：连接
        if (!testBasicConnection(signaling)) {
            std::cerr << "\n[失败] 基本连接测试失败" << std::endl;
            return 1;
        }
        
        // 场景2：加入房间
        if (!testJoinRoom(signaling)) {
            std::cerr << "\n[失败] 加入房间测试失败" << std::endl;
            return 1;
        }
        
        // 场景3：等待配对
        bool paired = testWaitForPairing(signaling);
        
        if (paired) {
            // 场景4/5：发送消息（根据角色）
            if (signaling.getRole() == "offerer") {
                testSendAsOfferer(signaling);
            } else {
                testSendAsAnswerer(signaling);
            }
            
            // 保持连接一段时间，接收消息
            printSeparator("保持连接");
            std::cout << "[信息] 保持连接10秒，监听消息..." << std::endl;
            waitSeconds(10);
        }
        
        // 场景6：状态查询
        testStatusQuery(signaling);
        
        // 场景7：断开连接
        testDisconnect(signaling);
        
        printSeparator("测试完成");
        std::cout << "[结果] 所有测试场景执行完毕" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n[异常] 程序异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

