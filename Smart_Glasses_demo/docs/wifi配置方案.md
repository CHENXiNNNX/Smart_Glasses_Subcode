1.wifi连接状态枚举
enum class wifiState {
    UNKNOWN = 0,        // 未知状态
    DISCONNECTED,       // 已断开
    SCANNING,           // 扫描中
    CONNECTING,         // 连接中
    AUTHENTICATING,     // 认证中
    ASSOCIATED,         // 已关联（但未获取IP）
    OBTAINING_IP,       // 获取IP中
    CONNECTED,          // 已连接
    FAILED              // 失败
};

2.wifi错误类型
enum class wifiError {
    NONE = 0,
    INITIALIZATION_FAILED,      // 初始化失败
    INTERFACE_NOT_FOUND,        // 接口未找到
    INTERFACE_NOT_UP,           // 接口未UP
    WPA_SUPPLICANT_NOT_FOUND,   // wpa_supplicant未找到
    WPA_SUPPLICANT_NOT_RUNNING, // wpa_supplicant未运行
    SCAN_FAILED,                // 扫描失败
    CONNECTION_FAILED,          // 连接失败
    DISCONNECTION_FAILED,       // 断开失败
    AUTHENTICATION_FAILED,      // 认证失败
    TIMEOUT,                    // 超时
    INVALID_PASSWORD,           // 密码错误
    NETWORK_NOT_FOUND,          // 网络未找到
    DHCP_FAILED,                // DHCP失败
    ALREADY_CONNECTED,          // 已经连接
    UNKNOWN                     // 未知错误
};

3.wifi加密类型
enum class wifiSecurity {
    NONE = 0,           // 无加密
    WEP,                // WEP加密
    WPA_PSK,            // WPA-PSK
    WPA2_PSK,           // WPA2-PSK
    WPA3_PSK,           // WPA3-PSK
    UNKNOWN             // 未知
};

4.wifi网络信息
struct wifiInfo {
    std::string ssid;                   // 网络名称
    std::string bssid;                  // MAC地址
    wifiSecurity security;              // 加密类型
    int signal_strength;                // 信号强度 (0-100)
    int frequency;                      // 频率 (MHz)
    int channel;                        // 信道
    
    wifiInfo() 
        : security(wifiSecurity::NONE),
          signal_strength(0),
          frequency(0),
          channel(0) {}
};

5.当前连接信息
struct wifiConnectionInfo {
    std::string ssid;                   // 当前SSID
    std::string bssid;                  // 当前BSSID
    std::string ip_address;             // IP地址
    int signal_strength;                // 信号强度 (0-100)
    wifiSecurity security;          // 加密类型
    wifiState state;                    // 连接状态
    
    wifiConnectionInfo()
        : signal_strength(0),
          security(wifiSecurity::NONE),
          state(wifiState::DISCONNECTED) {}
};

6.已保存的wifi网络信息(/etc/wpa_supplicant.conf)
struct savedNetworkInfo {
    std::string ssid;                    // 网络SSID
    std::string ssid;                    // 网络BSSID
    bool is_enabled_auto;                // 是否启用自动连接
    bool is_current;                     // 是否为当前连接
    
    savedNetworkInfo()
        : is_enabled_auto(false),
          is_current(false) {}
};

6.wifi管理器配置
struct wifiConfig {
    // 固定配置
    static constexpr const char* INTERFACE_NAME = "wlan0";
    static constexpr const char* WPA_CONF_PATH = "/etc/wpa_supplicant.conf";
    static constexpr const char* WPA_CTRL_PATH = "/var/run/wpa_supplicant";
    
    // 超时配置
    int interface_up_timeout_ms = 5000;       // 接口UP超时（5秒）
    int interface_up_check_interval_ms = 500; // 接口UP检查间隔（500ms）
    int scan_timeout_ms = 10000;              // 扫描超时（10秒）
    int connect_timeout_ms = 30000;           // 连接超时（30秒）
    int dhcp_timeout_ms = 15000;              // DHCP超时（15秒）
    int wpa_command_timeout_ms = 5000;        // wpa_cli命令超时（5秒）
    
    // 功能开关
    bool auto_save_config = true;             // 连接成功后自动保存配置

    // 自动扫描配置
    bool audto_scan = fales;                  // 自动扫描
    int timed_scan_ms = 60 * 1000;            // 60秒自动扫描一次

    // 自动重连配置
    bool enable_auto_reconnect = false;       // 启用自动重连
    int reconnect_interval_sec = 10;          // 重连间隔（秒）
    int reconnect_max_attempts = 5;           // 最大重连次数（0表示无限）
    int reconnect_delay_sec = 3;              // 重连前等待时间（秒）
};

7.wifi功能
(1)提供查询接口
-getState            // 获取WiFi状态
-isConnected         // 检查是否已连接
-isInterfaceUp       // 检查网络接口是否UP
-getCurrentSSID      // 获取当前SSID
-getIPaddress        // 获取当前IP地址
-getSignalStrength   // 获取信号强度（信号强度百分比）
-getConnectionInfo   // 获取当前连接信息
-getSavedNetworks    // 获取已保存的网络列表
...

(2)提供开启和关闭接口
-initialize          // 初始化WiFi管理器
-shutdown            // 关闭WiFi管理器
...

(3)提供功能接口
-扫描wifi(同步/异步)
-连接wifi(同步/异步)
-断开wifi
-重连wifi
...

8.wifi大致实现功能
(1)实现wifi的连接，断开，查询等功能
(2)读取已连接过的wifi信息(/etc/wpa_supplicant.conf)，并提供删除wifi信息的接口
(3)提供自动检测/etc/wpa_supplicant.conf中已有的wifi并自动扫描匹配最优wifi，如果
成功则自动连接wifi获取ip(只尝试连接1次，可配置次数)，不成功则后续自动进入wifi未连
接的状态，等待手动连接
(4)切换wifi，先检测是否连接wifi，如果已连接wifi则先断开wifi再进行连接操作
...

