
#include "Renderer.h"
#include "VertexData.h"

#pragma comment( lib, "C:\\proj\\VulkanSDK\\1.3.239.0\\Lib\\vulkan-1.lib" )
#ifdef _DEBUG
#pragma comment( lib, "C:\\proj\\VulkanSDK\\1.3.239.0\\Lib\\glslang-default-resource-limitsd.lib" )
#pragma comment( lib, "C:\\proj\\VulkanSDK\\1.3.239.0\\Lib\\shaderc_combinedd.lib" )
#else
#pragma comment( lib, "C:\\proj\\VulkanSDK\\1.3.239.0\\Lib\\glslang-default-resource-limits.lib" )
#pragma comment( lib, "C:\\proj\\VulkanSDK\\1.3.239.0\\Lib\\shaderc_combined.lib" )
#endif

#include <array>
#include <iostream>
#include <vector>

#include "ShaderUtil.h"

static VkFence create_fence(VkDevice device, VkFenceCreateFlags flags) {
    VkFenceCreateInfo create_info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, flags };
    VkFence fence;
    VkResult res = vkCreateFence(device, &create_info, nullptr, &fence);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return fence;
}

static VkSemaphore create_semaphore(VkDevice device, VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, flags };
    VkSemaphore semaphore;
    VkResult res = vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return semaphore;
}

static uint32_t get_queue_family_index(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    std::vector<VkQueueFamilyProperties> queue_family_properties;
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    queue_family_properties.resize(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_family_properties.data());

    for (uint32_t i = 0; i < (uint32_t) queue_family_properties.size(); ++i) {
        if (queue_family_properties[i].queueCount == 0) {
            continue;
        }
        if (!(queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }

        VkBool32 supported;
        VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &supported);
        if (res != VK_SUCCESS) {
            continue;
        }

        if (supported == VK_TRUE) {
            return i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

Render::~Render() {
    if (!m_init) {
        return;
    }
    vkDeviceWaitIdle(m_device);
    for (VkImageView image_view : m_image_views) {
        vkDestroyImageView(m_device, image_view, nullptr);
    }
    m_image_views.clear();
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    vkDestroyCommandPool(m_device, m_command_pool, nullptr);
    vkDestroyPipeline(m_device, m_pipeline[0], nullptr);
    vkDestroyPipeline(m_device, m_pipeline[1], nullptr);
    vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);

    vkUnmapMemory(m_device, m_uniform_buffer_memory);
    vkDestroyBuffer(m_device, m_uniform_buffer, nullptr);
    vkFreeMemory(m_device, m_uniform_buffer_memory, nullptr);

    vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
    for (VkFence fence : m_queue_submit_fence) {
        vkDestroyFence(m_device, fence, nullptr);
    }
    for (VkSemaphore semaphore : m_swapchain_acquire_semaphore) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_swapchain_release_semaphore) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    vkDestroyBuffer(m_device, m_vertex_buffer, nullptr);
    vkFreeMemory(m_device, m_vertex_buffer_memory, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

bool Render::resize() {
    if (m_device == VK_NULL_HANDLE) {
        return false;
    }

    VkSurfaceCapabilitiesKHR surface_capabilities;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);
    if (res != VK_SUCCESS) {
        return false;
    }

    if (surface_capabilities.currentExtent.width == m_extent.width && surface_capabilities.currentExtent.height == m_extent.height) {
        return false;
    }

    vkDeviceWaitIdle(m_device);

    m_extent = surface_capabilities.currentExtent;

    create_swapchain(m_swapchain, surface_capabilities);
    if (m_swapchain == VK_NULL_HANDLE) {
        return false;
    }

    // apparently command buffers can reference the old swap chain, so they need to be recreated here? source?
    vkFreeCommandBuffers(m_device, m_command_pool, static_cast<uint32_t>(m_command_buffers.size()), m_command_buffers.data());
    create_command_buffers();

    m_frame = 0;

    return true;
}

VkResult Render::acquire_next_image(uint32_t frame, uint32_t & image_index) {

    VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_swapchain_acquire_semaphore[frame], VK_NULL_HANDLE, &image_index);
    if (res != VK_SUCCESS) {
        return res;
    }

    res = vkWaitForFences(m_device, 1, &m_queue_submit_fence[image_index], true, UINT64_MAX);
    if (res != VK_SUCCESS) {
        return res;
    }
    res = vkResetFences(m_device, 1, &m_queue_submit_fence[image_index] );
    if (res != VK_SUCCESS) {
        return res;
    }

    return VK_SUCCESS;
}

void Render::draw() {
    uint32_t index;
    VkResult res = acquire_next_image(m_frame, index);
    m_frame += 1;
    m_frame %= IMAGE_COUNT;

    // Handle outdated error in acquire.
    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
        resize();
        res = acquire_next_image(m_frame, index);
    }

    if (res != VK_SUCCESS) {
        vkQueueWaitIdle(m_queue);
        return;
    }

    if (!render(index)) {
        return;
    }

    // present
    VkPresentInfoKHR present_info{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr,
        1, &m_swapchain_release_semaphore[index],   // wait semaphores
        1, &m_swapchain,                            // swapchains
        &index,                                     // image indexes
        nullptr                                     // pResults
    };
    res = vkQueuePresentKHR(m_queue, &present_info);

    // Handle Outdated error in present.
    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) {
        resize();
    } else if (res != VK_SUCCESS) {
        // Failed to present swapchain image.
        vkQueueWaitIdle(m_queue);
        return;
    }
}

