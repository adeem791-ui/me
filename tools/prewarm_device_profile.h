// Vulkan device setup for kyty_compute_prewarm that matches the emulator's.
//
// Drivers may key cached pipelines on device state such as robustness. If the prewarmer
// compiles with different features than kyty_emulator, the cache file still loads but the
// emulator's pipelines can miss. This mirrors the compute-relevant parts of
// VulkanCreateDevice() in upstream src/graphics/presentation/window/vulkanWindow.cpp (7b7f9a7).
// Graphics-only state (swapchain, depth clip, color write, mesh, barycentrics, feedback
// loops, provoking vertex) is not needed for compute pipelines and is left out.
//
// Plain C++ and vulkan.h only, so it can be tested without a GPU or Windows.
#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace KytyPrewarm {

// What the driver reports. Fill with QueryChain() + vkGetPhysicalDeviceFeatures2 /
// vkGetPhysicalDeviceProperties2, or by hand in tests.
struct SupportedDevice {
    VkPhysicalDeviceFeatures2 f2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features f11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features f12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features f13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup_layout {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT image_atomic_int64 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT};
    VkPhysicalDeviceMaintenance8FeaturesKHR maintenance8 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR};
    VkPhysicalDeviceProperties2 p2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceVulkan12Properties p12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceVulkan13Properties p13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
    std::vector<std::string> extensions;

    SupportedDevice() = default;
    SupportedDevice(const SupportedDevice&) = delete; // holds pNext pointers into itself
    SupportedDevice& operator=(const SupportedDevice&) = delete;

    bool HasExtension(const char* name) const {
        return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
    }

    // Links every struct so one vkGetPhysicalDeviceFeatures2 / Properties2 call fills them.
    // Extension structs are only chained when the extension exists, as the spec requires.
    void QueryChain() {
        f2.pNext = &f11;
        f11.pNext = &f12;
        f12.pNext = &f13;
        void** tail = &f13.pNext;
        auto append = [&](bool present, auto& feature) {
            if (!present) return;
            *tail = &feature;
            tail = &feature.pNext;
        };
        append(HasExtension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME), robustness2);
        append(HasExtension(VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME), workgroup_layout);
        append(HasExtension(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME), image_atomic_int64);
        append(HasExtension(VK_KHR_MAINTENANCE_8_EXTENSION_NAME), maintenance8);
        *tail = nullptr;
        p2.pNext = &p12;
        p12.pNext = &p13;
        p13.pNext = nullptr;
    }
};

// What to pass to vkCreateDevice.
struct DeviceProfile {
    VkPhysicalDeviceFeatures2 f2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features f11 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features f12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features f13 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup_layout {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT image_atomic_int64 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT};
    VkPhysicalDeviceMaintenance8FeaturesKHR maintenance8 {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR};
    std::vector<const char*> extensions;
    bool push_descriptor       = false;
    bool subgroup_size_control = false; // same rule as compute_subgroup_size_control_enabled

    DeviceProfile() = default;
    DeviceProfile(const DeviceProfile&) = delete;
    DeviceProfile& operator=(const DeviceProfile&) = delete;

    bool Enabled(const char* name) const {
        return std::any_of(extensions.begin(), extensions.end(),
                           [&](const char* e) { return std::strcmp(e, name) == 0; });
    }

    // Links the enabled structs; pass the result as VkDeviceCreateInfo::pNext.
    void* CreateChain() {
        f2.pNext = &f11;
        f11.pNext = &f12;
        f12.pNext = &f13;
        void** tail = &f13.pNext;
        auto append = [&](const char* ext, auto& feature) {
            if (!Enabled(ext)) return;
            *tail = &feature;
            tail = &feature.pNext;
        };
        append(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME, robustness2);
        append(VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME, workgroup_layout);
        append(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME, image_atomic_int64);
        append(VK_KHR_MAINTENANCE_8_EXTENSION_NAME, maintenance8);
        *tail = nullptr;
        return &f2;
    }
};

