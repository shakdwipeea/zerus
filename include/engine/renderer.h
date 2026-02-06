//
// Zerus Game Engine - Renderer Module
//
// Target-agnostic rendering: records draw commands into any VkImage.
// Works with both swapchain images (windowed) and offscreen images (testing).
//
// After record_frame(), the image is left in
// VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL. The caller is responsible for the final
// layout transition:
//   - PRESENT_SRC_KHR for display (frame.h does this)
//   - TRANSFER_SRC_OPTIMAL for readback (tests do this)
//

#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan_core.h>

typedef struct
{
    VkClearColorValue clear_color;
} renderer_t;

renderer_t create_renderer()
{
    renderer_t renderer = {
        .clear_color = { .float32 = { 0.0f, 0.1f, 0.15f, 1.0f } },
    };
    return renderer;
}

// Records rendering commands into the given command buffer.
// Assumes the command buffer is already in the recording state.
// Leaves the image in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
void record_frame(renderer_t*     renderer,
                  VkCommandBuffer cmd,
                  VkImage         image,
                  VkFormat        format,
                  VkExtent2D      extent)
{
    (void) format;  // Reserved for future use (render passes)
    (void) extent;  // Reserved for future use (viewport/scissor)

    // Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier_to_clear = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier_to_clear);

    // Clear to the configured color
    VkImageSubresourceRange range = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    vkCmdClearColorImage(cmd,
                         image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &renderer->clear_color,
                         1,
                         &range);

    // Image is left in TRANSFER_DST_OPTIMAL.
    // Caller handles final transition.
}

void destroy_renderer(renderer_t* renderer)
{
    (void) renderer;
    // Nothing to clean up yet.
    // Future: destroy pipeline, render pass, descriptor sets, etc.
}

#endif  // RENDERER_H