VkInstance Render::create_instance() {

    VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
        "PerfViewer",
        0,
        "PerfViewer",
        0,
        VK_API_VERSION_1_3
    };

    std::vector<const char *> layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    std::vector<const char *> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo instance_create_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr,
        VkInstanceCreateFlags(),
        &app_info,
        (uint32_t) layers.size(), layers.data(),
        (uint32_t) extensions.size(), extensions.data()
    };

    VkInstance instance;
    VkResult res = vkCreateInstance(&instance_create_info, nullptr, &instance);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return instance;
}

VkPhysicalDevice Render::select_physical_device(VkInstance instance) {
    std::vector<VkPhysicalDevice> physical_devices;
    uint32_t count;
    VkResult res = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    physical_devices.resize(count);
    res = vkEnumeratePhysicalDevices(instance, &count, physical_devices.data());
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    for (VkPhysicalDevice physical_device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            continue;
        }

        std::vector<VkExtensionProperties> device_extension_properties;
        uint32_t extension_count;
        VkResult res = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
        if (res != VK_SUCCESS) {
            continue;
        }
        device_extension_properties.resize(extension_count);
        res = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, device_extension_properties.data());
        if (res != VK_SUCCESS) {
            continue;
        }

        bool found;
        for (const char * extension : DEVICE_EXTENSIONS) {
            found = false;
            for (VkExtensionProperties & extProps : device_extension_properties) {
                if (strcmp(extProps.extensionName, extension) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }
        }
        if (found) {
            return physical_device;
        }
    }

    return VK_NULL_HANDLE;
}

VkSurfaceKHR Render::create_surface(HINSTANCE hinstance, HWND hwnd) const {
    VkWin32SurfaceCreateInfoKHR surface_create_info{
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr,
        VkWin32SurfaceCreateFlagsKHR(),
        hinstance,
        hwnd
    };
    VkSurfaceKHR surface;
    VkResult res = vkCreateWin32SurfaceKHR(m_instance, &surface_create_info, nullptr, &surface);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return surface;
}

VkDeviceMemory Render::alloc(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties) {
    uint32_t memory_type_index = -1;
    for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1 << i)) == 0) {
            continue;
        }
        if ((m_memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            memory_type_index = i;
            break;
        }
    }
    if (memory_type_index == -1) {
        return VK_NULL_HANDLE;
    }

    VkMemoryAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr,
        requirements.size,
        memory_type_index
    };
    VkDeviceMemory memory;
    VkResult res = vkAllocateMemory(m_device, &alloc_info, nullptr, &memory);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return memory;
}

