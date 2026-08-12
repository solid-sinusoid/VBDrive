cmake_minimum_required(VERSION 3.22)
project(vbdrive_foc_cycle_sync_tests LANGUAGES CXX)

enable_testing()

add_executable(test_foc_cycle_sync
    test_foc_cycle_sync.cpp
    ../App/foc_cycle_sync.cpp
)
target_compile_features(test_foc_cycle_sync PRIVATE cxx_std_20)
target_compile_options(test_foc_cycle_sync PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME foc_cycle_sync COMMAND test_foc_cycle_sync)
