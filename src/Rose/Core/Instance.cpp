#include "Instance.hpp"

#include <iostream>
#include <ranges>
#include <imgui/imgui.h>
#include <GLFW/glfw3.h>

namespace RoseEngine {

#define RESET       "\033[0m"
#define BLACK       "\033[30m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"
#define BOLDBLACK   "\033[1m\033[30m"
#define BOLDRED     "\033[1m\033[31m"
#define BOLDGREEN   "\033[1m\033[32m"
#define BOLDYELLOW  "\033[1m\033[33m"
#define BOLDBLUE    "\033[1m\033[34m"
#define BOLDMAGENTA "\033[1m\033[35m"
#define BOLDCYAN    "\033[1m\033[36m"
#define BOLDWHITE   "\033[1m\033[37m"

// Debug messenger functions
VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
	vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity_,
	vk::Flags<vk::DebugUtilsMessageTypeFlagBitsEXT> messageType_,
	const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {
	vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity = (vk::DebugUtilsMessageSeverityFlagBitsEXT)messageSeverity_;
	vk::DebugUtilsMessageTypeFlagsEXT messageType = (vk::DebugUtilsMessageTypeFlagsEXT)messageType_;

	std::string msgstr = pCallbackData->pMessage;

	/*{ // skip past ' ... | MessageID = ... | '
		const size_t offset = msgstr.find_last_of("|");
		if (offset != std::string::npos)
			msgstr = msgstr.substr(offset + 2); // skip '| '
	}*/

	std::string specstr;
	{ // Separately print 'The Vulkan spec states: '
		const size_t offset = msgstr.find("The Vulkan spec states:");
		if (offset != std::string::npos) {
			specstr = msgstr.substr(offset);
			msgstr = msgstr.substr(0, offset);
		}
	}

	auto print_fn = [&](std::ostream& stream) {
		stream << pCallbackData->pMessageIdName << std::endl;
		stream << "\t";
		stream << BOLDWHITE << msgstr << RESET;
		stream << std::endl;
		if (!specstr.empty())
			stream << "\t" << specstr << std::endl;
	};

	if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
		print_fn(std::cerr << BOLDRED);
		//throw std::runtime_error(pCallbackData->pMessage);
	} else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		print_fn(std::cerr << BOLDYELLOW);
	else
		print_fn(std::cout << BOLDCYAN);

	return VK_FALSE;
}

ref<Instance> Instance::Create(const vk::ArrayProxy<const std::string>& extensions, const vk::ArrayProxy<const std::string>& layers) {
	auto instance = make_ref<Instance>();

	instance->mContext = vk::raii::Context();

	for (const auto& e : extensions) instance->mExtensions.emplace(e);

	// Remove unsupported layers

	std::unordered_set<std::string> available;
	for (const auto& layer : instance->mContext.enumerateInstanceLayerProperties()) available.emplace(layer.layerName.data());
	for (const auto& e : layers) {
		if (!available.contains(e)) {
			std::cerr << "Warning: Removing unsupported validation layer: " << e << std::endl;
			continue;
		}
		instance->mValidationLayers.emplace(e);
	}

	// Add debug extensions if needed

	if (instance->mValidationLayers.contains("VK_LAYER_KHRONOS_validation")) {
		instance->mExtensions.emplace(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		instance->mExtensions.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		instance->mExtensions.emplace(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	std::vector<const char*> instanceExts;
	std::vector<const char*> validationLayers;
	for (const std::string& s : instance->mExtensions) instanceExts.push_back(s.c_str());
	for (const std::string& v : instance->mValidationLayers) validationLayers.push_back(v.c_str());

	// create instance

	vk::ApplicationInfo appInfo {
		.pApplicationName = "Rose",
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0),
		.pEngineName = "Rose",
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.apiVersion = instance->mContext.enumerateInstanceVersion()
	};
	instance->mInstance = instance->mContext.createInstance(vk::InstanceCreateInfo{}
		.setPApplicationInfo(&appInfo)
		.setPEnabledExtensionNames(instanceExts)
		.setPEnabledLayerNames(validationLayers)
	);

	instance->mVulkanApiVersion = appInfo.apiVersion;

	if (instance->mValidationLayers.contains("VK_LAYER_KHRONOS_validation")) {
		// instance->mDebugMessenger = vk::raii::DebugUtilsMessengerEXT(instance->mInstance, vk::DebugUtilsMessengerCreateInfoEXT{
		// 	.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
		// 	.messageType     = (vk::DebugUtilsMessageTypeFlagsEXT)vk::FlagTraits<vk::DebugUtilsMessageTypeFlagBitsEXT>::allFlags,
		// 	.pfnUserCallback = DebugCallback
		// });
	}

	return instance;
}

}
