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

add_executable(test_monotonic_micros
    test_monotonic_micros.cpp
)
target_compile_features(test_monotonic_micros PRIVATE cxx_std_20)
target_compile_options(test_monotonic_micros PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME monotonic_micros COMMAND test_monotonic_micros)

add_executable(test_disable_diagnostics
    test_disable_diagnostics.cpp
)
target_compile_features(test_disable_diagnostics PRIVATE cxx_std_20)
target_compile_options(test_disable_diagnostics PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME disable_diagnostics COMMAND test_disable_diagnostics)
