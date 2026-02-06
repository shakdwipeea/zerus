//
// Zerus Game Engine - Surface Module
//
// GLFW window, Vulkan surface, swapchain, and image view management.
//

#ifndef SURFACE_H
#define SURFACE_H
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include "prelude.h"
#include "GLFW/glfw3.h"

typedef enum
{
    SURFACE_OK,
    SURFACE_SHOULD_CLOSE,
    SURFACE_CREATION_FAILED,
    SURFACE_FORMAT_NOT_FOUND,
    SURFACE_PRESENT_MODE_NOT_FOUND,
    SURFACE_SWAPCHAIN_CREATION_FAILED,
    SURFACE_SWAPCHAIN_IMAGES_NOT_FOUND
} surface_status_t;


GLFWwindow* make_window()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Zerus", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Failed to create GLFW window\n");
        return NULL;
    }

    // On Wayland/Hyprland, explicitly setting the window size after creation
    // nudges the compositor into acknowledging the requested dimensions.
    glfwSetWindowSize(window, 800, 600);

    return window;
}

// On Wayland, a newly created window can report framebuffer size 0x0 until
// the compositor is ready. Poll events until we get a real size.
void wait_for_window_ready(GLFWwindow* window)
{
    int width = 0, height = 0;
    while (width == 0 || height == 0)
    {
        glfwPollEvents();
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0)
        {
            glfwWaitEventsTimeout(0.01);
        }
    }
    printf("Framebuffer ready: %dx%d\n", width, height);
}

string_array_t* get_glfw_extensions(allocator* alloc)
{
    uint32_t     count           = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&count);

    string_array_t* extensions = make_string_array(alloc, count + 1);

    for (uint32_t i = 0; i < count; i++)
    {
        extensions->data[i] = make_from_c_string(glfw_extensions[i]);
        extensions->len++;
    }

    return extensions;
}

typedef struct
{
    surface_status_t status;
    GLFWwindow*      window;

    VkSurfaceKHR   surface;
    VkSwapchainKHR swapchain;

    VkFormat   image_format;
    VkExtent2D extent;

    uint32_t     image_count;
    VkImage*     images;
    VkImageView* views;
} surface_info_t;

