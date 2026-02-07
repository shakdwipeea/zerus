/*
 * Zerus Game Engine - Core Module
 *
 * A cross-platform game engine core system
 *
 * LICENSE:
 * This software is dual-licensed to the public domain and under the following
 * license: you are granted a perpetual, irrevocable license to copy, modify,
 * publish, and distribute this file as you see fit.
 *
 * USAGE:
 * Do this:
 *    #define ZERUS_CORE_IMPLEMENTATION
 * before you include this file in *one* C or C++ file to create the
 * implementation.
 *
 * // i.e. it should look like this:
 * #include ...
 * #include ...
 * #include ...
 * #define ZERUS_CORE_IMPLEMENTATION
 * #include "engine/core.h"
 *
 * You can #define ZERUS_CORE_STATIC before the #include to make the
 * implementation private to the source file that creates it.
 */

#ifndef ZERUS_CORE_H
#define ZERUS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan_core.h>

#include "prelude.h"
#include "device.h"
#include "surface.h"
#include "frame.h"
#include "renderer.h"
#include "gpu_image.h"

// Engine version
#define ENGINE_VERSION_MAJOR 1
#define ENGINE_VERSION_MINOR 0
#define ENGINE_VERSION_PATCH 0

// Configuration
#ifndef ZERUS_CORE_DEF
#ifdef ZERUS_CORE_STATIC
#define ZERUS_CORE_DEF static
#else
#define ZERUS_CORE_DEF extern
#endif
#endif

typedef enum
{
    INIT_OK,
    GLFW_INIT_FAILED,
    GLFW_VULKAN_NOT_SUPPORTED,
    VULKAN_INSTANCE_FAILED,
    VULKAN_VALIDATION_NOT_FOUND,
    VULKAN_DEBUG_MESSENGER_FAILED,
    VULKAN_DEVICE_FAILED,
    VULKAN_SURFACE_FAILED,
    VULKAN_FRAME_STATE_FAILED,
} engine_error_t;

typedef enum
{
    ENGINE_UPDATE_OK,            // frame completed, keep going
    ENGINE_UPDATE_SHOULD_CLOSE,  // window close requested (graceful)
    ENGINE_UPDATE_ERROR,         // unrecoverable frame/device error
} engine_update_status_t;

typedef struct
{
    bool                                 headless;
    window_config_t                      window;
    bool                                 enable_validation;
    PFN_vkDebugUtilsMessengerCallbackEXT debug_callback;
} engine_config_t;

// Default initializer – use this (or zerus_engine_default_config()) to get a
// fully-populated config.  Passing a zero-initialized engine_config_t directly
// to zerus_engine_init is allowed but fields will keep their zero values.
#define ENGINE_CONFIG_DEFAULT                       \
    {                                               \
        .headless          = false,                 \
        .window            = { .width   = 800,      \
                               .height  = 600,      \
                               .title   = "Zerus",  \
                               .visible = true },   \
        .enable_validation = true,                  \
        .debug_callback    = NULL,                  \
    }

// Engine subsystems state
typedef struct zerus_engine_state_t
{
    bool            initialized;
    bool            glfw_initialized;
    bool            validation_enabled;
    engine_error_t  err;
    allocator*      alloc;
    engine_config_t config;

    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    device_info_t  device_info;
    surface_info_t surface_info;
    frame_state_t  frame_state;
    renderer_t     renderer;
} zerus_engine_state_t;

// Core engine functions
ZERUS_CORE_DEF engine_config_t      zerus_engine_default_config(void);
ZERUS_CORE_DEF zerus_engine_state_t zerus_engine_init(allocator*,
                                                      engine_config_t);
ZERUS_CORE_DEF engine_update_status_t zerus_engine_update(zerus_engine_state_t* engine);
ZERUS_CORE_DEF void zerus_engine_shutdown(zerus_engine_state_t* engine);
ZERUS_CORE_DEF void zerus_engine_start(zerus_engine_state_t* engine);

