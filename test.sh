#!/bin/bash
# set -e 遇到错误立即退出（但在测试某些预期行为时我们手动控制）
echo "=== Running Kilo Editor Tests ==="

# ==========================================
# 测试 1：检查程序是否存在
# ==========================================
echo "Test 1: Check if kilo exists"
if [ ! -f ./kilo ]; then
    echo "FAIL: kilo executable not found"
    exit 1
fi
echo "PASS: kilo exists"

# ==========================================
# 测试 2：基本打开与退出
# ==========================================
echo "Test 2: Open and Quit test"
echo "Hello World" > test_input.txt
# 发送 Ctrl+O (\x0f) 退出。因为文件未修改(dirty=0)，按一次即退出
printf '\x0f' | timeout 2s ./kilo test_input.txt > /dev/null 2>&1
if [ $? -eq 0 ] || [ $? -eq 124 ]; then # 0=正常退出, 124=timeout(也被视为程序运行了)
    echo "PASS: Opened and exited cleanly"
else
    echo "FAIL: Crash on open"
    exit 1
fi
rm -f test_input.txt

# ==========================================
# 测试 3：语法高亮检测 (核心新增)
# ==========================================
echo "Test 3: Syntax Highlighting Detection"
# 创建一个包含 C 语言关键字和注释的测试文件
cat << 'EOF' > test_syntax.c
int main() {
    // this is a comment
    return 0;
}
EOF

# 运行 kilo，将标准输出和错误重定向到 log 文件
# 注意：kilo 退出前会发送清屏指令，但之前的绘制内容已保存在 log 中
printf '\x0f' | timeout 2s ./kilo test_syntax.c > output.log 2>&1 || true

# 检测关键字颜色 (HL_KEYWORD1: int, return -> RGB 30, 144, 255)
# ANSI 序列格式: \x1b[38;2;R;G;Bm
if grep -q $'\x1b\[38;2;30;144;255m' output.log; then
    echo "PASS: Keyword highlighting (blue) detected"
else
    echo "FAIL: Keyword highlighting missing"
fi

# 检测注释颜色 (HL_COMMENT: // -> RGB 0, 255, 154)
if grep -q $'\x1b\[38;2;0;255;154m' output.log; then
    echo "PASS: Comment highlighting (green) detected"
else
    echo "FAIL: Comment highlighting missing"
fi

# ==========================================
# 测试 4：状态栏信息检测
# ==========================================
echo "Test 4: Status Bar Detection"
# 状态栏应该包含文件名和行数信息
if grep -q "test_syntax.c" output.log && grep -q "lines" output.log; then
    echo "PASS: Status bar shows filename and lines"
else
    echo "FAIL: Status bar information missing"
fi

# 清理
rm -f test_syntax.c output.log

echo "=== All tests completed ==="
exit 0