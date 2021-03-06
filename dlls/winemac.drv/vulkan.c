/* Mac Driver Vulkan implementation
 *
 * Copyright 2017 Roderick Colenbrander
 * Copyright 2018 Andrew Eikum for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"
#include "macdrv.h"

#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"

#include "wine/debug.h"
#include "wine/heap.h"
#include "wine/library.h"
#include "macdrv.h"

/* We only want host compatible structures and don't need alignment. */
#define WINE_VK_ALIGN(x)

#include "wine/vulkan.h"
#include "wine/vulkan_driver.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);

#ifdef SONAME_LIBMOLTENVK

typedef VkFlags VkMacOSSurfaceCreateFlagsMVK;
#define VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK 1000123000

struct wine_vk_surface
{
    macdrv_metal_device device;
    macdrv_metal_view view;
    VkSurfaceKHR surface; /* native surface */
};

typedef struct VkMacOSSurfaceCreateInfoMVK
{
    VkStructureType sType;
    const void *pNext;
    VkMacOSSurfaceCreateFlagsMVK flags;
    const void *pView; /* NSView */
} VkMacOSSurfaceCreateInfoMVK;

static VkResult (*pvkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
static VkResult (*pvkCreateInstance)(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
static VkResult (*pvkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
static VkResult (*pvkCreateMacOSSurfaceMVK)(VkInstance, const VkMacOSSurfaceCreateInfoMVK*, const VkAllocationCallbacks *, VkSurfaceKHR *);
static void (*pvkDestroyInstance)(VkInstance, const VkAllocationCallbacks *);
static void (*pvkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *);
static void (*pvkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
static VkResult (*pvkEnumerateInstanceExtensionProperties)(const char *, uint32_t *, VkExtensionProperties *);
static void * (*pvkGetDeviceProcAddr)(VkDevice, const char *);
static void * (*pvkGetInstanceProcAddr)(VkInstance, const char *);
static VkResult (*pvkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *);
static VkResult (*pvkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *);
static VkResult (*pvkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkPresentModeKHR *);
static VkResult (*pvkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *);
static VkResult (*pvkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
static VkResult (*pvkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR *);

static void *macdrv_get_vk_device_proc_addr(const char *name);
static void *macdrv_get_vk_instance_proc_addr(VkInstance instance, const char *name);

static inline struct wine_vk_surface *surface_from_handle(VkSurfaceKHR handle)
{
    return (struct wine_vk_surface *)(uintptr_t)handle;
}

static BOOL wine_vk_init(void)
{
    static BOOL init_done = FALSE;
    static void *vulkan_handle;

    if (init_done) return (vulkan_handle != NULL);
    init_done = TRUE;

    if (!(vulkan_handle = wine_dlopen(SONAME_LIBMOLTENVK, RTLD_NOW, NULL, 0)))
    {
        ERR("Failed to load %s\n", SONAME_LIBMOLTENVK);
        return FALSE;
    }

#define LOAD_FUNCPTR(f) if ((p##f = wine_dlsym(vulkan_handle, #f, NULL, 0)) == NULL) return FALSE;
    LOAD_FUNCPTR(vkAcquireNextImageKHR)
    LOAD_FUNCPTR(vkCreateInstance)
    LOAD_FUNCPTR(vkCreateSwapchainKHR)
    LOAD_FUNCPTR(vkCreateMacOSSurfaceMVK)
    LOAD_FUNCPTR(vkDestroyInstance)
    LOAD_FUNCPTR(vkDestroySurfaceKHR)
    LOAD_FUNCPTR(vkDestroySwapchainKHR)
    LOAD_FUNCPTR(vkEnumerateInstanceExtensionProperties)
    LOAD_FUNCPTR(vkGetDeviceProcAddr)
    LOAD_FUNCPTR(vkGetInstanceProcAddr)
    LOAD_FUNCPTR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    LOAD_FUNCPTR(vkGetPhysicalDeviceSurfaceFormatsKHR)
    LOAD_FUNCPTR(vkGetPhysicalDeviceSurfacePresentModesKHR)
    LOAD_FUNCPTR(vkGetPhysicalDeviceSurfaceSupportKHR)
    LOAD_FUNCPTR(vkGetSwapchainImagesKHR)
    LOAD_FUNCPTR(vkQueuePresentKHR)
#undef LOAD_FUNCPTR

    return TRUE;
}

/* Helper function for converting between win32 and MoltenVK compatible VkInstanceCreateInfo.
 * Caller is responsible for allocation and cleanup of 'dst'.
 */
static VkResult wine_vk_instance_convert_create_info(const VkInstanceCreateInfo *src,
        VkInstanceCreateInfo *dst)
{
    unsigned int i;
    const char **enabled_extensions = NULL;

    dst->sType = src->sType;
    dst->flags = src->flags;
    dst->pApplicationInfo = src->pApplicationInfo;
    dst->pNext = src->pNext;
    dst->enabledLayerCount = 0;
    dst->ppEnabledLayerNames = NULL;
    dst->enabledExtensionCount = 0;
    dst->ppEnabledExtensionNames = NULL;

    if (src->enabledExtensionCount > 0)
    {
        enabled_extensions = heap_calloc(src->enabledExtensionCount, sizeof(*src->ppEnabledExtensionNames));
        if (!enabled_extensions)
        {
            ERR("Failed to allocate memory for enabled extensions\n");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        for (i = 0; i < src->enabledExtensionCount; i++)
        {
            /* Substitute extension with MoltenVK ones else copy. Long-term, when we
             * support more extensions, we should store these in a list.
             */
            if (!strcmp(src->ppEnabledExtensionNames[i], "VK_KHR_win32_surface"))
            {
                enabled_extensions[i] = "VK_MVK_macos_surface";
            }
            else
            {
                enabled_extensions[i] = src->ppEnabledExtensionNames[i];
            }
        }
        dst->ppEnabledExtensionNames = enabled_extensions;
        dst->enabledExtensionCount = src->enabledExtensionCount;
    }

    return VK_SUCCESS;
}

static void wine_vk_surface_destroy(VkInstance instance, struct wine_vk_surface *surface)
{
    if (!surface)
        return;

    /* vkDestroySurfaceKHR must handle VK_NULL_HANDLE (0) for surface. */
    pvkDestroySurfaceKHR(instance, surface->surface, NULL /* allocator */);

    if (surface->view)
        macdrv_view_remove_metal_view(surface->view);

    if (surface->device)
        macdrv_release_metal_device(surface->device);

    heap_free(surface);
}

static VkResult macdrv_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    TRACE("%p, 0x%s, 0x%s, 0x%s, 0x%s, %p\n", device,
            wine_dbgstr_longlong(swapchain), wine_dbgstr_longlong(timeout),
            wine_dbgstr_longlong(semaphore), wine_dbgstr_longlong(fence), index);

    return pvkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, index);
}

static VkResult macdrv_vkCreateInstance(const VkInstanceCreateInfo *create_info,
        const VkAllocationCallbacks *allocator, VkInstance *instance)
{
    VkInstanceCreateInfo create_info_host;
    VkResult res;
    TRACE("create_info %p, allocator %p, instance %p\n", create_info, allocator, instance);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    /* Perform a second pass on converting VkInstanceCreateInfo. Winevulkan
     * performed a first pass in which it handles everything except for WSI
     * functionality such as VK_KHR_win32_surface. Handle this now.
     */
    res = wine_vk_instance_convert_create_info(create_info, &create_info_host);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to convert instance create info, res=%d\n", res);
        return res;
    }

    res = pvkCreateInstance(&create_info_host, NULL /* allocator */, instance);

    heap_free((void *)create_info_host.ppEnabledExtensionNames);
    return res;
}

static VkResult macdrv_vkCreateSwapchainKHR(VkDevice device,
        const VkSwapchainCreateInfoKHR *create_info,
        const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
    VkSwapchainCreateInfoKHR create_info_host;
    TRACE("%p %p %p %p\n", device, create_info, allocator, swapchain);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    create_info_host = *create_info;
    create_info_host.surface = surface_from_handle(create_info->surface)->surface;

    return pvkCreateSwapchainKHR(device, &create_info_host, NULL /* allocator */,
            swapchain);
}

static VkResult macdrv_vkCreateWin32SurfaceKHR(VkInstance instance,
        const VkWin32SurfaceCreateInfoKHR *create_info,
        const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    VkResult res;
    VkMacOSSurfaceCreateInfoMVK create_info_host;
    struct wine_vk_surface *mac_surface;
    struct macdrv_win_data *data;

    TRACE("%p %p %p %p\n", instance, create_info, allocator, surface);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    /* TODO: support child window rendering. */
    if (GetAncestor(create_info->hwnd, GA_PARENT) != GetDesktopWindow())
    {
        FIXME("Application requires child window rendering, which is not implemented yet!\n");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    if (!(data = get_win_data(create_info->hwnd)))
    {
        FIXME("DC for window %p of other process: not implemented\n", create_info->hwnd);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    mac_surface = heap_alloc_zero(sizeof(*mac_surface));
    if (!mac_surface)
    {
        release_win_data(data);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    mac_surface->device = macdrv_create_metal_device();
    if (!mac_surface->device)
    {
        ERR("Failed to allocate Metal device for hwnd=%p\n", create_info->hwnd);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto err;
    }

    mac_surface->view = macdrv_view_get_metal_view(data->client_cocoa_view, mac_surface->device);
    if (!mac_surface->view)
    {
        ERR("Failed to allocate Metal view for hwnd=%p\n", create_info->hwnd);

        /* VK_KHR_win32_surface only allows out of host and device memory as errors. */
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto err;
    }

    create_info_host.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
    create_info_host.pNext = NULL;
    create_info_host.flags = 0; /* reserved */
    create_info_host.pView = mac_surface->view;

    res = pvkCreateMacOSSurfaceMVK(instance, &create_info_host, NULL /* allocator */, &mac_surface->surface);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to create MoltenVK surface, res=%d\n", res);
        goto err;
    }

    *surface = (uintptr_t)mac_surface;

    release_win_data(data);

    TRACE("Created surface=0x%s\n", wine_dbgstr_longlong(*surface));
    return VK_SUCCESS;

err:
    wine_vk_surface_destroy(instance, mac_surface);
    release_win_data(data);
    return res;
}

static void macdrv_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    TRACE("%p %p\n", instance, allocator);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    pvkDestroyInstance(instance, NULL /* allocator */);
}

static void macdrv_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
        const VkAllocationCallbacks *allocator)
{
    struct wine_vk_surface *mac_surface = surface_from_handle(surface);

    TRACE("%p 0x%s %p\n", instance, wine_dbgstr_longlong(surface), allocator);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    wine_vk_surface_destroy(instance, mac_surface);
}

static void macdrv_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
         const VkAllocationCallbacks *allocator)
{
    TRACE("%p, 0x%s %p\n", device, wine_dbgstr_longlong(swapchain), allocator);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    pvkDestroySwapchainKHR(device, swapchain, NULL /* allocator */);
}

static VkResult macdrv_vkEnumerateInstanceExtensionProperties(const char *layer_name,
        uint32_t *count, VkExtensionProperties* properties)
{
    unsigned int i;
    VkResult res;

    TRACE("layer_name %p, count %p, properties %p\n", debugstr_a(layer_name), count, properties);

    /* This shouldn't get called with layer_name set, the ICD loader prevents it. */
    if (layer_name)
    {
        ERR("Layer enumeration not supported from ICD.\n");
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    /* We will return the same number of instance extensions reported by the host back to
     * winevulkan. Along the way we may replace MoltenVK extensions with their win32 equivalents.
     * Winevulkan will perform more detailed filtering as it knows whether it has thunks
     * for a particular extension.
     */
    res = pvkEnumerateInstanceExtensionProperties(layer_name, count, properties);
    if (!properties || res < 0)
        return res;

    for (i = 0; i < *count; i++)
    {
        /* For now the only MoltenVK extension we need to fixup. Long-term we may need an array. */
        if (!strcmp(properties[i].extensionName, "VK_MVK_macos_surface"))
        {
            TRACE("Substituting VK_KHR_win32_surface for VK_MVK_macos_surface\n");

            snprintf(properties[i].extensionName, sizeof(properties[i].extensionName), "VK_KHR_win32_surface");
            properties[i].specVersion = 6; /* Revision as of 4/24/2017 */
        }
    }

    TRACE("Returning %u extensions.\n", *count);
    return res;
}

static void *macdrv_vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    void *proc_addr;

    TRACE("%p, %s\n", device, debugstr_a(name));

    if ((proc_addr = macdrv_get_vk_device_proc_addr(name)))
        return proc_addr;

    return pvkGetDeviceProcAddr(device, name);
}

static void *macdrv_vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    void *proc_addr;

    TRACE("%p, %s\n", instance, debugstr_a(name));

    if ((proc_addr = macdrv_get_vk_instance_proc_addr(instance, name)))
        return proc_addr;

    return pvkGetInstanceProcAddr(instance, name);
}

static VkResult macdrv_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice phys_dev,
        VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *capabilities)
{
    struct wine_vk_surface *mac_surface = surface_from_handle(surface);

    TRACE("%p, 0x%s, %p\n", phys_dev, wine_dbgstr_longlong(surface), capabilities);

    return pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev, mac_surface->surface,
            capabilities);
}