surface_info_t create_surface(allocator*    alloc,
                              VkInstance    instance,
                              device_info_t device_info)
{
    surface_info_t surface_info = { 0 };
    surface_info.window         = make_window();

    if (!surface_info.window)
    {
        surface_info.status = SURFACE_CREATION_FAILED;
        return surface_info;
    }

    // Wait until the compositor gives us a real framebuffer size.
    // On Wayland/Hyprland this can be 0x0 initially.
    wait_for_window_ready(surface_info.window);

    VkResult res = glfwCreateWindowSurface(
        instance, surface_info.window, NULL, &surface_info.surface);
    if (res != VK_SUCCESS)
    {
        surface_info.status = SURFACE_CREATION_FAILED;
        return surface_info;
    }


    // choose surface format
    uint32_t surface_format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_info.physical_device,
                                         surface_info.surface,
                                         &surface_format_count,
                                         NULL);

    if (surface_format_count == 0)
    {
        surface_info.status = SURFACE_FORMAT_NOT_FOUND;
        return surface_info;
    }

    VkSurfaceFormatKHR* surface_formats = alloc->malloc(
        surface_format_count * sizeof(VkSurfaceFormatKHR), alloc->ctx);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_info.physical_device,
                                         surface_info.surface,
                                         &surface_format_count,
                                         surface_formats);

    VkSurfaceFormatKHR choosen_surface_format = { 0 };
    for (uint32_t i = 0; i < surface_format_count; i++)
    {
        VkSurfaceFormatKHR format = surface_formats[i];

        if (format.format == VK_FORMAT_B8G8R8A8_SRGB
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            choosen_surface_format = format;
            break;
        }
    }

    if (choosen_surface_format.format == VK_FORMAT_UNDEFINED)
    {
        printf("could not find the surface format we want");
        choosen_surface_format = surface_formats[0];
    }

    // find swapchain extents
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_info.physical_device,
                                              surface_info.surface,
                                              &surface_capabilities);

    int width, height;
    glfwGetFramebufferSize(surface_info.window, &width, &height);

    width  = clamp(width,
                  surface_capabilities.minImageExtent.width,
                  surface_capabilities.maxImageExtent.width);
    height = clamp(height,
                   surface_capabilities.minImageExtent.height,
                   surface_capabilities.maxImageExtent.height);

    uint32_t image_count = surface_capabilities.minImageCount + 1;
    if (image_count > surface_capabilities.maxImageCount)
    {
        image_count = surface_capabilities.maxImageCount;
    }

    // choose present mode
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_info.physical_device,
                                              surface_info.surface,
                                              &present_mode_count,
                                              NULL);
    if (present_mode_count == 0)
    {
        surface_info.status = SURFACE_PRESENT_MODE_NOT_FOUND;
        return surface_info;
    }

    VkPresentModeKHR* present_modes = alloc->malloc(
        present_mode_count * sizeof(VkPresentModeKHR), alloc->ctx);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_info.physical_device,
                                              surface_info.surface,
                                              &present_mode_count,
                                              present_modes);

    VkPresentModeKHR choosen_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < present_mode_count; i++)
    {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            choosen_present_mode = present_modes[i];
            break;
        }
    }


    VkSwapchainCreateInfoKHR create_info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = NULL,
        .surface          = surface_info.surface,
        .minImageCount    = image_count,
        .imageFormat      = choosen_surface_format.format,
        .imageColorSpace  = choosen_surface_format.colorSpace,
        .imageExtent      = (VkExtent2D) { width, height },
        .imageArrayLayers = 1,
        .imageUsage
        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = surface_capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = choosen_present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };

    res = vkCreateSwapchainKHR(
        device_info.device, &create_info, nullptr, &surface_info.swapchain);
    if (res != VK_SUCCESS)
    {
        printf("failed to create swapchain object\n");
        surface_info.status = SURFACE_SWAPCHAIN_CREATION_FAILED;
        return surface_info;
    }

    res = vkGetSwapchainImagesKHR(device_info.device,
                                  surface_info.swapchain,
                                  &surface_info.image_count,
                                  NULL);
    if (res != VK_SUCCESS)
    {
        surface_info.status = SURFACE_SWAPCHAIN_IMAGES_NOT_FOUND;
        return surface_info;
    }

    surface_info.images
        = alloc->malloc(image_count * sizeof(VkImage), alloc->ctx);
    res = vkGetSwapchainImagesKHR(device_info.device,
                                  surface_info.swapchain,
                                  &image_count,
                                  surface_info.images);
    if (res != VK_SUCCESS)
    {
        surface_info.status = SURFACE_SWAPCHAIN_IMAGES_NOT_FOUND;
        return surface_info;
    }

    VkImageViewCreateInfo view_info = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = choosen_surface_format.format,
        .components       = (VkComponentMapping) {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount      = 1,
            .baseArrayLayer  = 0,
            .layerCount = 1,
        }
    };

    surface_info.views
        = alloc->malloc(image_count * sizeof(VkImageView), alloc->ctx);
    for (uint32_t i = 0; i < image_count; i++)
    {
        view_info.image = surface_info.images[i];
        // todo add error handling ?
        vkCreateImageView(
            device_info.device, &view_info, nullptr, &surface_info.views[i]);
    }

    surface_info.image_format = choosen_surface_format.format;
    surface_info.extent       = (VkExtent2D) { width, height };

    return surface_info;
}

surface_status_t update_surface(surface_info_t* surface)
{
    if (glfwWindowShouldClose(surface->window))
    {
        return SURFACE_SHOULD_CLOSE;
    }

    glfwPollEvents();

    return SURFACE_OK;
}

void destroy_surface(allocator*      alloc,
                     VkInstance      instance,
                     device_info_t   device_info,
                     surface_info_t* surface)
{
    for (uint32_t i = 0; i < surface->image_count; i++)
    {
        vkDestroyImageView(device_info.device, surface->views[i], nullptr);
    }

    vkDestroySwapchainKHR(device_info.device, surface->swapchain, nullptr);

    alloc->free(surface->images, alloc->ctx);
    alloc->free(surface->views, alloc->ctx);

    vkDestroySurfaceKHR(instance, surface->surface, nullptr);

    glfwDestroyWindow(surface->window);
    glfwTerminate();
}

#endif  // SURFACE_H
