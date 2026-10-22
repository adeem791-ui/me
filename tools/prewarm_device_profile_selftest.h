// GPU-free checks for prewarm_device_profile.h. Used by kyty_compute_prewarm --selftest and
// by tools/prewarm_device_profile_test.cpp on Linux CI.
#pragma once

#include "prewarm_device_profile.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace KytyPrewarm {

inline void SetAllFeatures(SupportedDevice& s) {
    auto fill = [](auto& feature) {
        constexpr size_t first = offsetof(std::remove_reference_t<decltype(feature)>, pNext) + sizeof(void*);
        for (size_t at = first; at + sizeof(VkBool32) <= sizeof(feature); at += sizeof(VkBool32)) {
            const VkBool32 one = VK_TRUE;
            std::memcpy(reinterpret_cast<char*>(&feature) + at, &one, sizeof(one));
        }
    };
    auto* core = reinterpret_cast<VkBool32*>(&s.f2.features);
    for (size_t i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); ++i) core[i] = VK_TRUE;
    fill(s.f11); fill(s.f12); fill(s.f13); fill(s.robustness2); fill(s.workgroup_layout);
    fill(s.image_atomic_int64); fill(s.maintenance8);
    s.p12.shaderSignedZeroInfNanPreserveFloat64 = VK_TRUE;
    s.p13.minSubgroupSize = 32;
    s.p13.maxSubgroupSize = 64;
    s.p13.requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
}

inline bool ProfileIsSubset(const DeviceProfile& p, const SupportedDevice& s) {
    bool ok = CoreSubset(p.f2.features, s.f2.features) && FeatureSubset(p.f11, s.f11) &&
              FeatureSubset(p.f12, s.f12) && FeatureSubset(p.f13, s.f13) &&
              FeatureSubset(p.robustness2, s.robustness2) &&
              FeatureSubset(p.workgroup_layout, s.workgroup_layout) &&
              FeatureSubset(p.image_atomic_int64, s.image_atomic_int64) &&
              FeatureSubset(p.maintenance8, s.maintenance8);
    for (const char* ext : p.extensions) ok = ok && s.HasExtension(ext);
    return ok;
}

// Returns true when every check passes; prints the first failure.
inline bool DeviceProfileSelfTest() {
    bool ok = true;
    auto check = [&](bool value, const char* what) {
        if (!value && ok) std::printf("Device profile self-test failed: %s\n", what);
        ok = ok && value;
    };
    const char* all_exts[] = {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
                              VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME,
                              VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME,
                              VK_KHR_MAINTENANCE_8_EXTENSION_NAME};

    {   // Full-featured device: robustness matches the emulator.
        SupportedDevice s;
        SetAllFeatures(s);
        for (const char* e : all_exts) s.extensions.emplace_back(e);
        DeviceProfile p;
        BuildDeviceProfile(s, p);
        check(p.f2.features.robustBufferAccess == VK_TRUE, "robustBufferAccess");
        check(p.robustness2.robustBufferAccess2 == VK_TRUE, "robustBufferAccess2");
        check(p.robustness2.robustImageAccess2 == VK_TRUE, "robustImageAccess2");
        check(p.robustness2.nullDescriptor == VK_TRUE, "nullDescriptor");
        check(p.f13.robustImageAccess == VK_TRUE, "robustImageAccess");
        check(p.Enabled(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME), "robustness2 extension enabled");
        check(p.subgroup_size_control && p.f13.subgroupSizeControl, "subgroup size control");
        check(p.push_descriptor, "push descriptor");
        check(p.f2.features.shaderFloat64 == VK_TRUE, "shaderFloat64 with float controls");
        // Not enabled by the emulator, so must stay off even though supported.
        check(p.f2.features.sparseBinding == VK_FALSE, "sparseBinding stays off");
        check(p.f12.descriptorIndexing == VK_FALSE, "descriptorIndexing stays off");
        check(p.f13.inlineUniformBlock == VK_FALSE, "inlineUniformBlock stays off");
        check(ProfileIsSubset(p, s), "full device: profile within support");
        // The create chain must reach robustness2.
        bool found = false;
        for (auto* node = static_cast<VkBaseOutStructure*>(p.CreateChain()); node; node = node->pNext) {
            found = found || node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        }
        check(found, "robustness2 struct is in the vkCreateDevice chain");
    }
    {   // Device without the extensions and with gaps: nothing unsupported is enabled.
        SupportedDevice s;
        SetAllFeatures(s);
        s.f2.features.shaderFloat64 = VK_FALSE;
        s.f13.robustImageAccess = VK_FALSE;
        s.p13.maxSubgroupSize = 32; // no wave64
        DeviceProfile p;
        BuildDeviceProfile(s, p);
        check(p.extensions.empty(), "no extensions when none are available");
        check(p.robustness2.robustBufferAccess2 == VK_FALSE, "no robustness2 without extension");
        check(p.f2.features.shaderFloat64 == VK_FALSE, "unsupported shaderFloat64");
        check(p.f13.robustImageAccess == VK_FALSE, "unsupported robustImageAccess");
        check(!p.subgroup_size_control && !p.f13.subgroupSizeControl, "no wave64, no size control");
        check(ProfileIsSubset(p, s), "limited device: profile within support");
        for (auto* node = static_cast<VkBaseOutStructure*>(p.CreateChain()); node; node = node->pNext) {
            check(node->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
                  "absent extension struct is not chained");
        }
    }
    {   // shaderFloat64 needs the float-controls property too (emulator rule).
        SupportedDevice s;
        SetAllFeatures(s);
        s.p12.shaderSignedZeroInfNanPreserveFloat64 = VK_FALSE;
        DeviceProfile p;
        BuildDeviceProfile(s, p);
        check(p.f2.features.shaderFloat64 == VK_FALSE, "shaderFloat64 needs float controls");
    }
    {   // Query chain only links structs for extensions that exist.
        SupportedDevice s;
        s.extensions.emplace_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        s.QueryChain();
        int count = 0;
        for (auto* node = reinterpret_cast<VkBaseOutStructure*>(&s.f2); node; node = node->pNext) ++count;
        check(count == 5, "query chain: features2, 1.1, 1.2, 1.3, robustness2");
    }
    std::printf("Device profile self-test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

} // namespace KytyPrewarm