static VkResult macdrv_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice phys_dev,
        VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *formats)
{
    struct wine_vk_surface *mac_surface = surface_from_handle(surface);

    TRACE("%p, 0x%s, %p, %p\n", phys_dev, wine_dbgstr_longlong(surface), count, formats);

    return pvkGetPhysicalDeviceSurfaceFormatsKHR(phys_dev, mac_surface->surface,
            count, formats);
}

static VkResult macdrv_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice phys_dev,
        VkSurfaceKHR surface, uint32_t *count, VkPresentModeKHR *modes)
{
    struct wine_vk_surface *mac_surface = surface_from_handle(surface);

    TRACE("%p, 0x%s, %p, %p\n", phys_dev, wine_dbgstr_longlong(surface), count, modes);

    return pvkGetPhysicalDeviceSurfacePresentModesKHR(phys_dev, mac_surface->surface, count,
            modes);
}

static VkResult macdrv_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice phys_dev,
        uint32_t index, VkSurfaceKHR surface, VkBool32 *supported)
{
    struct wine_vk_surface *mac_surface = surface_from_handle(surface);

    TRACE("%p, %u, 0x%s, %p\n", phys_dev, index, wine_dbgstr_longlong(surface), supported);

    return pvkGetPhysicalDeviceSurfaceSupportKHR(phys_dev, index, mac_surface->surface,
            supported);
}

