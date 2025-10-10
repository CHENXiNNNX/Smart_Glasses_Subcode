# MAC地址获取工具使用文档

## 概述

`mac.h/cc` 提供了获取设备网络接口MAC地址的工具函数，支持自动识别无线网卡并获取其MAC地址。

**功能特性**:
- ✅ 自动检测并优先获取无线网卡（wlan*, wlp*）MAC地址
- ✅ 如果没有无线网卡，自动获取第一个有效的有线网卡MAC地址
- ✅ 支持指定网络接口名称获取MAC地址
- ✅ 支持获取所有网络接口列表
- ✅ 支持MAC地址格式化
- ✅ 自动过滤无效接口（回环、虚拟接口等）

---


### 1. 包含头文件

```cpp
#include "app/tool/mac/mac.h"

using namespace glasses::tool;
```

### 2. 使用示例

```cpp
#include <iostream>
#include "app/tool/mac/mac.h"

int main() {
    // 获取无线网卡MAC地址（推荐）
    std::string mac = glasses::tool::getWirelessMacAddress();
    
    if (!mac.empty()) {
        std::cout << "设备MAC地址: " << mac << std::endl;
        // 输出示例: 设备MAC地址: 00:0c:29:bd:43:04
    } else {
        std::cout << "获取MAC地址失败" << std::endl;
    }
    
    return 0;
}
```

---

## API 接口说明

### 函数1: `getWirelessMacAddress()`

**功能**: 获取无线网卡的MAC地址

**函数签名**:
```cpp
std::string getWirelessMacAddress();
```

**返回值**:
- 成功: 返回MAC地址字符串（格式：`xx:xx:xx:xx:xx:xx`）
- 失败: 返回空字符串

**查找优先级**:
1. 无线网卡（wlan0, wlan1, wlp2s0 等）
2. 第一个可用的有线网卡（eth0, enp0s3 等）
3. 失败返回空字符串

**示例**:
```cpp
std::string mac = glasses::tool::getWirelessMacAddress();
if (!mac.empty()) {
    std::cout << "MAC: " << mac << std::endl;
    // 输出: MAC: a4:5e:60:c7:89:3f
}
```

---

### 函数2: `getMacAddressByInterface()`

**功能**: 获取指定网络接口的MAC地址

**函数签名**:
```cpp
std::string getMacAddressByInterface(const std::string& interface_name);
```

**参数**:
- `interface_name`: 网络接口名称（如 "eth0", "wlan0" 等）

**返回值**:
- 成功: 返回MAC地址字符串
- 失败: 返回空字符串

**示例**:
```cpp
// 获取eth0的MAC地址
std::string eth0_mac = glasses::tool::getMacAddressByInterface("eth0");

// 获取wlan0的MAC地址
std::string wlan0_mac = glasses::tool::getMacAddressByInterface("wlan0");

if (!wlan0_mac.empty()) {
    std::cout << "wlan0 MAC: " << wlan0_mac << std::endl;
}
```

---

### 函数3: `getAllNetworkInterfaces()`

**功能**: 获取所有网络接口列表

**函数签名**:
```cpp
std::vector<std::string> getAllNetworkInterfaces();
```

**返回值**:
- 返回所有网络接口名称的vector（已排序，无线网卡优先）

**示例**:
```cpp
auto interfaces = glasses::tool::getAllNetworkInterfaces();

std::cout << "发现 " << interfaces.size() << " 个网络接口:" << std::endl;
for (const auto& iface : interfaces) {
    std::string mac = glasses::tool::getMacAddressByInterface(iface);
    std::cout << "  - " << iface << ": " << mac << std::endl;
}

// 输出示例:
// 发现 2 个网络接口:
//   - wlan0: a4:5e:60:c7:89:3f
//   - eth0: 00:0c:29:bd:43:04
```

---

### 函数4: `formatMacAddress()`

**功能**: 格式化MAC地址为标准格式

**函数签名**:
```cpp
std::string formatMacAddress(const std::string& mac);
```

**参数**:
- `mac`: 原始MAC地址（支持多种格式）

**返回值**:
- 成功: 返回标准格式的MAC地址（小写，冒号分隔）
- 失败: 返回空字符串

**支持的输入格式**:
- `AA:BB:CC:DD:EE:FF`（标准格式）
- `aabbccddeeff`（无分隔符）
- `AA-BB-CC-DD-EE-FF`（短横线分隔）

**示例**:
```cpp
std::string mac1 = glasses::tool::formatMacAddress("AABBCCDDEEFF");
// 结果: aa:bb:cc:dd:ee:ff

std::string mac2 = glasses::tool::formatMacAddress("AA-BB-CC-DD-EE-FF");
// 结果: aa:bb:cc:dd:ee:ff

std::string mac3 = glasses::tool::formatMacAddress("aa:bb:cc:dd:ee:ff");
// 结果: aa:bb:cc:dd:ee:ff
```

## 注意事项

### 1. 权限问题
- 读取 `/sys/class/net/` 目录通常不需要特殊权限
- 如果遇到权限问题，确保程序有读取权限

### 2. MAC地址为空的情况
```cpp
std::string mac = glasses::tool::getWirelessMacAddress();

if (mac.empty()) {
    // 处理MAC地址为空的情况
    std::cerr << "无法获取MAC地址，可能原因:" << std::endl;
    std::cerr << "  1. 没有可用的网络接口" << std::endl;
    std::cerr << "  2. 权限不足" << std::endl;
    std::cerr << "  3. 虚拟环境或容器中" << std::endl;
    
    // 使用默认值或退出
    mac = "00:00:00:00:00:00";
}
```

### 3. 过滤的接口类型
以下类型的接口会被**自动过滤**（不返回）：
- `lo` - 回环接口
- `docker*` - Docker虚拟接口
- `veth*` - 虚拟以太网接口
- `virbr*` - libvirt虚拟桥接接口

### 4. MAC地址格式
- 返回的MAC地址格式固定为：`xx:xx:xx:xx:xx:xx`（小写，冒号分隔）
- 长度固定为17个字符


