#!/bin/bash
# 代码检查脚本
# 使用 clang-tidy 和 clang-format 检查代码

set -uo pipefail

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo -e "${GREEN}=== 代码检查工具 ===${NC}\n"

# 检查工具是否安装
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "${RED}错误: $1 未安装${NC}"
        echo "请安装: sudo apt-get install $2"
        exit 1
    fi
}

check_tool clang-tidy clang-tidy
check_tool clang-format clang-format

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

# 1. 使用 clang-format 检查代码格式
echo -e "${YELLOW}[1/2] 检查代码格式 (clang-format)...${NC}"
FILES_TO_CHECK=$(find_cpp_files)
FORMAT_ISSUES=0

for file in $FILES_TO_CHECK; do
    if [ -f "$file" ]; then
        # 使用临时文件直接保存 clang-format 的输出
        TEMP_FILE=$(mktemp)
        if ! clang-format "$file" > "$TEMP_FILE" 2>/dev/null; then
            echo -e "${RED}格式检查失败: $file${NC}"
            FORMAT_ISSUES=$((FORMAT_ISSUES + 1))
            rm -f "$TEMP_FILE" 2>/dev/null || true
            continue
        fi
        
        # 比较文件（忽略 diff 的退出码）
        if ! diff -q "$file" "$TEMP_FILE" > /dev/null 2>&1; then
            echo -e "${RED}格式问题: $file${NC}"
            diff -u "$file" "$TEMP_FILE" 2>&1 | head -20 || true
            FORMAT_ISSUES=$((FORMAT_ISSUES + 1))
        fi
        
        rm -f "$TEMP_FILE" 2>/dev/null || true
    fi
done

if [ $FORMAT_ISSUES -eq 0 ]; then
    echo -e "${GREEN}代码格式检查通过${NC}\n"
else
    echo -e "${RED}✗ 发现 $FORMAT_ISSUES 个格式问题${NC}\n"
fi

# 2. 使用 clang-tidy 检查代码质量
echo -e "${YELLOW}[2/2] 检查代码质量 (clang-tidy)...${NC}"

# 查找编译数据库
COMPILE_DB=""
if [ -f "build/compile_commands.json" ]; then
    COMPILE_DB="build/compile_commands.json"
elif [ -f "compile_commands.json" ]; then
    COMPILE_DB="compile_commands.json"
else
    echo -e "${YELLOW}警告: 未找到 compile_commands.json${NC}"
    echo "项目需要先编译一次以生成编译数据库"
    echo "运行: cmake -B build && cmake --build build"
    echo ""
    read -p "是否继续使用基本检查? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

TIDY_ISSUES=0
for file in $FILES_TO_CHECK; do
    if [ -f "$file" ]; then
        echo "检查: $file"
        if [ -n "$COMPILE_DB" ]; then
            # 使用编译数据库（使用 -p 参数指定包含 compile_commands.json 的目录）
            # 只检查 app、common、test 目录和 main.cpp，忽略系统头文件和第三方库
            OUTPUT=$(clang-tidy "$file" \
                --config-file=.clang-tidy \
                -p "$(dirname "$COMPILE_DB")" \
                --header-filter="^$(pwd)/(app|common|test)/.*|^$(pwd)/main\\.cpp$" \
                --system-headers=0 \
                --quiet \
                2>&1)
        else
            # 基本检查（不使用编译数据库）
            OUTPUT=$(clang-tidy "$file" \
                --config-file=.clang-tidy \
                --header-filter="^$(pwd)/(app|common|test)/.*|^$(pwd)/main\\.cpp$" \
                --system-headers=0 \
                --quiet \
                2>&1)
        fi
        
        # 只检查 app、common、test 目录和 main.cpp 的警告和错误
        # 过滤掉系统库、第三方库和编译器兼容性错误
        # 只保留文件路径以 app/、common/、test/ 开头或 main.cpp 的警告
        PROJECT_ISSUES=$(echo "$OUTPUT" | \
            grep -E "(warning|error):" | \
            grep -E "(app/|common/|test/|main\\.cpp)" | \
            grep -v "clang-diagnostic-error" | \
            grep -v "unknown argument" | \
            grep -v "not supported" | \
            grep -v "/third_party/" | \
            grep -v "/build/" | \
            grep -v "expanded from macro" || true)
        
        if [ -n "$PROJECT_ISSUES" ]; then
            # 只显示项目代码的警告和错误，排除宏展开的警告
            echo "$PROJECT_ISSUES"
            TIDY_ISSUES=$((TIDY_ISSUES + 1))
        fi
    fi
done

if [ $TIDY_ISSUES -eq 0 ]; then
    echo -e "${GREEN}代码质量检查通过${NC}\n"
else
    echo -e "${RED}✗ 发现 $TIDY_ISSUES 个文件有代码质量问题${NC}\n"
fi

# 总结
echo -e "${GREEN}=== 检查完成 ===${NC}"
TOTAL_ISSUES=$((FORMAT_ISSUES + TIDY_ISSUES))
if [ $TOTAL_ISSUES -eq 0 ]; then
    echo -e "${GREEN}所有检查通过！${NC}"
    exit 0
else
    echo -e "${YELLOW}发现 $TOTAL_ISSUES 个问题 (格式: $FORMAT_ISSUES, 质量: $TIDY_ISSUES)，请修复后重新检查${NC}"
    exit 1
fi