#endif  // ZERUS_CORE_H

//
// IMPLEMENTATION
//

#ifdef ZERUS_CORE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

// Private namespace for internal functions
#define zerus_core__ zerus_core__

static string_t required_validation_layer
    = MAKE_STR("VK_LAYER_KHRONOS_validation");
static string_t required_validation_extension
    = MAKE_STR(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

static VKAPI_ATTR VkBool32 VKAPI_CALL default_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void*                                       p_user_data)
{
    (void) p_user_data;

    printf("message severity: %d\n", message_severity);
    printf("message type: %d\n", message_type);
    printf("validation layer: %s\n", p_callback_data->pMessage);
    return VK_FALSE;
}

bool check_validation_support()
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    if (count == 0)
    {
        return false;
    }

    VkLayerProperties* layer_properties
        = (VkLayerProperties*) malloc(count * sizeof(VkLayerProperties));
    if (!layer_properties)
    {
        return false;
    }

    vkEnumerateInstanceLayerProperties(&count, layer_properties);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
    {
        VkLayerProperties layer = layer_properties[i];

        string_t layer_name
            = { .chars = layer.layerName,
                .len   = find_length_of_c_string(layer.layerName) };

        if (string_equal(layer_name, required_validation_layer))
        {
            found = true;
            break;
        }
    }

    free(layer_properties);
    return found;
}

bool create_debug_utils_messenger(VkInstance                           instance,
                                  PFN_vkDebugUtilsMessengerCallbackEXT callback,
                                  VkDebugUtilsMessengerEXT* debug_messenger)
{
    VkDebugUtilsMessengerCreateInfoEXT create_info = { 0 };
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    create_info.messageSeverity
        = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    create_info.pfnUserCallback = callback ? callback : default_debug_callback;
    create_info.pUserData       = NULL;

    PFN_vkCreateDebugUtilsMessengerEXT create_fn
        = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
    if (create_fn == NULL)
    {
        printf("failed to load vkCreateDebugUtilsMessengerEXT\n");
        return false;
    }

    VkResult res = create_fn(instance, &create_info, NULL, debug_messenger);
    if (res != VK_SUCCESS)
    {
        printf("failed to create debug messenger: %d\n", res);
        return false;
    }

    return true;
}

void destroy_debug_utils_messenger(VkInstance               instance,
                                   VkDebugUtilsMessengerEXT debug_messenger)
{
    if (instance == VK_NULL_HANDLE || debug_messenger == VK_NULL_HANDLE)
    {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn
        = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_fn == NULL)
    {
        return;
    }

    destroy_fn(instance, debug_messenger, NULL);
}

static string_array_t* collect_instance_extensions(
    allocator* alloc, const engine_config_t* config, bool validation_enabled)
{
    string_array_t* extensions = NULL;

    if (config->headless)
    {
        // Headless mode intentionally starts from an empty extension set so it
        // can run on systems that only provide compute/offscreen capabilities.
        extensions = make_string_array(alloc, validation_enabled ? 1 : 0);
    }
    else
    {
        extensions = get_glfw_extensions(alloc);
    }

    if (!extensions)
    {
        return NULL;
    }

    if (validation_enabled)
    {
        if (!string_array_push(
                alloc, extensions, required_validation_extension))
        {
            string_array_free(alloc, extensions);
            return NULL;
        }
    }

    return extensions;
}

