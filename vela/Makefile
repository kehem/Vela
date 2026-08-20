# Primary build when CMake is unavailable. Mirrors CMakeLists.txt.
CC      ?= gcc
PYTHON  ?= python3
PY_CFLAGS := $(shell $(PYTHON)-config --includes)
PY_LDFLAGS := $(shell $(PYTHON)-config --embed --ldflags)
SSL_LIBS := -lssl -lcrypto
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra -Wpedantic -fno-strict-overflow -D_GNU_SOURCE
CFLAGS  += -Iinclude $(PY_CFLAGS)
LDFLAGS ?= $(PY_LDFLAGS) $(SSL_LIBS) -lpthread

SRC := \
  src/core/log.c \
  src/core/util.c \
  src/core/buf.c \
  src/event/loop.c \
  src/http/parser.c \
  src/http/response.c \
  src/network/conn.c \
  src/network/server.c \
  src/python/embed.c \
  src/worker/master.c \
  src/metrics/metrics.c \
  src/websocket/ws.c \
  src/config/config.c \
  src/cli/main.c

OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test bin

all: bin/vela

bin/vela: $(OBJ)
	@mkdir -p bin
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/test_http_parser: tests/test_http_parser.c src/http/parser.c src/core/util.c src/core/buf.c
	@mkdir -p build/tests
	$(CC) $(CFLAGS) -o $@ tests/test_http_parser.c src/http/parser.c src/core/util.c src/core/buf.c

build/tests/test_config: tests/test_config.c src/config/config.c src/core/log.c src/core/util.c
	@mkdir -p build/tests
	$(CC) $(CFLAGS) -o $@ tests/test_config.c src/config/config.c src/core/log.c src/core/util.c -lpthread

build/tests/test_ws: tests/test_ws.c src/websocket/ws.c
	@mkdir -p build/tests
	$(CC) $(CFLAGS) -o $@ tests/test_ws.c src/websocket/ws.c $(SSL_LIBS)

test: build/tests/test_http_parser build/tests/test_config build/tests/test_ws
	./build/tests/test_http_parser
	./build/tests/test_config
	./build/tests/test_ws
	@echo ALL_TESTS_PASSED

clean:
	rm -rf build bin
