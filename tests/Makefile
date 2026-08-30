# 算法层主机端单元测试
#
# 用途: 在 PC (Linux/macOS/WSL) 上编译运行 crouse.c/algorithms.c 的单元测试,
#       不依赖任何嵌入式工具链或硬件。
# 运行: make -C tests

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
CFLAGS  += -Istub -I../crouse.h
LDLIBS  = -lm

SRCS = test_algorithms.c ../crouse.c/algorithms.c
BIN  = test_algorithms

all: run

run: $(BIN)
	./$(BIN)

$(BIN): $(SRCS) stub/FreeRTOS.h stub/task.h stub/systick.h
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDLIBS)

clean:
	rm -f $(BIN)

.PHONY: all run clean
