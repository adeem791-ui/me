// Experimental compute-only Vulkan cache warmup for KytyPS5.
// Input: KCS1 shader pipeline recipes recorded by the renderer during successful execution.
// Output: its existing signed/checksummed KytyPC2 Vulkan driver cache file.
// Does not read or modify game files, issue GPU dispatches, or execute shaders.
#define NOMINMAX
#define VK_NO_PROTOTYPES
#define XXH_INLINE_ALL
#include <windows.h>
#include <vulkan/vulkan.h>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Recipe {
    fs::path path;
    std::vector<uint32_t> spirv;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    uint32_t subgroup = 0;
    uint32_t flags = 0;
    uint32_t push_bytes = 0;
};

// The values are all host-order little-endian words. The file is a local, untrusted input.
bool ReadRecipe(const fs::path& path, Recipe& out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    const uint64_t length = fs::file_size(path, ec);
    if (ec || length < 32 || length > 20ull * 1024 * 1024) return false;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    const auto bytes = stream.tellg();
    if (bytes < std::streampos(32) || static_cast<uint64_t>(bytes) % 4) return false;
    std::vector<uint32_t> buffer(static_cast<size_t>(bytes) / 4);
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bytes))) return false;
    if (buffer[0] != 0x3153434bu || buffer[1] != 1u || buffer[2] < 5 || buffer[2] > 4'194'304) return false;
    if (buffer[6] > 64u || buffer[7] > 256u || buffer[7] % 4 != 0) return false;
    if ((buffer[5] & ~7u) != 0) return false; // Refuse unknown KCS1 pipeline flags.
    const uint64_t start = 8u + static_cast<uint64_t>(buffer[6]) * 3u;
    if (start > buffer.size() || buffer[2] != buffer.size() - start) return false;
    if (buffer[start] != 0x07230203u) return false; // SPIR-V magic
    if (buffer[4] != 0 && buffer[4] != 32 && buffer[4] != 64) return false;
    out.path = path;
    out.subgroup = buffer[4];
    out.flags = buffer[5];
    out.push_bytes = buffer[7];
    out.bindings.clear();
    for (uint32_t i = 0; i < buffer[6]; ++i) {
        const auto base = 8 + 3 * i;
        const auto kind = buffer[base + 1];
        if (buffer[base] >= 4096 || !buffer[base + 2] || buffer[base + 2] > 4096 ||
            (kind != VK_DESCRIPTOR_TYPE_SAMPLER && kind != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
             kind != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && kind != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)) return false;
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = buffer[base];
        binding.descriptorType = static_cast<VkDescriptorType>(kind);
        binding.descriptorCount = buffer[base + 2];
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        if (std::any_of(out.bindings.begin(), out.bindings.end(),
                        [&](const auto& b) { return b.binding == binding.binding; })) return false;
        out.bindings.push_back(binding);
    }
    out.spirv.assign(buffer.begin() + static_cast<ptrdiff_t>(start), buffer.end());
    return true;
}

struct VkApi {
    HMODULE lib = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueues = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 getFeatures2 = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkCreatePipelineCache createCache = nullptr;
    PFN_vkDestroyPipelineCache destroyCache = nullptr;
    PFN_vkGetPipelineCacheData getCacheData = nullptr;
    PFN_vkMergePipelineCaches mergeCaches = nullptr;
    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout createSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroySetLayout = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateComputePipelines createComputePipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;

    bool Init(VkInstance instance = VK_NULL_HANDLE, VkDevice device = VK_NULL_HANDLE) {
        if (!lib) {
            lib = LoadLibraryW(L"vulkan-1.dll");
            if (!lib) return false;
            getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib, "vkGetInstanceProcAddr"));
            if (!getInstanceProcAddr) return false;
        }
#define LOAD_I(field, name) field = reinterpret_cast<decltype(field)>(getInstanceProcAddr(instance, "vk" #name)); if (!field) return false
        LOAD_I(createInstance, CreateInstance);
        if (instance == VK_NULL_HANDLE) return true;
        LOAD_I(destroyInstance, DestroyInstance);
        LOAD_I(enumeratePhysicalDevices, EnumeratePhysicalDevices);
        LOAD_I(getPhysicalDeviceProperties, GetPhysicalDeviceProperties);
        LOAD_I(getQueues, GetPhysicalDeviceQueueFamilyProperties);
        LOAD_I(getFeatures2, GetPhysicalDeviceFeatures2);
        LOAD_I(enumerateDeviceExtensions, EnumerateDeviceExtensionProperties);
        LOAD_I(createDevice, CreateDevice);
        getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
        if (!getDeviceProcAddr) return false;
#undef LOAD_I
        if (device == VK_NULL_HANDLE) return true;