// Builds the emulator's compute-relevant device setup, never enabling anything the device
// lacks. Line references are to upstream vulkanWindow.cpp at 7b7f9a7.
inline void BuildDeviceProfile(const SupportedDevice& s, DeviceProfile& out) {
    auto both = [](VkBool32 want, VkBool32 have) -> VkBool32 {
        return (want && have) ? VK_TRUE : VK_FALSE;
    };
    const VkPhysicalDeviceFeatures& have = s.f2.features;
    VkPhysicalDeviceFeatures& f = out.f2.features;
    f = {};
    // Core features (~L591-616).
    f.fragmentStoresAndAtomics = both(1, have.fragmentStoresAndAtomics);
    f.samplerAnisotropy = both(1, have.samplerAnisotropy);
    f.robustBufferAccess = both(1, have.robustBufferAccess);
    f.depthBounds = both(1, have.depthBounds);
    f.depthClamp = both(1, have.depthClamp);
    f.shaderStorageImageWriteWithoutFormat = both(1, have.shaderStorageImageWriteWithoutFormat);
    f.shaderImageGatherExtended = both(1, have.shaderImageGatherExtended);
    f.independentBlend = both(1, have.independentBlend);
    f.dualSrcBlend = both(1, have.dualSrcBlend);
    f.tessellationShader = both(1, have.tessellationShader);
    f.sampleRateShading = both(1, have.sampleRateShading);
    f.depthBiasClamp = both(1, have.depthBiasClamp);
    f.shaderClipDistance = both(1, have.shaderClipDistance);
    f.shaderCullDistance = both(1, have.shaderCullDistance);
    f.largePoints = both(1, have.largePoints);
    f.multiViewport = both(1, have.multiViewport);
    f.fillModeNonSolid = both(1, have.fillModeNonSolid);
    f.vertexPipelineStoresAndAtomics = both(1, have.vertexPipelineStoresAndAtomics);
    f.shaderInt64 = both(1, have.shaderInt64);
    f.shaderFloat64 = both(have.shaderFloat64, s.p12.shaderSignedZeroInfNanPreserveFloat64);

    // RequiredVulkan11/12/13Features() (~L50-76) plus the optional bits set at ~L521, ~L640.
    out.f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    out.f11.storageBuffer16BitAccess = both(1, s.f11.storageBuffer16BitAccess);
    out.f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    out.f12.samplerMirrorClampToEdge = both(1, s.f12.samplerMirrorClampToEdge);
    out.f12.timelineSemaphore = both(1, s.f12.timelineSemaphore);
    out.f12.shaderOutputLayer = both(1, s.f12.shaderOutputLayer);
    out.f12.shaderOutputViewportIndex = both(1, s.f12.shaderOutputViewportIndex);
    out.f12.bufferDeviceAddress = both(1, s.f12.bufferDeviceAddress);
    out.f12.shaderBufferInt64Atomics = both(1, s.f12.shaderBufferInt64Atomics);
    out.f12.storageBuffer8BitAccess = both(1, s.f12.storageBuffer8BitAccess);
    out.f12.shaderSampledImageArrayNonUniformIndexing =
        both(1, s.f12.shaderSampledImageArrayNonUniformIndexing);
    out.f12.shaderStorageImageArrayNonUniformIndexing =
        both(1, s.f12.shaderStorageImageArrayNonUniformIndexing);
    out.f12.shaderStorageBufferArrayNonUniformIndexing =
        both(1, s.f12.shaderStorageBufferArrayNonUniformIndexing);
    out.f12.shaderSharedInt64Atomics = both(1, s.f12.shaderSharedInt64Atomics);

    // Subgroup size control: emulator enables it only if compute supports wave64 (~L560).
    out.subgroup_size_control =
        s.f13.subgroupSizeControl && (s.p13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
        s.p13.minSubgroupSize <= 64 && s.p13.maxSubgroupSize >= 64;
    out.f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    out.f13.dynamicRendering = both(1, s.f13.dynamicRendering);
    out.f13.synchronization2 = both(1, s.f13.synchronization2);
    out.f13.robustImageAccess = both(1, s.f13.robustImageAccess);
    out.f13.subgroupSizeControl = out.subgroup_size_control ? VK_TRUE : VK_FALSE;

    out.extensions.clear();
    out.push_descriptor = s.HasExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (out.push_descriptor) out.extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

    // VK_EXT_robustness2 (~L636): changes out-of-bounds behaviour, so it can change codegen.
    out.robustness2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    if (s.HasExtension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
        out.extensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        out.robustness2.robustBufferAccess2 = both(1, s.robustness2.robustBufferAccess2);
        out.robustness2.robustImageAccess2 = both(1, s.robustness2.robustImageAccess2);
        out.robustness2.nullDescriptor = both(1, s.robustness2.nullDescriptor);
    }
    out.workgroup_layout = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR};
    if (s.HasExtension(VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME)) {
        out.extensions.push_back(VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME);
        out.workgroup_layout.workgroupMemoryExplicitLayout =
            both(1, s.workgroup_layout.workgroupMemoryExplicitLayout);
    }
    out.image_atomic_int64 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT};
    if (s.HasExtension(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME)) {
        out.extensions.push_back(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);
        out.image_atomic_int64.shaderImageInt64Atomics =
            both(1, s.image_atomic_int64.shaderImageInt64Atomics);
    }
    out.maintenance8 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR};
    if (s.HasExtension(VK_KHR_MAINTENANCE_8_EXTENSION_NAME)) {
        out.extensions.push_back(VK_KHR_MAINTENANCE_8_EXTENSION_NAME);
        out.maintenance8.maintenance8 = both(1, s.maintenance8.maintenance8);
    }
}

