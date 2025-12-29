#!/bin/bash
# 代码格式化脚本
# 使用 clang-format 自动格式化代码

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${GREEN}=== 代码格式化工具 ===${NC}\n"

# 检查工具是否安装
if ! command -v clang-format &> /dev/null; then
    echo -e "${YELLOW}错误: clang-format 未安装${NC}"
    echo "请安装: sudo apt-get install clang-format"
    exit 1
fi

# 查找所有 C++ 源文件
find_cpp_files() {
    # 查找 app、common、test 目录下的文件
    find app common test \( -name "*.cc" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) 2>/dev/null | \
        grep -v build | \
        grep -v third_party | \
        sort
    # 添加根目录下的 main.cpp（如果存在）
    if [ -f "main.cpp" ]; then
        echo "main.cpp"
    fi
}

FILES=$(find_cpp_files)
FILE_COUNT=$(echo "$FILES" | wc -l)

echo "找到 $FILE_COUNT 个文件"
echo ""

# 询问是否继续
read -p "是否格式化所有文件? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "已取消"
    exit 0
fi

# 格式化文件
FORMATTED=0
for file in $FILES; do
    if [ -f "$file" ]; then
        echo "格式化: $file"
        clang-format -i "$file"
        FORMATTED=$((FORMATTED + 1))
    fi
done

echo ""
echo -e "${GREEN}已格式化 $FORMATTED 个文件${NC}"

