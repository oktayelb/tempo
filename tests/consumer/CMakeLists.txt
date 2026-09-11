cmake_minimum_required(VERSION 3.20)

project(tempo_consumer LANGUAGES CXX)

find_package(tempo CONFIG REQUIRED)

add_executable(tempo-consumer main.cpp)
target_link_libraries(tempo-consumer PRIVATE tempo::tempo)
target_compile_features(tempo-consumer PRIVATE cxx_std_20)

enable_testing()
add_test(NAME consumer-smoke COMMAND tempo-consumer)