VkDeviceMemory Render::alloc(VkImage image, VkMemoryPropertyFlags properties) {
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(m_device, image, &requirements);

    VkDeviceMemory memory = alloc(requirements, properties);

    VkResult res = vkBindImageMemory(m_device, image, memory, 0);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return memory;
}

VkDeviceMemory Render::alloc(VkBuffer buffer, VkMemoryPropertyFlags properties) {
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

    VkDeviceMemory memory = alloc(requirements, properties);

    VkResult res = vkBindBufferMemory(m_device, buffer, memory, 0);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return memory;
}

VkDevice Render::create_device(VkPhysicalDevice physical_device, uint32_t queue_family_index) {
    float const priorities[1] = {1.0};
    VkDeviceQueueCreateInfo queue_create_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr,
        VkDeviceQueueCreateFlags{},
        queue_family_index,
        1, // queueCount
        priorities
    };

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        nullptr,
        VK_TRUE
    };

    VkDeviceCreateInfo device_create_info{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &dynamic_rendering_feature,
        VkDeviceCreateFlags{},
        1, &queue_create_info,
        0, nullptr, // ppEnabledLayerNames
        static_cast<uint32_t>(sizeof(DEVICE_EXTENSIONS)/sizeof(DEVICE_EXTENSIONS[0])), DEVICE_EXTENSIONS,
        nullptr // pEnabledFeatures
    };

    VkDevice device;
    VkResult res = vkCreateDevice(physical_device, &device_create_info, nullptr, &device);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return device;
}

bool Render::create_swapchain(VkSwapchainKHR old_swapchain, VkSurfaceCapabilitiesKHR & surface_capabilities) {
    for (VkImageView image_view : m_image_views) {
        vkDestroyImageView(m_device, image_view, nullptr);
    }
    m_image_views.clear();

    if (IMAGE_COUNT < surface_capabilities.minImageCount) {
        return false;
    }
    if (surface_capabilities.maxImageCount != 0 && IMAGE_COUNT > surface_capabilities.maxImageCount) {
        return false;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr,
        VkSwapchainCreateFlagsKHR(),                // Flags
        m_surface,                                  // Surface
        IMAGE_COUNT,                                // Minimum image count
        VK_FORMAT_B8G8R8A8_UNORM,                   // Image format
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,          // Image color space
        m_extent,                                   // Image extent
        1,                                          // Image array layers
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,        // Image usage
        VK_SHARING_MODE_EXCLUSIVE,                  // Image sharing mode
        0,                                          // Queue family index count
        nullptr,                                    // Queue family indices
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,      // Pre transform
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,          // Composite alpha
        VK_PRESENT_MODE_MAILBOX_KHR,                // Present mode
        VK_TRUE,                                    // Clipped
        old_swapchain};                              // Old swapchain

    VkResult res = vkCreateSwapchainKHR(m_device, &swapchain_create_info, nullptr, &m_swapchain);
    if (old_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, old_swapchain, nullptr);
    }
    if (res != VK_SUCCESS) {
        return false;
    }

    uint32_t count;
    res = vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
    if (res != VK_SUCCESS) {
        return false;
    }

    // Get swapchain images

    m_images.resize(count);
    res = vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_images.data());
    if (res != VK_SUCCESS) {
        return false;
    }

    // Create image views

    m_image_views.reserve(count);
    VkImageSubresourceRange subresource_range_info{
        VK_IMAGE_ASPECT_COLOR_BIT,  // aspectMask
        0,                          // baseMipLevel
        1,                          // levelCount
        0,                          // baseArrayLayer
        1                           // layerCount
    };
    VkImageViewCreateInfo image_view_create_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr,
        VkImageViewCreateFlags(),
        VkImage(),                // image
        VK_IMAGE_VIEW_TYPE_2D,    // viewType
        COLOR_FORMAT,             // format
        VkComponentMapping(),     // components
        subresource_range_info    // subresourceRange
    };
    for (const VkImage image : m_images) {
        image_view_create_info.image = image;
        VkImageView image_view;
        res = vkCreateImageView(m_device, &image_view_create_info, nullptr, &image_view);
        if (res != VK_SUCCESS) {
            false;
        }
        m_image_views.push_back(image_view);
    }
    return true;
}