static VkBool32 macdrv_vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice phys_dev,
        uint32_t index)
{
    TRACE("%p %u\n", phys_dev, index);

    return VK_TRUE;
}

static VkResult macdrv_vkGetSwapchainImagesKHR(VkDevice device,
        VkSwapchainKHR swapchain, uint32_t *count, VkImage *images)
{
    TRACE("%p, 0x%s %p %p\n", device, wine_dbgstr_longlong(swapchain), count, images);
    return pvkGetSwapchainImagesKHR(device, swapchain, count, images);
}

static VkResult macdrv_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *present_info)
{
    TRACE("%p, %p\n", queue, present_info);
    return pvkQueuePresentKHR(queue, present_info);
}

static const struct vulkan_funcs vulkan_funcs =
{
    macdrv_vkAcquireNextImageKHR,
    macdrv_vkCreateInstance,
    macdrv_vkCreateSwapchainKHR,
    macdrv_vkCreateWin32SurfaceKHR,
    macdrv_vkDestroyInstance,
    macdrv_vkDestroySurfaceKHR,
    macdrv_vkDestroySwapchainKHR,
    macdrv_vkEnumerateInstanceExtensionProperties,
    macdrv_vkGetDeviceProcAddr,
    macdrv_vkGetInstanceProcAddr,
    macdrv_vkGetPhysicalDeviceSurfaceCapabilitiesKHR,
    macdrv_vkGetPhysicalDeviceSurfaceFormatsKHR,
    macdrv_vkGetPhysicalDeviceSurfacePresentModesKHR,
    macdrv_vkGetPhysicalDeviceSurfaceSupportKHR,
    macdrv_vkGetPhysicalDeviceWin32PresentationSupportKHR,
    macdrv_vkGetSwapchainImagesKHR,
    macdrv_vkQueuePresentKHR,
};

