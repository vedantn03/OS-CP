CC      ?= gcc
CFLAGS  ?= -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Iinclude -pthread
LDFLAGS ?= -lncurses -pthread -lrt

SRC = src/main.c \
      src/config.c \
      src/logger.c \
      src/scheduler.c \
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
	rm -f $(OUT) output/*.txt output/*.json

run: all
	./$(OUT)

demo: all
	./$(OUT) --demo

headless: all
	./$(OUT) --headless

test:
	sh tests/run_tests.sh

.PHONY: all clean run demo headless test