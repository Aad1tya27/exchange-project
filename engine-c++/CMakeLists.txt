cmake_minimum_required(VERSION 3.16)

project(whiskers_engine_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(engine_core
    src/engine.cpp
    src/orderbook.cpp
    src/redis_client.cpp
    src/redis_protocol.cpp
)

target_include_directories(engine_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(engine_tests
    tests/engine_tests.cpp
    tests/orderbook_tests.cpp
)

target_link_libraries(engine_tests PRIVATE engine_core)

add_executable(engine_app
    src/main.cpp
)

target_link_libraries(engine_app PRIVATE engine_core)