// Every VkBool32 after sType/pNext in `enabled` is also set in `supported`.
template <typename T>
bool FeatureSubset(const T& enabled, const T& supported) {
    constexpr size_t first = offsetof(T, pNext) + sizeof(void*);
    static_assert((sizeof(T) - first) % sizeof(VkBool32) == 0, "feature struct layout");
    for (size_t at = first; at + sizeof(VkBool32) <= sizeof(T); at += sizeof(VkBool32)) {
        VkBool32 a = 0, b = 0;
        std::memcpy(&a, reinterpret_cast<const char*>(&enabled) + at, sizeof(a));
        std::memcpy(&b, reinterpret_cast<const char*>(&supported) + at, sizeof(b));
        if (a && !b) return false;
    }
    return true;
}

inline bool CoreSubset(const VkPhysicalDeviceFeatures& enabled, const VkPhysicalDeviceFeatures& supported) {
    const auto* a = reinterpret_cast<const VkBool32*>(&enabled);
    const auto* b = reinterpret_cast<const VkBool32*>(&supported);
    for (size_t i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); ++i) {
        if (a[i] && !b[i]) return false;
    }
    return true;
}

// One line for the console, comparable with the emulator's "Vulkan robustness:" log line.
inline std::string Describe(const DeviceProfile& p) {
    std::string out = "Device profile: robustBufferAccess=" + std::to_string(p.f2.features.robustBufferAccess) +
        " robustBufferAccess2=" + std::to_string(p.robustness2.robustBufferAccess2) +
        " robustImageAccess=" + std::to_string(p.f13.robustImageAccess) +
        " robustImageAccess2=" + std::to_string(p.robustness2.robustImageAccess2) +
        " nullDescriptor=" + std::to_string(p.robustness2.nullDescriptor) +
        " subgroupSizeControl=" + std::to_string(p.f13.subgroupSizeControl) +
        " shaderFloat64=" + std::to_string(p.f2.features.shaderFloat64) + " extensions=";
    for (size_t i = 0; i < p.extensions.size(); ++i) {
        out += (i ? "," : "");
        out += p.extensions[i];
    }
    return out;
}

} // namespace KytyPrewarm
