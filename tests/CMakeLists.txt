# Zerus Engine - Tests

# Unit tests for prelude utilities (no GPU needed)
add_executable(test_prelude test_prelude.c)
target_include_directories(test_prelude PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_prelude m)
add_test(NAME test_prelude COMMAND test_prelude)

# Headless renderer E2E test (Vulkan required, no window/display needed)
add_executable(test_renderer test_renderer.c)
target_include_directories(test_renderer PRIVATE ${CMAKE_SOURCE_DIR}/include)
# test_renderer is headless, but it includes engine/core.h which compiles the
# surface module in the same translation unit, so GLFW remains a link-time dep.
target_link_libraries(test_renderer vulkan glfw m)
add_test(NAME test_renderer COMMAND test_renderer)

# Swapchain frame-loop test via GLFW surface.
# Prefer running under Xvfb; this avoids Wayland/display coupling while still
# exercising real acquire/present semaphore usage.
add_executable(test_swapchain test_swapchain.c)
target_include_directories(test_swapchain PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_swapchain vulkan glfw m)

find_program(XVFB_RUN_EXECUTABLE NAMES xvfb-run)
if (XVFB_RUN_EXECUTABLE)
    add_test(
        NAME test_swapchain
        COMMAND ${CMAKE_COMMAND} -E env
                --unset=WAYLAND_DISPLAY
                --unset=XDG_SESSION_TYPE
                ${XVFB_RUN_EXECUTABLE}
                -a
                -s
                "-screen 0 1280x720x24"
                $<TARGET_FILE:test_swapchain>
    )
else ()
    add_test(NAME test_swapchain COMMAND test_swapchain)
endif ()

# Suppress external library leaks in ASan (NVIDIA driver, dbus, etc.)
set_tests_properties(test_renderer test_swapchain PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.txt"
)
