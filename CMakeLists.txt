cmake_minimum_required(VERSION 3.16)
project(vela VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -D_GNU_SOURCE)

find_package(Python3 REQUIRED COMPONENTS Development.Embed)
find_package(OpenSSL REQUIRED)

add_executable(vela
  src/core/log.c
  src/core/util.c
  src/core/buf.c
  src/event/loop.c
  src/http/parser.c
  src/http/response.c
  src/network/conn.c
  src/network/server.c
  src/python/embed.c
  src/worker/master.c
  src/metrics/metrics.c
  src/websocket/ws.c
  src/config/config.c
  src/cli/main.c
)
target_include_directories(vela PRIVATE include)
target_link_libraries(vela PRIVATE Python3::Python OpenSSL::SSL OpenSSL::Crypto pthread)

enable_testing()
add_executable(test_http_parser tests/test_http_parser.c src/http/parser.c src/core/util.c src/core/buf.c)
target_include_directories(test_http_parser PRIVATE include)
add_test(NAME http_parser COMMAND test_http_parser)

install(TARGETS vela RUNTIME DESTINATION bin)
