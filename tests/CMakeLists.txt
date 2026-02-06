# Zerus Engine - Tests

# Unit tests for prelude utilities (no GPU needed)
add_executable(test_prelude test_prelude.c)
target_include_directories(test_prelude PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_prelude m)
add_test(NAME test_prelude COMMAND test_prelude)

# Headless renderer E2E test (Vulkan required, no window/display needed)
add_executable(test_renderer test_renderer.c)
target_include_directories(test_renderer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_renderer vulkan m)
add_test(NAME test_renderer COMMAND test_renderer)

# Suppress external library leaks in ASan (NVIDIA driver, dbus, etc.)
set_tests_properties(test_renderer PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.txt"
)