engine_error_t _init_vulkan(allocator* alloc, zerus_engine_state_t* engine)
{
    const engine_config_t* config = &engine->config;

    if (!config->headless)
    {
        if (!glfwInit())
        {
            return GLFW_INIT_FAILED;
        }
        engine->glfw_initialized = true;

        if (!glfwVulkanSupported())
        {
            return GLFW_VULKAN_NOT_SUPPORTED;
        }
    }

    bool validation_enabled = false;
    if (config->enable_validation)
    {
        if (!check_validation_support())
        {
            return VULKAN_VALIDATION_NOT_FOUND;
        }
        validation_enabled = true;
    }

    string_array_t* extensions
        = collect_instance_extensions(alloc, config, validation_enabled);
    if (!extensions)
    {
        return VULKAN_INSTANCE_FAILED;
    }

    const VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = NULL,
        .pApplicationName   = "Zerus Engine",
        .applicationVersion = VK_MAKE_VERSION(
            ENGINE_VERSION_MAJOR, ENGINE_VERSION_MINOR, ENGINE_VERSION_PATCH),
        .engineVersion = VK_MAKE_VERSION(
            ENGINE_VERSION_MAJOR, ENGINE_VERSION_MINOR, ENGINE_VERSION_PATCH),
        .apiVersion = VK_API_VERSION_1_4,
    };

    const char* const validation_layers[]
        = { string_to_cstring(&required_validation_layer) };

    const VkInstanceCreateInfo instance_create_info
        = { .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = NULL,
            .pApplicationInfo        = &app_info,
            .enabledExtensionCount   = extensions->len,
            .ppEnabledExtensionNames = string_array_to_cstrings(extensions),
            .enabledLayerCount       = validation_enabled ? 1 : 0,
            .ppEnabledLayerNames
            = validation_enabled ? validation_layers : NULL };

    VkResult result
        = vkCreateInstance(&instance_create_info, NULL, &engine->instance);
    if (result != VK_SUCCESS)
    {
        string_array_free(alloc, extensions);
        return VULKAN_INSTANCE_FAILED;
    }

    if (validation_enabled)
    {
        if (!create_debug_utils_messenger(engine->instance,
                                          config->debug_callback,
                                          &engine->debug_messenger))
        {
            string_array_free(alloc, extensions);
            return VULKAN_DEBUG_MESSENGER_FAILED;
        }
        engine->validation_enabled = true;
    }

    engine->device_info
        = pick_device(alloc, engine->instance, config->headless);
    if (engine->device_info.error != DEVICE_OK)
    {
        printf("error creating device %d\n", engine->device_info.error);
        string_array_free(alloc, extensions);
        return VULKAN_DEVICE_FAILED;
    }

    if (!config->headless)
    {
        engine->surface_info = create_surface(
            alloc, engine->instance, engine->device_info, config->window);
        if (engine->surface_info.status != SURFACE_OK)
        {
            printf("error creating surface %d\n", engine->surface_info.status);
            string_array_free(alloc, extensions);
            return VULKAN_SURFACE_FAILED;
        }
    }

    engine->frame_state = create_frame_state(engine->device_info);
    if (engine->frame_state.status != FRAME_OK)
    {
        printf("error creating frame state %d\n", engine->frame_state.status);
        string_array_free(alloc, extensions);
        return VULKAN_FRAME_STATE_FAILED;
    }

    engine->renderer = create_renderer();

    printf("Vulkan instance created...\n");

    string_array_free(alloc, extensions);
    return INIT_OK;
}

ZERUS_CORE_DEF engine_config_t zerus_engine_default_config(void)
{
    engine_config_t config = ENGINE_CONFIG_DEFAULT;
    return config;
}

ZERUS_CORE_DEF zerus_engine_state_t zerus_engine_init(allocator*      alloc,
                                                      engine_config_t config)
{
    zerus_engine_state_t state = {
        .initialized = true,
        .alloc       = alloc,
        .config      = config,
    };

    printf("Initializing renderer...\n");
    state.err = _init_vulkan(alloc, &state);
    if (state.err != INIT_OK)
    {
        printf("error in vulkan init %d\n", state.err);
        zerus_engine_shutdown(&state);
        state.initialized = false;
        return state;
    }

    return state;
}

