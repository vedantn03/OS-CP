CC      ?= gcc
CFLAGS  ?= -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Iinclude -pthread
LDFLAGS ?= -lncurses -pthread -lrt

SRC = src/main.c \
      src/config.c \
      src/logger.c \
      src/scheduler.c \
      src/process.c \
      src/invariants.c \
      src/memory.c \
      src/io_buffer.c \
      src/interrupt.c \
      src/dashboard.c \
      src/clock.c \
      src/rng.c

OUT = exam_os

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT) $(OUT)_asan $(OUT)_tsan output/*.txt output/*.json

run: all
	./$(OUT)

demo: all
	./$(OUT) --demo

headless: all
	./$(OUT) --headless

test:
	sh tests/run_tests.sh

.PHONY: all clean run demo headless test
tsan: exam_os_tsan

exam_os_tsan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -g -O1 -fno-omit-frame-pointer -o $@ $(SRC) $(LDFLAGS) -fsanitize=thread -g -O1 -fno-omit-frame-pointer

.PHONY: tsan

asan: exam_os_asan

exam_os_asan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=address -g -O1 -fno-omit-frame-pointer -o $@ $(SRC) $(LDFLAGS) -fsanitize=address

.PHONY: asan