#define LOAD_D(field, name) field = reinterpret_cast<decltype(field)>(getDeviceProcAddr(device, "vk" #name)); if (!field) return false
        LOAD_D(destroyDevice, DestroyDevice);
        LOAD_D(createCache, CreatePipelineCache);
        LOAD_D(destroyCache, DestroyPipelineCache);
        LOAD_D(getCacheData, GetPipelineCacheData);
        LOAD_D(mergeCaches, MergePipelineCaches);
        LOAD_D(createShaderModule, CreateShaderModule);
        LOAD_D(destroyShaderModule, DestroyShaderModule);
        LOAD_D(createSetLayout, CreateDescriptorSetLayout);
        LOAD_D(destroySetLayout, DestroyDescriptorSetLayout);
        LOAD_D(createPipelineLayout, CreatePipelineLayout);
        LOAD_D(destroyPipelineLayout, DestroyPipelineLayout);
        LOAD_D(createComputePipelines, CreateComputePipelines);
        LOAD_D(destroyPipeline, DestroyPipeline);
#undef LOAD_D
        return true;
    }
};

std::string Signature(const VkPhysicalDeviceProperties& props) {
    std::ostringstream out;
    out << "KytyPC2:" << std::hex << std::setfill('0') << std::setw(8) << props.vendorID << ":"
        << std::setw(8) << props.deviceID << ":" << std::setw(8) << props.driverVersion << ":";
    for (uint8_t byte : props.pipelineCacheUUID) out << std::setw(2) << static_cast<unsigned>(byte);
    out << "\n";
    return out.str();
}

std::vector<uint8_t> ReadExistingCache(const fs::path& file, const std::string& signature) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto length = in.tellg();
    if (length <= static_cast<std::streamoff>(signature.size() + 8) || length > std::streampos(1024ull * 1024 * 1024)) return {};
    std::vector<uint8_t> filedata(static_cast<size_t>(length));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(filedata.data()), static_cast<std::streamsize>(length))) return {};
    if (std::memcmp(filedata.data(), signature.data(), signature.size()) != 0) return {};
    uint64_t stored_hash;
    std::memcpy(&stored_hash, filedata.data() + signature.size(), sizeof(stored_hash));
    std::vector<uint8_t> data(filedata.begin() + static_cast<ptrdiff_t>(signature.size() + 8), filedata.end());
    if (XXH3_64bits(data.data(), data.size()) != stored_hash) return {};
    return data;
}

bool WriteCache(const fs::path& file, const std::string& signature, const std::vector<uint8_t>& payload) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error) return false;
    fs::path temp = file;
    temp += L".prewarm.tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const uint64_t hash = XXH3_64bits(payload.data(), payload.size());
    out.write(signature.data(), static_cast<std::streamsize>(signature.size()));
    out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    out.flush();
    if (!out.good()) return false;
    out.close();
    // Atomic replacement on NTFS: does not erase a good existing cache if writing fails.
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::wcerr << L"Failed to publish driver cache: " << file.wstring() << L"\n";
        return false;
    }
    return true;
}

struct WorkerStats { uint32_t ok = 0, failed = 0; };

WorkerStats CompileRange(VkApi& api, VkDevice device, VkPipelineCache cache,
                         const std::vector<fs::path>& paths, unsigned shard, unsigned shards,
                         bool push_descriptor, bool subgroup_size_control) {
    WorkerStats stats;
    for (size_t i = shard; i < paths.size(); i += shards) {
        Recipe rec;
        if (!ReadRecipe(paths[i], rec)) { ++stats.failed; continue; }
        VkShaderModule shader = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptor = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        do {
            VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            sc.codeSize = rec.spirv.size() * sizeof(uint32_t);
            sc.pCode = rec.spirv.data();
            if (api.createShaderModule(device, &sc, nullptr, &shader) != VK_SUCCESS) break;
            VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            dc.bindingCount = static_cast<uint32_t>(rec.bindings.size());
            dc.pBindings = rec.bindings.data();
            // Exact layout flags from the recorded runtime pipeline are necessary for cache hits.
            if ((rec.flags & 2u) && push_descriptor) dc.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
            else if (rec.flags & 2u) break; // Can't reproduce the runtime layout.
            if (api.createSetLayout(device, &dc, nullptr, &descriptor) != VK_SUCCESS) break;
            VkPushConstantRange push{};
            push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            push.size = rec.push_bytes;
            VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pc.setLayoutCount = 1;
            pc.pSetLayouts = &descriptor;
            if (rec.push_bytes) { pc.pushConstantRangeCount = 1; pc.pPushConstantRanges = &push; }
            if (api.createPipelineLayout(device, &pc, nullptr, &layout) != VK_SUCCESS) break;
            VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
            subgroup.requiredSubgroupSize = rec.subgroup;
            VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = shader;
            stage.pName = "main";
            if (rec.subgroup) {
                if (!subgroup_size_control) break;
                stage.pNext = &subgroup;
            }
            VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cp.flags = ((rec.flags & 1u) ? VK_PIPELINE_CREATE_DISPATCH_BASE_BIT : 0u) |
                       ((rec.flags & 4u) ? VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT : 0u);
            cp.stage = stage;
            cp.layout = layout;
            cp.basePipelineIndex = -1;
            const VkResult status = api.createComputePipelines(device, cache, 1, &cp, nullptr, &pipeline);
            if (status != VK_SUCCESS) break;
            ++stats.ok;
        } while (false);
        if (!pipeline) ++stats.failed;
        if (pipeline) api.destroyPipeline(device, pipeline, nullptr);
        if (layout) api.destroyPipelineLayout(device, layout, nullptr);
        if (descriptor) api.destroySetLayout(device, descriptor, nullptr);
        if (shader) api.destroyShaderModule(device, shader, nullptr);
    }
    return stats;
}