bool Render::create_command_buffers() {
    VkCommandBufferAllocateInfo allocate_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
        m_command_pool,                   // commandPool
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,  // level
        1                                 // commandBufferCount
    };                               

    m_command_buffers.resize(m_image_views.size());
    for (int i = 0; i < m_image_views.size(); ++i) {
        VkResult res = vkAllocateCommandBuffers(m_device, &allocate_info, &m_command_buffers[i]);
        if (res != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool Render::render(uint32_t swapchain_index) {

    VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // flags
        nullptr                                      // pInheritanceInfo
    };
    VkResult res = vkBeginCommandBuffer(m_command_buffers[swapchain_index], &begin_info);
    if (res != VK_SUCCESS) {
        return false;
    }

    // transition image to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    {
        const VkImageMemoryBarrier image_memory_barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_NONE,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            0, 0,
            m_images[swapchain_index],
            VkImageSubresourceRange{
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            }
        };
        vkCmdPipelineBarrier(
            m_command_buffers[swapchain_index],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,              // srcStageMask
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // dstStageMask
            0,                                              // dependencyFlags
            0, nullptr,                                     // memory barriers
            0, nullptr,                                     // buffer memory barriers
            1, &image_memory_barrier                        // image memory barriers
        );
    }

    VkRenderingAttachmentInfoKHR color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    color_attachment.imageView = m_image_views[swapchain_index];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = { 1.0f,1.0f,1.0f,1.0f };

    VkRenderingInfoKHR rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    rendering_info.renderArea = { 0, 0, m_extent.width, m_extent.height };
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = VK_NULL_HANDLE;
    rendering_info.pStencilAttachment = VK_NULL_HANDLE;

    vkCmdBeginRenderingKHR(m_command_buffers[swapchain_index], &rendering_info);

    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f };
    vkCmdSetViewport(m_command_buffers[swapchain_index], 0, 1, &vp);

    VkRect2D scissor{ { 0, 0 }, { m_extent.width, m_extent.height } };
    vkCmdSetScissor(m_command_buffers[swapchain_index], 0, 1, &scissor);

    vkCmdBindDescriptorSets(m_command_buffers[swapchain_index], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &m_descriptor_set, 0, nullptr);

    if (!update_uniform_buffer()) {
        return false;
    }

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(m_command_buffers[swapchain_index], 0, 1, &m_vertex_buffer, offsets);

    // lines
    vkCmdBindPipeline(m_command_buffers[swapchain_index], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline[1]);
    vkCmdBindIndexBuffer(m_command_buffers[swapchain_index], m_vertex_buffer, m_vertex_buffer_index_offset_line, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(m_command_buffers[swapchain_index], m_index_count_line, 1, 0, 0, 0);

    // triangles
    vkCmdBindPipeline(m_command_buffers[swapchain_index], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline[0]);
    vkCmdBindIndexBuffer(m_command_buffers[swapchain_index], m_vertex_buffer, m_vertex_buffer_index_offset_tri, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(m_command_buffers[swapchain_index], m_index_count_tri, 1, 0, 0, 0);

    vkCmdEndRenderingKHR(m_command_buffers[swapchain_index]);

    // transition image to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    {
        const VkImageMemoryBarrier image_memory_barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_NONE,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            0, 0,
            m_images[swapchain_index],
            VkImageSubresourceRange{
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            }
        };
        vkCmdPipelineBarrier(
            m_command_buffers[swapchain_index],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,           // dstStageMask
            0,                                              // dependencyFlags
            0, nullptr,                                     // memory barriers
            0, nullptr,                                     // buffer memory barriers
            1, &image_memory_barrier                        // image memory barriers
        );
    }

    res = vkEndCommandBuffer(m_command_buffers[swapchain_index]);
    if (res != VK_SUCCESS) {
        return false;
    }

    // Submit command buffer to graphics queue
    VkPipelineStageFlags wait_stage{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr,
        1, &m_swapchain_acquire_semaphore[swapchain_index],
        &wait_stage,
        1, &m_command_buffers[swapchain_index],
        1, &m_swapchain_release_semaphore[swapchain_index]
    };
    res = vkQueueSubmit(m_queue,
        1, &submit_info,
        m_queue_submit_fence[swapchain_index]);
    if (res != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool Render::setup_descriptors() {
    VkDescriptorSetLayoutBinding set_layout_binding{
        0,                                  // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptorType
        1,                                  // descriptorCount
        VK_SHADER_STAGE_VERTEX_BIT,         // stageFlags
        nullptr                             // pImmutableSamplers
    };
    VkDescriptorSetLayoutCreateInfo set_layout_create_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr,
        VkDescriptorSetLayoutCreateFlags{},
        1, &set_layout_binding              // bindings
    };
    VkResult res = vkCreateDescriptorSetLayout(m_device, &set_layout_create_info, nullptr, &m_descriptor_set_layout);
    if (res != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize descriptor_pool_size{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1                           // descriptorCount
    };
    VkDescriptorPoolCreateInfo descriptor_pool_create_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
        VkDescriptorPoolCreateFlags{},
        1,                          // maxSets
        1, &descriptor_pool_size    // pool sizes
    };
    res = vkCreateDescriptorPool(m_device, &descriptor_pool_create_info, nullptr, &m_descriptor_pool);
    if (res != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo descriptor_set_alloc_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        m_descriptor_pool,              // descriptorPool
        1, &m_descriptor_set_layout     // descriptor set layouts
    };
    res = vkAllocateDescriptorSets(m_device, &descriptor_set_alloc_info, &m_descriptor_set);
    if (res != VK_SUCCESS) {
        return false;
    }

    // create uniform buffer
    VkDeviceSize uniform_buffer_size = 0x80; //  4*4*sizeof(float)+sizeof(uint32_t);
    VkBufferCreateInfo uniform_buffer_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
        VkBufferCreateFlags(),
        uniform_buffer_size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr
    };
    res = vkCreateBuffer(m_device, &uniform_buffer_create_info, nullptr, &m_uniform_buffer);
    if (res != VK_SUCCESS) {
        return false;
    }
    m_uniform_buffer_memory = alloc(m_uniform_buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (m_uniform_buffer_memory == VK_NULL_HANDLE) {
        return false;
    }
    res = vkMapMemory(m_device, m_uniform_buffer_memory, 0, uniform_buffer_size, 0, &m_uniform_memory_data);
    if (res != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo uniform_buffer_info{
        m_uniform_buffer,   // buffer
        0,                  // offset
        uniform_buffer_size // range
    };
    VkWriteDescriptorSet write_descriptor_set{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
        m_descriptor_set,                   // dstSet
        0,                                  // dstBinding
        0,                                  // dstArrayElement
        1,                                  // descriptorCount
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptorType
        nullptr,                            // pImageInfo
        &uniform_buffer_info,               // pBufferInfo
        nullptr,                            // pTexelBufferView
    };
    vkUpdateDescriptorSets(m_device, 1, &write_descriptor_set, 0, nullptr);

    if (!update_uniform_buffer()) {
        return false;
    }

    return true;
}

bool Render::create_pipeline() {

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr,
        VkPipelineLayoutCreateFlags{},
        1, &m_descriptor_set_layout,    // set layouts
        0, nullptr                      // push constants
    };
    VkResult res = vkCreatePipelineLayout(m_device, &pipeline_layout_create_info, nullptr, &m_pipeline_layout);
    if (res != VK_SUCCESS) {
        return false;
    }

    VkVertexInputBindingDescription binding_description{
        0,
        sizeof(vertex_t),
        VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription input_attribute_descriptions[2] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(vertex_t, pos) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex_t, color) }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_create_info{ 
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr,
        VkPipelineVertexInputStateCreateFlags(),
        1, &binding_description,                    // vertex binding descriptions
        2, input_attribute_descriptions             // vertex attribute descriptions
    };

    // Specify we will use triangle lists to draw geometry.
    VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr,
        VkPipelineInputAssemblyStateCreateFlags{},
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,            // topology
        VK_FALSE                                    // primitiveRestartEnable
    };

    // Specify rasterization state.
    VkPipelineRasterizationStateCreateInfo raster_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr,
        VkPipelineRasterizationStateCreateFlags{},
        VK_FALSE,                           // depthClampEnable
        VK_FALSE,                           // rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL,               // polygonMode
        VK_CULL_MODE_NONE,                  // cullMode
        VK_FRONT_FACE_COUNTER_CLOCKWISE,    // frontFace
        VK_FALSE,                           // depthBiasEnable
        0.0f,                               // depthBiasConstantFactor
        0.0f,                               // depthBiasClamp
        0.0f,                               // depthBiasSlopeFactor
        1.0f                                // lineWidth
    };

    // We will have one viewport and scissor box.
    VkPipelineViewportStateCreateInfo viewport_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr,
        VkPipelineViewportStateCreateFlags{},
        1, nullptr,                 // viewports
        1, nullptr                  // scissors
    };

    // Specify that these states will be dynamic, i.e. not part of pipeline state object.
    VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_create_info{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr,
        VkPipelineDynamicStateCreateFlags{},
        sizeof(dynamics)/sizeof(dynamics[0]), dynamics
    };

    // Load our SPIR-V shaders.
    VkShaderModule shader_module_vert = load_shader_module(m_device, "triangle.vert");
    if (shader_module_vert == VK_NULL_HANDLE) {
        return false;
    }
    VkShaderModule shader_module_frag = load_shader_module(m_device, "triangle.frag");
    if (shader_module_frag == VK_NULL_HANDLE) {
        return false;
    }

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VkPipelineShaderStageCreateFlags{},
            VK_SHADER_STAGE_VERTEX_BIT,     // stage
            shader_module_vert,             // module
            "main",                         // pName
            nullptr                         // pSpecializationInfo
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VkPipelineShaderStageCreateFlags{},
            VK_SHADER_STAGE_FRAGMENT_BIT,   // stage
            shader_module_frag,             // module
            "main",                         // pName
            nullptr                         // pSpecializationInfo
        }
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info_filled{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr,
        VkPipelineInputAssemblyStateCreateFlags{},
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,        // topology
        VK_FALSE                                    // primitiveRestartEnable
    };

    VkGraphicsPipelineCreateInfo pipe_create_info[2] = {
    {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr,
        VkPipelineCreateFlags(),
        sizeof(shader_stages)/sizeof(shader_stages[0]),
        shader_stages,                  // pStages
        &vertex_input_create_info,      // pVertexInputState
        &input_assembly_create_info_filled,    // pInputAssemblyState
        nullptr,                        // pTessellationState
        &viewport_create_info,          // pViewportState
        &raster_create_info,            // pRasterizationState
        nullptr,                        // pMultisampleState
        nullptr,                        // pDepthStencilState
        nullptr,                        // pColorBlendState
        &dynamic_create_info,           // pDynamicState
        m_pipeline_layout,              // layout
        nullptr,                        // renderPass
        0,                              // subpass
        VkPipeline(),                   // basePipelineHandle
        0,                              // basePipelineIndex
    },
    {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr,
        VkPipelineCreateFlags(),
        sizeof(shader_stages)/sizeof(shader_stages[0]),
        shader_stages,                  // pStages
        &vertex_input_create_info,      // pVertexInputState
        &input_assembly_create_info,    // pInputAssemblyState
        nullptr,                        // pTessellationState
        &viewport_create_info,          // pViewportState
        &raster_create_info,            // pRasterizationState
        nullptr,                        // pMultisampleState
        nullptr,                        // pDepthStencilState
        nullptr,                        // pColorBlendState
        &dynamic_create_info,           // pDynamicState
        m_pipeline_layout,              // layout
        nullptr,                        // renderPass
        0,                              // subpass
        VkPipeline(),                   // basePipelineHandle
        0,                              // basePipelineIndex
    } };

    res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 2, pipe_create_info, nullptr, m_pipeline);
    if (res != VK_SUCCESS) {
        return false;
    }


    // Pipeline is baked, we can delete the shader modules now.
    vkDestroyShaderModule(m_device, shader_stages[0].module, nullptr);
    vkDestroyShaderModule(m_device, shader_stages[1].module, nullptr);

    return true;
}