ZERUS_CORE_DEF engine_update_status_t zerus_engine_update(
    zerus_engine_state_t* engine)
{
    if (!engine || !engine->initialized)
    {
        return ENGINE_UPDATE_ERROR;
    }

    if (engine->config.headless)
    {
        // Headless mode does not own a window event pump. Callers drive work
        // directly with frame_begin_recording/frame_end_and_submit.
        return ENGINE_UPDATE_OK;
    }

    surface_status_t status = update_surface(&engine->surface_info);
    if (status == SURFACE_SHOULD_CLOSE)
    {
        return ENGINE_UPDATE_SHOULD_CLOSE;
    }

    frame_begin_result_t frame = begin_frame(&engine->frame_state,
                                             &engine->device_info,
                                             engine->surface_info.swapchain);
    if (frame.status == FRAME_SWAPCHAIN_OUT_OF_DATE)
    {
        // TODO: recreate swapchain
        return ENGINE_UPDATE_OK;
    }
    if (frame.status != FRAME_OK)
    {
        return ENGINE_UPDATE_ERROR;
    }

    record_frame(&engine->renderer,
                 frame.cmd,
                 engine->surface_info.images[frame.image_index],
                 engine->surface_info.image_format,
                 engine->surface_info.extent);

    frame_status_t frame_status
        = end_frame(&engine->frame_state,
                    &engine->device_info,
                    engine->surface_info.swapchain,
                    frame.image_index,
                    engine->surface_info.images[frame.image_index]);
    if (frame_status == FRAME_SWAPCHAIN_OUT_OF_DATE)
    {
        // TODO: recreate swapchain
        return ENGINE_UPDATE_OK;
    }

    return frame_status == FRAME_OK ? ENGINE_UPDATE_OK : ENGINE_UPDATE_ERROR;
}

ZERUS_CORE_DEF void zerus_engine_start(zerus_engine_state_t* engine)
{
    if (!engine || !engine->initialized)
    {
        return;
    }

    if (engine->config.headless)
    {
        printf("zerus_engine_start is not available in headless mode\n");
        return;
    }

    while (true)
    {
        engine_update_status_t status = zerus_engine_update(engine);
        if (status != ENGINE_UPDATE_OK)
        {
            if (status == ENGINE_UPDATE_ERROR)
            {
                printf("engine update error, shutting down\n");
            }
            zerus_engine_shutdown(engine);
            return;
        }
    }
}

ZERUS_CORE_DEF void zerus_engine_shutdown(zerus_engine_state_t* engine)
{
    if (!engine)
    {
        return;
    }

    printf("Shutting down subsystems...\n");

    if (!engine->initialized)
    {
        return;
    }

    if (engine->device_info.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(engine->device_info.device);

        destroy_renderer(&engine->renderer);
        destroy_frame_state(engine->device_info, &engine->frame_state);

        if (!engine->config.headless && engine->surface_info.window
            && engine->surface_info.surface != VK_NULL_HANDLE)
        {
            destroy_surface(engine->alloc,
                            engine->instance,
                            engine->device_info,
                            &engine->surface_info);
            engine->glfw_initialized = false;
        }

        vkDestroyDevice(engine->device_info.device, NULL);
        engine->device_info.device = VK_NULL_HANDLE;
    }

    if (!engine->config.headless && engine->surface_info.window
        && engine->surface_info.surface == VK_NULL_HANDLE)
    {
        // Surface creation can fail after window creation; this path avoids
        // leaking that temporary window during partial initialization failures.
        glfwDestroyWindow(engine->surface_info.window);
        engine->surface_info.window = NULL;
    }

    if (!engine->config.headless && engine->glfw_initialized)
    {
        glfwTerminate();
        engine->glfw_initialized = false;
    }

    destroy_debug_utils_messenger(engine->instance, engine->debug_messenger);
    engine->debug_messenger = VK_NULL_HANDLE;

    if (engine->instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(engine->instance, NULL);
        engine->instance = VK_NULL_HANDLE;
    }

    engine->initialized = false;
}

#endif  // ZERUS_CORE_IMPLEMENTATION
