#-Wall 代表 "所有警告"，当编译器发现程序中的代码在技术上可能没有错误，
#但在使用 C 语言时被认为是糟糕或有问题的，比如在初始化变量之前使用变量，编译器就会发出警告。
#-Wextra 和 -pedantic 会触发更多警告。
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
demo: demo.c
	$(CC) $(CFLAGS) demo.c -o demo
kilo: kilo.C
	$(CC) $(CFLAGS) kilo.c -o kilo