bool Render::update_uniform_buffer() {
    const float mat[] = {
        m_sx, 0.0f, 0.0f, 0.0f,
        0.0f, m_sy, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        m_x,  m_y,  0.0f, 1.0f
    };
    memcpy(m_uniform_memory_data, mat, sizeof(mat));
    memcpy((char*)m_uniform_memory_data+sizeof(mat), &m_selected_index, sizeof(uint32_t));

    VkMappedMemoryRange mapped_memory_range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr,
        m_uniform_buffer_memory, 0, VK_WHOLE_SIZE
    };
    VkResult res = vkFlushMappedMemoryRanges(m_device, 1, &mapped_memory_range);
    if (res != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool Render::setup_vertex_buffer(const std::vector<vertex_t> & vertices, const std::vector<uint32_t> & line_indices, const std::vector<uint32_t> & triangle_indices) {

    VkDeviceSize vertex_size = vertices.size() * sizeof(vertices[0]);
    VkDeviceSize line_index_size = line_indices.size() * sizeof(uint32_t);
    VkDeviceSize tri_index_size = triangle_indices.size() * sizeof(uint32_t);
    VkDeviceSize vertex_buffer_size = vertex_size + line_index_size + tri_index_size;

    m_index_count_line = (uint32_t) line_indices.size();
    m_index_count_tri = (uint32_t) triangle_indices.size();
    m_vertex_buffer_index_offset_line = vertex_size;
    m_vertex_buffer_index_offset_tri = m_vertex_buffer_index_offset_line + line_index_size;

    // create staging buffer
    VkBufferCreateInfo staging_buffer_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
        VkBufferCreateFlags(),
        vertex_buffer_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr
    };
    VkBuffer vertex_buffer_staging;
    VkResult res = vkCreateBuffer(m_device, &staging_buffer_create_info, nullptr, &vertex_buffer_staging);
    if (res != VK_SUCCESS) {
        return false;
    }
    VkDeviceMemory vertex_buffer_memory_staging = alloc(vertex_buffer_staging, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (vertex_buffer_memory_staging == VK_NULL_HANDLE) {
        return false;
    }

    // create vertex buffer
    VkBufferCreateInfo buffer_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
        VkBufferCreateFlags(),
        vertex_buffer_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr
    };
    res = vkCreateBuffer(m_device, &buffer_create_info, nullptr, &m_vertex_buffer);
    if (res != VK_SUCCESS) {
        return false;
    }
    m_vertex_buffer_memory = alloc(m_vertex_buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (m_vertex_buffer_memory == VK_NULL_HANDLE) {
        return false;
    }

    // stage vertex data
    void * data;
    res = vkMapMemory(m_device, vertex_buffer_memory_staging, 0, vertex_buffer_size, 0, &data);
    if (res != VK_SUCCESS) {
        return false;
    }
    memcpy(data, vertices.data(), (size_t) vertex_size);
    memcpy(((char *) data) + m_vertex_buffer_index_offset_line, line_indices.data(), (size_t) line_index_size);
    memcpy(((char *) data) + m_vertex_buffer_index_offset_tri, triangle_indices.data(), (size_t) tri_index_size);

    VkMappedMemoryRange mapped_memory_range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr,
        vertex_buffer_memory_staging, 0, vertex_buffer_size
    };
    res = vkFlushMappedMemoryRanges(m_device, 1, &mapped_memory_range);
    if (res != VK_SUCCESS) {
        return false;
    }
    vkUnmapMemory(m_device, vertex_buffer_memory_staging);

    // copy staging buffer to device memory
    {
        VkCommandBufferAllocateInfo alloc_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
            m_command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1
        };
        VkCommandBuffer command_buffer;
        res = vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer);
        if (res != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferBeginInfo begin_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr
        };
        res = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (res != VK_SUCCESS) {
            return false;
        }

        VkBufferCopy buffer_copy{
            0, 0, vertex_buffer_size
        };
        vkCmdCopyBuffer(command_buffer, vertex_buffer_staging, m_vertex_buffer, 1, &buffer_copy);

        res = vkEndCommandBuffer(command_buffer);
        if (res != VK_SUCCESS) {
            return false;
        }

        VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr,
            0, nullptr,
            nullptr,
            1, &command_buffer,
            0, nullptr
        };
        res = vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
        if (res != VK_SUCCESS) {
            return false;
        }
        res = vkQueueWaitIdle(m_queue);
        if (res != VK_SUCCESS) {
            return false;
        }

        vkFreeCommandBuffers(m_device, m_command_pool, 1, &command_buffer);
    }

    vkDestroyBuffer(m_device, vertex_buffer_staging, nullptr);
    vkFreeMemory(m_device, vertex_buffer_memory_staging, nullptr);
    return true;
}

