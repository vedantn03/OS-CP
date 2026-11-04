CC      ?= gcc
CFLAGS  ?= -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Iinclude -pthread
LDFLAGS ?= -lncurses -pthread -lrt

SRC = $(wildcard src/*.c)

OUT = exam_os

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT) $(OUT)_asan $(OUT)_tsan pagesim disksim deadsim synclab synclab_tsan memlab fslab output/*.txt output/*.json

run: all
	./$(OUT)

demo: all
	./$(OUT) --demo

headless: all
	./$(OUT) --headless

test: all pagesim disksim deadsim synclab memlab fslab
	sh tests/run_tests.sh

compare: all
	bash scripts/compare.sh

gantt: all
	./$(OUT) --headless --strict --tick-ms 10 --trace --workload INTERACTIVE --students 8 --duration 200 --algo RR --seed 3
	bash scripts/gantt.sh

help:
	@echo "make run       Run the simulator"
	@echo "make headless  Run without the interactive display"
	@echo "make test      Run the test suite"
	@echo "make compare   Generate scheduler comparison results"
	@echo "make gantt     Generate a trace and ASCII Gantt chart"
	@echo "make pagesim   Build the offline page replacement simulator"
	@echo "make disksim   Build the offline disk scheduling simulator"
	@echo "make deadsim    Build the offline deadlock safety simulator"
	@echo "make synclab    Build the standalone synchronization lab"
	@echo "make synclab_tsan Build synclab with ThreadSanitizer"
	@echo "make memlab     Build the standalone memory allocation lab"
	@echo "make fslab      Build the standalone file allocation lab"

pagesim:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -o pagesim tools/pagesim.c

disksim:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Iinclude -o disksim tools/disksim.c src/disk_sched.c

deadsim:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -o deadsim tools/deadsim.c

synclab:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -pthread -o synclab tools/synclab.c

synclab_tsan:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -pthread -fsanitize=thread -g -O1 -fno-omit-frame-pointer -o synclab_tsan tools/synclab.c

memlab:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -o memlab tools/memlab.c

fslab:
	$(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -o fslab tools/fslab.c

.PHONY: all clean run demo headless test compare gantt help pagesim disksim deadsim synclab synclab_tsan memlab fslab
tsan: exam_os_tsan

exam_os_tsan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -g -O1 -fno-omit-frame-pointer -o $@ $(SRC) $(LDFLAGS) -fsanitize=thread -g -O1 -fno-omit-frame-pointer

.PHONY: tsan

asan: exam_os_asan

exam_os_asan: $(SRC)
	$(CC) $(CFLAGS) -fsanitize=address -g -O1 -fno-omit-frame-pointer -o $@ $(SRC) $(LDFLAGS) -fsanitize=address

.PHONY: asan
