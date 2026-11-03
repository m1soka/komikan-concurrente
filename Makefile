CC = gcc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS = -Wall -Wextra -std=c17 -O2 -g
LDLIBS = -pthread
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,build/%.o,$(SOURCES))

.PHONY: all clean test unit integration asan tsan report zip
all: torneo

torneo: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@
build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -MMD -MP -c $< -o $@
-include $(OBJECTS:.o=.d)

build/test_board: tests/test_board.c src/board.c include/board.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_board.c src/board.c $(LDLIBS) -o $@
build/test_config_queue: tests/test_config_queue.c src/config.c src/queue.c include/config.h include/queue.h include/board.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_config_queue.c src/config.c src/queue.c $(LDLIBS) -o $@
build/test_game: tests/test_game.c src/game.c src/board.c src/config.c src/log.c include/game.h include/board.h include/config.h include/log.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_game.c src/game.c src/board.c src/config.c src/log.c $(LDLIBS) -o $@
build/libfailpthread.so: tests/fail_pthread_create.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $< -ldl -pthread -o $@
unit: build/test_board build/test_config_queue build/test_game
	./build/test_board
	./build/test_config_queue
	./build/test_game
integration: torneo build/libfailpthread.so
	python3 tests/test_integration.py

test: unit integration
asan:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) CFLAGS='-Wall -Wextra -std=c17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' all unit
	ASAN_OPTIONS=detect_leaks=0 python3 tests/test_integration.py --no-injection

tsan:
	$(MAKE) clean
	$(MAKE) CFLAGS='-Wall -Wextra -std=c17 -O1 -g -fsanitize=thread' all unit
	python3 tests/test_integration.py --no-injection
report:
	python3 tools/make_report.py
zip:
	python3 tools/package.py
clean:
	rm -rf build torneo
