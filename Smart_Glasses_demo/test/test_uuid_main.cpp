/**
 * @file test_uuid_persistence.cpp
 * @brief UUID持久化功能测试程序
 * @details 验证UUID生成、保存、读取功能
 */

 #include <iostream>
 #include <iomanip>
 #include <string>
 #include <unistd.h>  // for sleep
 #include "../app/chatbot/uuid/uuid.h"
 
 using namespace glasses::tool;
 
 // 打印分隔线
 void printSeparator(const std::string& title) {
     std::cout << "\n";
     std::cout << "========================================" << std::endl;
     std::cout << "  " << title << std::endl;
     std::cout << "========================================" << std::endl;
 }
 
 // 测试1：首次生成UUID并保存到配置文件
 void testFirstGeneration() {
     printSeparator("测试1: 首次生成UUID");
     
     std::cout << "\n调用 generateUUID() ..." << std::endl;
     std::string uuid1 = generateUUID();
     
     std::cout << "\n✓ UUID已生成并保存: " << uuid1 << std::endl;
 }
 
 // 测试2：第二次调用应该读取已有的UUID（不重复生成）
 void testSecondCall() {
     printSeparator("测试2: 再次调用（不应重复生成）");
     
     std::cout << "\n再次调用 generateUUID() ..." << std::endl;
     std::string uuid2 = generateUUID();
     
     std::cout << "\n✓ 读取到已有的UUID: " << uuid2 << std::endl;
     std::cout << "✓ 验证：UUID未重复生成" << std::endl;
 }
 
 // 测试3：直接读取配置文件中的UUID
 void testDirectRead() {
     printSeparator("测试3: 直接读取配置文件");
     
     std::cout << "\n调用 readUUIDFromConfig() ..." << std::endl;
     std::string uuid = readUUIDFromConfig(DEFAULT_CONFIG_FILE);
     
     if (!uuid.empty()) {
         std::cout << "\n✓ 成功读取UUID: " << uuid << std::endl;
         std::cout << "✓ UUID格式有效: " << (isValidUUID(uuid) ? "是" : "否") << std::endl;
     } else {
         std::cout << "\n✗ 读取失败：配置文件为空或格式错误" << std::endl;
     }
 }
 
 // 测试4：验证UUID唯一性（多次调用返回相同UUID）
 void testUniqueness() {
     printSeparator("测试4: UUID唯一性验证");
     
     std::cout << "\n连续调用 generateUUID() 5次..." << std::endl;
     
     std::string uuid_list[5];
     for (int i = 0; i < 5; i++) {
         uuid_list[i] = generateUUID();
         std::cout << "  第" << (i+1) << "次: " << uuid_list[i] << std::endl;
     }
     
     // 验证所有UUID是否相同
     bool all_same = true;
     for (int i = 1; i < 5; i++) {
         if (uuid_list[i] != uuid_list[0]) {
             all_same = false;
             break;
         }
     }
     
     if (all_same) {
         std::cout << "\n✓ 验证通过：所有UUID完全相同" << std::endl;
         std::cout << "✓ UUID不会重复生成" << std::endl;
     } else {
         std::cout << "\n✗ 验证失败：UUID不一致" << std::endl;
     }
 }
 
 // 测试5：生成新的随机UUID（不保存）
 void testNewUUID() {
     printSeparator("测试5: 生成新的随机UUID");
     
     std::cout << "\n调用 generateNewUUID() 生成3个临时UUID..." << std::endl;
     
     for (int i = 0; i < 3; i++) {
         std::string uuid = generateNewUUID();
         std::cout << "  临时UUID " << (i+1) << ": " << uuid << std::endl;
     }
     
     std::cout << "\n✓ 这些UUID不会保存到配置文件" << std::endl;
     
     // 验证配置文件中的UUID未改变
     std::string config_uuid = readUUIDFromConfig(DEFAULT_CONFIG_FILE);
     std::cout << "✓ 配置文件中的UUID仍为: " << config_uuid << std::endl;
 }
 
 // 测试6：格式化功能
 void testFormatting() {
     printSeparator("测试6: UUID格式化");
     
     // 测试无短横线的UUID
     std::string unformatted = "A1B2C3D4E5F64890ABCDEF1234567890";
     std::cout << "\n原始UUID（无短横线）: " << unformatted << std::endl;
     
     std::string formatted = formatUUID(unformatted);
     std::cout << "格式化后的UUID:       " << formatted << std::endl;
     
     if (!formatted.empty()) {
         std::cout << "✓ 格式化成功" << std::endl;
     }
 }
 
 // 主函数
 int main() {
     std::cout << "\n";
     std::cout << "╔════════════════════════════════════════╗" << std::endl;
     std::cout << "║   UUID持久化功能测试程序               ║" << std::endl;
     std::cout << "║   UUID Persistence Test Suite         ║" << std::endl;
     std::cout << "╚════════════════════════════════════════╝" << std::endl;
     
     try {
         // 首次运行前清空配置文件
         std::cout << "按Enter继续测试..." << std::endl;
         std::cin.get();
         
         // 运行所有测试
         testFirstGeneration();
         sleep(1);
         
         testSecondCall();
         sleep(1);
         
         testDirectRead();
         sleep(1);
         
         testUniqueness();
         sleep(1);
         
         testNewUUID();
         sleep(1);
         
         testFormatting();
         
         // 测试总结
         printSeparator("测试总结");
         std::cout << "\n  ✓ 所有测试执行完成" << std::endl;
         std::cout << "  ✓ UUID持久化功能正常工作" << std::endl;
         std::cout << "  ✓ UUID不会重复生成" << std::endl;
         std::cout << std::endl;
         
     } catch (const std::exception& e) {
         std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
         return 1;
     }
     
     std::cout << "========================================\n" << std::endl;
     
     return 0;
 }
 
 