// Sanity checks for the local recipe parser. No GPU or game files needed.
int SelfTest() {
    const fs::path root = fs::temp_directory_path() /
        (L"KytyPrewarmSelfTest-" + std::to_wstring(GetCurrentProcessId()) + L".kcs");
    const std::array<uint32_t, 13> valid = {
        0x3153434bu, 1u, 5u, 32u, 0u, 0u, 0u, 128u,
        0x07230203u, 0x00010300u, 0u, 4u, 0u
    };
    {
        std::ofstream out(root, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(valid.data()), sizeof(valid));
        if (!out.good()) return 8;
    }
    Recipe rec;
    bool success = ReadRecipe(root, rec) && rec.spirv.size() == 5 && rec.push_bytes == 128;
    {
        auto invalid = valid;
        invalid[0] = 0;
        std::ofstream out(root, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(invalid.data()), sizeof(invalid));
        if (!out.good()) return 8;
    }
    Recipe corrupt;
    success = success && !ReadRecipe(root, corrupt);
    std::error_code ec;
    fs::remove(root, ec);
    std::cout << (success ? "KCS1 parser self-test: PASS\n" : "KCS1 parser self-test: FAIL\n");
    return success ? 0 : 8;
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--selftest") return SelfTest();
    fs::path records, cache;
    unsigned workers = 2;
    for (int i = 1; i < argc; ++i) {
        std::wstring value = argv[i];
        if ((value == L"--records" || value == L"--cache" || value == L"--workers") && i + 1 < argc) {
            std::wstring arg = argv[++i];
            if (value == L"--records") records = arg;
            if (value == L"--cache") cache = arg;
            if (value == L"--workers") {
                try { workers = static_cast<unsigned>(std::stoul(arg)); } catch (...) { return 2; }
            }
        } else {
            std::cerr << "Usage: kyty_compute_prewarm --records <directory> --cache <PPSA26344.bin> [--workers 1..4]\n";
            return 2;
        }
    }
    if (records.empty() || cache.empty() || workers == 0 || workers > 4) return 2;
    std::error_code error;
    if (!fs::is_directory(records, error)) {
        std::cout << "No recorded compute pipelines yet. Start once with RECORD_COMPUTE_SHADERS.cmd.\n";
        return 3;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(records, error)) {
        if (error) break;
        if (entry.is_regular_file() && entry.path().extension() == L".kcs") files.push_back(entry.path());
        if (files.size() > 30000) { std::cerr << "Too many records; refusing unsafe scan\n"; return 4; }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::cout << "No recorded compute pipeline recipes in directory.\n"; return 3; }
    VkApi api;
    if (!api.Init()) { std::cerr << "Vulkan driver loader unavailable\n"; return 4; }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Kyty Compute Cache Prewarm";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ic.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (api.createInstance(&ic, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Vulkan 1.3 instance unavailable\n"; return 4;
    }
    auto cleanup_instance = [&]() { if (instance) api.destroyInstance(instance, nullptr); };
    if (!api.Init(instance)) { std::cerr << "Could not load instance Vulkan procedures\n"; cleanup_instance(); return 4; }
    uint32_t count = 0;
    if (api.enumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || !count || count > 32) {
        cleanup_instance(); return 4;
    }
    std::vector<VkPhysicalDevice> devices(count);
    api.enumeratePhysicalDevices(instance, &count, devices.data());
    VkPhysicalDevice selected = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    for (auto phys : devices) {
        VkPhysicalDeviceProperties p{};
        api.getPhysicalDeviceProperties(phys, &p);
        if (p.vendorID == 0x10de && (!selected || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) {
            selected = phys;
            props = p;
        }
    }
    if (!selected) { std::cerr << "NVIDIA GPU not found (tool prevents warming the wrong driver cache)\n"; cleanup_instance(); return 4; }
    const auto signature = Signature(props);
    std::cout << "GPU: " << props.deviceName << "\nCompute recordings: " << files.size() << "\n";
    auto initial = ReadExistingCache(cache, signature);
    std::cout << "Existing compatible Vulkan cache: " << initial.size() << " bytes\n";
    uint32_t qcount = 0;
    api.getQueues(selected, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(qcount);
    api.getQueues(selected, &qcount, queues.data());
    uint32_t queue_index = qcount;
    for (uint32_t i = 0; i < qcount; ++i) if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queue_index = i; break; }
    if (queue_index == qcount) { cleanup_instance(); return 4; }
    uint32_t ecnt = 0;
    api.enumerateDeviceExtensions(selected, nullptr, &ecnt, nullptr);
    std::vector<VkExtensionProperties> exts(ecnt);
    if (ecnt) api.enumerateDeviceExtensions(selected, nullptr, &ecnt, exts.data());
    const auto has_ext = [&](const char* name) {
        return std::any_of(exts.begin(), exts.end(), [&](const auto& ext){ return std::strcmp(ext.extensionName, name) == 0; });
    };
    const bool push_descriptor = has_ext(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    std::vector<const char*> enabled_extensions;
    if (push_descriptor) enabled_extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12; f12.pNext = &f13;
    api.getFeatures2(selected, &f2);
    const bool subgroup = f13.subgroupSizeControl == VK_TRUE;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = queue_index; qc.queueCount = 1; qc.pQueuePriorities = &priority;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.pNext = &f2;
    dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &qc;
    dc.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
    dc.ppEnabledExtensionNames = enabled_extensions.data();
    VkDevice device = VK_NULL_HANDLE;
    VkResult create_result = api.createDevice(selected, &dc, nullptr, &device);
    if (create_result != VK_SUCCESS || !api.Init(instance, device)) {
        std::cerr << "Could not create Vulkan compute device: " << create_result << "\n";
        if (device && api.destroyDevice) api.destroyDevice(device, nullptr);
        cleanup_instance(); return 4;
    }
    const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0,
                                        initial.size(), initial.empty() ? nullptr : initial.data()};
    std::vector<VkPipelineCache> shard_caches(std::min(workers, static_cast<unsigned>(files.size())), VK_NULL_HANDLE);
    bool create_ok = true;
    for (size_t i = 0; i < shard_caches.size(); ++i) {
        VkPipelineCacheCreateInfo shard_ci = ci;
        if (i != 0) { shard_ci.initialDataSize = 0; shard_ci.pInitialData = nullptr; }
        if (api.createCache(device, &shard_ci, nullptr, &shard_caches[i]) != VK_SUCCESS) {
            create_ok = false; break;
        }
    }
    if (!create_ok) {
        std::cerr << "Driver rejected pipeline cache creation\n";
        for (auto handle : shard_caches) if (handle) api.destroyCache(device, handle, nullptr);
        api.destroyDevice(device, nullptr); cleanup_instance(); return 4;
    }
    std::vector<WorkerStats> stats(shard_caches.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < shard_caches.size(); ++i) {
        threads.emplace_back([&, i] { stats[i] = CompileRange(api, device, shard_caches[i], files,
            static_cast<unsigned>(i), static_cast<unsigned>(shard_caches.size()), push_descriptor, subgroup); });
    }
    for (auto& thread : threads) thread.join();
    unsigned succeeded = 0, failed = 0;
    for (const auto& stat : stats) { succeeded += stat.ok; failed += stat.failed; }
    std::cout << "Pipeline results: compiled=" << succeeded << " failed_or_unavailable=" << failed << "\n";
    bool saved = false;
    if (succeeded) {
        if (shard_caches.size() == 1 || api.mergeCaches(device, shard_caches[0],
                static_cast<uint32_t>(shard_caches.size() - 1), shard_caches.data() + 1) == VK_SUCCESS) {
            size_t required = 0;
            VkResult status = api.getCacheData(device, shard_caches[0], &required, nullptr);
            if (status == VK_SUCCESS && required >= 32 && required <= 512ull * 1024 * 1024) {
                std::vector<uint8_t> bytes(required);
                status = api.getCacheData(device, shard_caches[0], &required, bytes.data());
                if (status == VK_SUCCESS) { bytes.resize(required); saved = WriteCache(cache, signature, bytes); }
            }
        }
    }
    for (auto h : shard_caches) if (h) api.destroyCache(device, h, nullptr);
    api.destroyDevice(device, nullptr);
    cleanup_instance();
    std::cout << (saved ? "Warmed driver cache saved. Launch Kyty normally.\n"
                        : "No new cache saved; original cache left intact.\n");
    return saved ? 0 : 5;
}