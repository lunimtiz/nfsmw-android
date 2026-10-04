#include <jni.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string Json(const std::string& value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '\\' || c == '"') result += '\\';
    if (c >= 32) result += char(c);
  }
  return result + '"';
}

std::string Probe() {
  uint32_t loader_version = VK_API_VERSION_1_0;
  auto version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
      vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  if (version) version(&loader_version);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "NFSMW diagnostics";
  app.apiVersion = loader_version >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2
      : loader_version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  VkInstance instance{};
  VkResult result = vkCreateInstance(&ci, nullptr, &instance);
  if (result != VK_SUCCESS) {
    return "{\"compatible\":false,\"missing\":[\"No se pudo iniciar Vulkan (" +
        std::to_string(result) + ")\"]}";
  }
  uint32_t count = 0;
  result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
  if (result != VK_SUCCESS || !count) {
    vkDestroyInstance(instance, nullptr);
    return "{\"compatible\":false,\"missing\":[\"No se encontro una GPU Vulkan\"]}";
  }
  std::vector<VkPhysicalDevice> devices(count);
  result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
  if (result != VK_SUCCESS) {
    vkDestroyInstance(instance, nullptr);
    return "{\"compatible\":false,\"missing\":[\"No se pudo consultar la GPU\"]}";
  }
  // Android's native renderer selects the first physical device by default.
  VkPhysicalDevice gpu = devices[0];
  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceProperties(gpu, &props);
  vkGetPhysicalDeviceFeatures(gpu, &features);
  // Vulkan 1.2 devices report these through VkPhysicalDeviceVulkan12Features. Vulkan 1.1 devices (Mali-G68)
  // report the same features through their extensions, which the native renderer also supports.
  uint32_t extension_count = 0;
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> extensions(extension_count);
  if (extension_count) vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extension_count, extensions.data());
  auto has_extension = [&](const char* name) {
    for (const auto& e : extensions) if (!std::strcmp(e.extensionName, name)) return true;
    return false;
  };
  const bool has_bda_extension = has_extension("VK_KHR_buffer_device_address");
  const bool has_indexing_extension = has_extension("VK_EXT_descriptor_indexing");
  VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  VkPhysicalDeviceFeatures2 fs{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  if (props.apiVersion >= VK_API_VERSION_1_1) {
    void** tail = &fs.pNext;
    if (props.apiVersion >= VK_API_VERSION_1_2) {
      *tail = &v12;
    } else {
      if (has_bda_extension) { *tail = &bda; tail = &bda.pNext; }
      if (has_indexing_extension) { *tail = &indexing; tail = &indexing.pNext; }
    }
    auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (query) query(gpu, &fs);
  }
  const bool api12 = props.apiVersion >= VK_API_VERSION_1_2;
  const bool buffer_device_address = api12 ? v12.bufferDeviceAddress : bda.bufferDeviceAddress;
  const bool runtime_descriptor_array = api12 ? v12.runtimeDescriptorArray : indexing.runtimeDescriptorArray;
  const bool partially_bound = api12 ? v12.descriptorBindingPartiallyBound
                                     : indexing.descriptorBindingPartiallyBound;
  const bool update_after_bind = api12 ? v12.descriptorBindingSampledImageUpdateAfterBind
                                       : indexing.descriptorBindingSampledImageUpdateAfterBind;
  const bool update_unused = api12 ? v12.descriptorBindingUpdateUnusedWhilePending
                                   : indexing.descriptorBindingUpdateUnusedWhilePending;
  std::vector<std::string> missing;
  auto require = [&](bool supported, const char* name) {
    if (!supported) missing.emplace_back(name);
  };
  // The native shaders read their constants from dynamic UBOs (no 64-bit pointers, so no shaderInt64) and use
  // four descriptor sets: three image heaps and one with the samplers and the UBOs.
  require(props.apiVersion >= VK_API_VERSION_1_1, "Vulkan 1.1 o posterior");
  require(props.limits.maxBoundDescriptorSets >= 4, "4 descriptor sets enlazados");
  require(features.independentBlend, "independentBlend");
  require(features.shaderSampledImageArrayDynamicIndexing, "shaderSampledImageArrayDynamicIndexing");
  require(buffer_device_address, api12 ? "bufferDeviceAddress" : "bufferDeviceAddress (VK_KHR_buffer_device_address)");
  require(runtime_descriptor_array, "runtimeDescriptorArray");
  require(partially_bound, "descriptorBindingPartiallyBound");
  require(update_after_bind, "descriptorBindingSampledImageUpdateAfterBind");
  require(update_unused, "descriptorBindingUpdateUnusedWhilePending");
  std::ostringstream formats;
  std::ostringstream conversions;
  conversions << '[';
  bool first_conversion = true;
  formats << '{';
  const VkFormat bc[] = {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK,
                         VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK};
  for (unsigned i = 0; i < 5; ++i) {
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(gpu, bc[i], &fp);
    constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    bool supported = (fp.optimalTilingFeatures & required) == required;
    std::string name = "BC" + std::to_string(i + 1);
    if (!supported) {
      // The native renderer now decodes unsupported BC formats on the CPU.
      // Only reject the device if the corresponding uncompressed format is
      // unavailable too. Keep missing BC in the report as a conversion.
      const VkFormat host = i < 3 ? VK_FORMAT_R8G8B8A8_UNORM
          : i == 3 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
      VkFormatProperties host_props{};
      vkGetPhysicalDeviceFormatProperties(gpu, host, &host_props);
      require((host_props.optimalTilingFeatures & required) == required,
              i < 3 ? "Texturas RGBA8 (conversion BC1-3)"
                    : i == 3 ? "Texturas R8 (conversion BC4)" : "Texturas RG8 (conversion BC5)");
      if (!first_conversion) conversions << ',';
      first_conversion = false;
      conversions << Json(name);
    }
    if (i) formats << ',';
    formats << Json(name) << ':' << (supported ? "true" : "false");
  }
  formats << '}';
  conversions << ']';
  std::ostringstream output;
  output << "{\"gpu\":" << Json(props.deviceName)
         << ",\"vulkan\":" << Json(std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." +
              std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_PATCH(props.apiVersion)))
         << ",\"driverVersion\":" << props.driverVersion
         << ",\"vendorId\":" << props.vendorID
         << ",\"textureFormats\":" << formats.str()
         << ",\"cpuTextureConversions\":" << conversions.str()
         << ",\"vertexPipelineStoresAndAtomics\":" << (features.vertexPipelineStoresAndAtomics ? "true" : "false")
         << ",\"fragmentStoresAndAtomics\":" << (features.fragmentStoresAndAtomics ? "true" : "false")
         << ",\"occlusionQueriesDefault\":" << Json(props.vendorID == 0x13B5 ? "off" : "on")
         << ",\"checkedRenderer\":\"nativo\""
         << ",\"compatible\":" << (missing.empty() ? "true" : "false") << ",\"missing\":[";
  for (size_t i = 0; i < missing.size(); ++i) {
    if (i) output << ',';
    output << Json(missing[i]);
  }
  output << "]}";
  vkDestroyInstance(instance, nullptr);
  return output.str();
}
}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_nfsmw_android_Diagnostics_nativeGpuReport(JNIEnv* env, jclass) {
  const std::string report = Probe();
  return env->NewStringUTF(report.c_str());
}
