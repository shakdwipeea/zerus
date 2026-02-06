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
    VULKAN_INSTANCE_FAILED,
    VULKAN_VALIDATION_NOT_FOUND,
    VULKAN_SURFACE_FAILED
} engine_error_t;

// Engine subsystems state
typedef struct zerus_engine_state_t
{
    bool           initialized;
    engine_error_t err;
    allocator*     alloc;

    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    device_info_t  device_info;
    surface_info_t surface_info;
    frame_state_t  frame_state;
    renderer_t     renderer;
} zerus_engine_state_t;


// Core engine functions
ZERUS_CORE_DEF zerus_engine_state_t zerus_engine_init(allocator*);
ZERUS_CORE_DEF bool zerus_engine_update(zerus_engine_state_t* engine);
ZERUS_CORE_DEF void zerus_engine_shutdown(zerus_engine_state_t* engine);
ZERUS_CORE_DEF void zerus_engine_start(zerus_engine_state_t* engine);

#endif  // ZERUS_CORE_H

//
// IMPLEMENTATION
//

#ifdef ZERUS_CORE_IMPLEMENTATION

#include <stdio.h>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"


// Private namespace for internal functions
#define zerus_core__ zerus_core__

string_t required_validation_layer = MAKE_STR("VK_LAYER_KHRONOS_validation");
string_t required_validation_extension
    = MAKE_STR(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);


bool check_validation_support()
{
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, NULL);

    VkLayerProperties layer_properties[256];
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

    return found;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
               VkDebugUtilsMessageTypeFlagsEXT             message_type,
               const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
               void*                                       p_user_data)
{
    (void) p_user_data;

    printf("message severity %d \n", message_severity);
    printf("message type: %d \n", message_type);
    printf("validation layer: %s \n", p_callback_data->pMessage);
    return VK_FALSE;
}

bool create_debug_utils_messenger(VkInstance                instance,
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

    create_info.pfnUserCallback = debug_callback;
    create_info.pUserData       = NULL;

    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT
        = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
    if (pfnCreateDebugUtilsMessengerEXT == NULL)
    {
        printf("failed to register debug callback");
        return false;
    }

    VkResult res = pfnCreateDebugUtilsMessengerEXT(
        instance, &create_info, nullptr, debug_messenger);
    if (res)
    {
        printf("failed to create debug messenger %d", res);
        return false;
    }

    return true;
}

void destroy_debug_utils_messenger(VkInstance               instance,
                                   VkDebugUtilsMessengerEXT debug_messenger)
{
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT
        = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (pfnDestroyDebugUtilsMessengerEXT == NULL)
    {
        return;
    }

    pfnDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
}


engine_error_t _init_vulkan(allocator* alloc, zerus_engine_state_t* engine)
{
    if (!check_validation_support())
    {
        printf("validation support not found");
        return VULKAN_VALIDATION_NOT_FOUND;
    }

    string_array_t* extensions = get_glfw_extensions(alloc);


    // create instance
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


    string_array_push(alloc, extensions, required_validation_extension);

    const VkInstanceCreateInfo instance_create_info
        = { .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = NULL,
            .pApplicationInfo        = &app_info,
            .enabledExtensionCount   = extensions->len,
            .ppEnabledExtensionNames = string_array_to_cstrings(extensions),
            .enabledLayerCount       = 1,
            .ppEnabledLayerNames     = (const char*[]) {
                string_to_cstring(&required_validation_layer) } };

    VkResult result
        = vkCreateInstance(&instance_create_info, NULL, &engine->instance);
    if (result)
    {
        printf("error creating vulkan instance");
        return VULKAN_INSTANCE_FAILED;
    }

    if (!create_debug_utils_messenger(engine->instance,
                                      &engine->debug_messenger))
    {
        return VULKAN_VALIDATION_NOT_FOUND;
    }

    engine->device_info = pick_device(alloc, engine->instance);
    if (engine->device_info.error)
    {
        printf("error creating device %d \n", engine->device_info.error);
        return VULKAN_INSTANCE_FAILED;
    }

    engine->surface_info
        = create_surface(alloc, engine->instance, engine->device_info);
    if (engine->surface_info.status)
    {
        printf("error creating surface %d \n", engine->surface_info.status);
        return VULKAN_SURFACE_FAILED;
    }

    engine->frame_state = create_frame_state(engine->device_info);
    if (engine->frame_state.status)
    {
        printf("error creating frame state %d \n", engine->frame_state.status);
        return VULKAN_INSTANCE_FAILED;
    }

    engine->renderer = create_renderer();

    printf("Vulkan instance created...\n");

    alloc->free(extensions, alloc->ctx);  // Free immediately after use
    return INIT_OK;
}

ZERUS_CORE_DEF
zerus_engine_state_t zerus_engine_init(allocator* alloc)
{
    zerus_engine_state_t state = { .initialized = true, .alloc = alloc };

    // Initialize subsystems
    printf("Initializing renderer... \n");
    state.err = _init_vulkan(alloc, &state);
    if (state.err)
    {
        printf("error in vulkan init %d", state.err);
        return state;
    }

    return state;
}

ZERUS_CORE_DEF bool zerus_engine_update(zerus_engine_state_t* engine)
{
    surface_status_t status = update_surface(&engine->surface_info);
    if (status == SURFACE_SHOULD_CLOSE)
    {
        return false;
    }

    frame_begin_result_t frame = begin_frame(&engine->frame_state,
                                             &engine->device_info,
                                             engine->surface_info.swapchain);
    if (frame.status == FRAME_SWAPCHAIN_OUT_OF_DATE)
    {
        // TODO: recreate swapchain
        return true;
    }

    record_frame(&engine->renderer,
                 frame.cmd,
                 engine->surface_info.images[frame.image_index],
                 engine->surface_info.image_format,
                 engine->surface_info.extent);

    end_frame(&engine->frame_state,
              &engine->device_info,
              engine->surface_info.swapchain,
              frame.image_index,
              engine->surface_info.images[frame.image_index]);

    return true;
}

ZERUS_CORE_DEF void zerus_engine_start(zerus_engine_state_t* engine)
{
    if (!engine->initialized)
    {
        return;
    }

    while (true)
    {
        if (!zerus_engine_update(engine))
        {
            zerus_engine_shutdown(engine);
            return;
        }
    }
}

ZERUS_CORE_DEF void zerus_engine_shutdown(zerus_engine_state_t* engine)
{
    printf("Shutting down subsystems...\n");

    if (engine->initialized)
    {
        vkDeviceWaitIdle(engine->device_info.device);

        destroy_renderer(&engine->renderer);
        destroy_frame_state(engine->device_info, &engine->frame_state);

        destroy_debug_utils_messenger(engine->instance,
                                      engine->debug_messenger);

        destroy_surface(engine->alloc,
                        engine->instance,
                        engine->device_info,
                        &engine->surface_info);

        // maybe should be in a function like free_device_info
        vkDestroyDevice(engine->device_info.device, nullptr);

        vkDestroyInstance(engine->instance, nullptr);

        engine->initialized = false;
    }
}


#endif  // ZERUS_CORE_IMPLEMENTATION
