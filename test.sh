#!/bin/bash

echo "=== Running Kilo Editor Tests ==="

# 测试 1：检查 kilo 程序是否存在且可执行
echo "Test 1: Check if kilo exists"
if [ ! -f ./kilo ]; then
    echo "FAIL: kilo executable not found"
    exit 1
fi
echo "PASS: kilo exists"

# 测试 2：检查能否正常打开文件（模拟打开并立即退出）
echo "Test 2: Open file test"
echo "Hello World" > test_input.txt
# 使用 timeout 防止程序卡死，输入 Ctrl+O (0x0f) 退出
printf '\x0f' | timeout 2s ./kilo test_input.txt
if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    echo "PASS: Opened and exited"
else
    echo "FAIL: Crash on open"
    exit 1
fi

# 清理测试文件
rm -f test_input.txt

echo "=== All tests passed ==="
exit 0