bool Render::init(HINSTANCE hinstance, HWND hwnd, const std::vector<vertex_t> & vertices, const std::vector<uint32_t> & line_indices, const std::vector<uint32_t> & triangle_indices) {
    // instance

    m_instance = create_instance();
    if (m_instance == VK_NULL_HANDLE) {
        return false;
    }

    // physical device

    m_physical_device = select_physical_device(m_instance);
    if (m_physical_device == VK_NULL_HANDLE) {
        return false;
    }

    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_memory_properties);

    // surface

    m_surface = create_surface(hinstance, hwnd);
    if (m_surface == VK_NULL_HANDLE) {
        return false;
    }

    m_queue_family_index = get_queue_family_index(m_physical_device, m_surface);
    if (m_queue_family_index == -1) {
        return false;
    }

    // device

    m_device = create_device(m_physical_device, m_queue_family_index);
    if (m_device == VK_NULL_HANDLE) {
        return false;
    }

    vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderingKHR"));
    vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(m_device, "vkCmdEndRenderingKHR"));

    const int queueIndex = 0;
    vkGetDeviceQueue(m_device, m_queue_family_index, queueIndex, &m_queue);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);
    if (res != VK_SUCCESS) {
        return false;
    }

    m_extent = surface_capabilities.currentExtent;

    // swapchain

    create_swapchain(VkSwapchainKHR(), surface_capabilities);
    if (m_swapchain == VK_NULL_HANDLE) {
        return false;
    }

    // command pool

    VkCommandPoolCreateInfo command_pool_create_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        m_queue_family_index};

    res = vkCreateCommandPool(m_device, &command_pool_create_info, nullptr, &m_command_pool);
    if (res != VK_SUCCESS) {
        return false;
    }

    // descriptors

    if (!setup_descriptors()) {
        return false;
    }

    // pipeline

    if (!create_pipeline()) {
        return false;
    }

    // command buffers

    if (!create_command_buffers()) {
        return false;
    }

    // per-frame sync stuff

    const size_t num_frames = m_image_views.size();
    m_queue_submit_fence.reserve(num_frames);
    m_swapchain_acquire_semaphore.reserve(num_frames);
    m_swapchain_release_semaphore.reserve(num_frames);

    for (size_t i = 0; i < num_frames; ++i) {
        m_queue_submit_fence.push_back(create_fence(m_device, VK_FENCE_CREATE_SIGNALED_BIT));
        m_swapchain_acquire_semaphore.push_back(create_semaphore(m_device, VkSemaphoreCreateFlags()));
        m_swapchain_release_semaphore.push_back(create_semaphore(m_device, VkSemaphoreCreateFlags()));
    }

    // vertex buffer

    setup_vertex_buffer(vertices, line_indices, triangle_indices);

    m_init = true;
    return true;
}