static void *get_vulkan_driver_device_proc_addr(const struct vulkan_funcs *vulkan_funcs,
        const char *name)
{
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;

    if (!strcmp(name, "AcquireNextImageKHR"))
        return vulkan_funcs->p_vkAcquireNextImageKHR;
    if (!strcmp(name, "CreateSwapchainKHR"))
        return vulkan_funcs->p_vkCreateSwapchainKHR;
    if (!strcmp(name, "DestroySwapchainKHR"))
        return vulkan_funcs->p_vkDestroySwapchainKHR;
    if (!strcmp(name, "GetDeviceProcAddr"))
        return vulkan_funcs->p_vkGetDeviceProcAddr;
    if (!strcmp(name, "GetSwapchainImagesKHR"))
        return vulkan_funcs->p_vkGetSwapchainImagesKHR;
    if (!strcmp(name, "QueuePresentKHR"))
        return vulkan_funcs->p_vkQueuePresentKHR;

    return NULL;
}

static void *macdrv_get_vk_device_proc_addr(const char *name)
{
    return get_vulkan_driver_device_proc_addr(&vulkan_funcs, name);
}

static void *get_vulkan_driver_instance_proc_addr(const struct vulkan_funcs *vulkan_funcs,
        VkInstance instance, const char *name)
{
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;

    if (!strcmp(name, "CreateInstance"))
        return vulkan_funcs->p_vkCreateInstance;
    if (!strcmp(name, "EnumerateInstanceExtensionProperties"))
        return vulkan_funcs->p_vkEnumerateInstanceExtensionProperties;

    if (!instance)
        return NULL;

    if (!strcmp(name, "CreateWin32SurfaceKHR"))
        return vulkan_funcs->p_vkCreateWin32SurfaceKHR;
    if (!strcmp(name, "DestroyInstance"))
        return vulkan_funcs->p_vkDestroyInstance;
    if (!strcmp(name, "DestroySurfaceKHR"))
        return vulkan_funcs->p_vkDestroySurfaceKHR;
    if (!strcmp(name, "GetInstanceProcAddr"))
        return vulkan_funcs->p_vkGetInstanceProcAddr;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceFormatsKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceSurfaceFormatsKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfacePresentModesKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceSurfacePresentModesKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceSupportKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceSurfaceSupportKHR;
    if (!strcmp(name, "GetPhysicalDeviceWin32PresentationSupportKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceWin32PresentationSupportKHR;

    name -= 2;

    return get_vulkan_driver_device_proc_addr(vulkan_funcs, name);
}

static void *macdrv_get_vk_instance_proc_addr(VkInstance instance, const char *name)
{
    return get_vulkan_driver_instance_proc_addr(&vulkan_funcs, instance, name);
}

const struct vulkan_funcs *get_vulkan_driver(UINT version)
{
    if (version != WINE_VULKAN_DRIVER_VERSION)
    {
        ERR("version mismatch, vulkan wants %u but driver has %u\n", version, WINE_VULKAN_DRIVER_VERSION);
        return NULL;
    }

    if (wine_vk_init())
        return &vulkan_funcs;

    return NULL;
}

#else /* No vulkan */

const struct vulkan_funcs *get_vulkan_driver(UINT version)
{
    ERR("Wine was built without Vulkan support.\n");
    return NULL;
}

#endif /* SONAME_LIBMOLTENVK */

const struct vulkan_funcs *macdrv_wine_get_vulkan_driver(PHYSDEV dev, UINT version)
{
    const struct vulkan_funcs *ret;

    if (!(ret = get_vulkan_driver( version )))
    {
        dev = GET_NEXT_PHYSDEV( dev, wine_get_vulkan_driver );
        ret = dev->funcs->wine_get_vulkan_driver( dev, version );
    }
    return ret;
}
