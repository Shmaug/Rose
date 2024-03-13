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

bool Instance::sDisableDebugCallback = false;

// Debug messenger functions
VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	if (Instance::sDisableDebugCallback) return VK_FALSE;

	std::string msgstr = pCallbackData->pMessage;

	{ // skip past ' ... | MessageID = ... | '
		const size_t offset = msgstr.find_last_of("|");
		if (offset != std::string::npos)
			msgstr = msgstr.substr(offset + 2); // skip '| '
	}

	std::string specstr;
	{ // Separately print 'The Vulkan spec states: '
		const size_t offset = msgstr.find("The Vulkan spec states:");
		if (offset != std::string::npos) {
			specstr = msgstr.substr(offset);
			msgstr = msgstr.substr(0, offset);
		}
	}

	auto print_fn = [&](std::ostream& stream) {
		stream << pCallbackData->pMessageIdName << ": " << std::endl;
		stream << "\t" << BOLDWHITE << msgstr << RESET << std::endl;
		if (!specstr.empty())
			stream << "\t" << specstr << std::endl;
	};

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		print_fn(std::cerr << BOLDRED);
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		print_fn(std::cerr << BOLDYELLOW);
	else
		print_fn(std::cout << BOLDCYAN);

	return VK_FALSE;
}

Instance::Instance(const std::vector<std::string>& extensions, const std::vector<std::string>& layers) {
	mContext = vk::raii::Context();

	mExtensions       = extensions | std::ranges::to<std::unordered_set<std::string>>();
	mValidationLayers = layers     | std::ranges::to<std::unordered_set<std::string>>();

	// Remove unsupported layers

	if (!mValidationLayers.empty()) {
		std::unordered_set<std::string> available;
		for (const auto& layer : mContext.enumerateInstanceLayerProperties()) available.emplace(layer.layerName.data());
		for (auto it = mValidationLayers.begin(); it != mValidationLayers.end();)
			if (available.find(*it) == available.end()) {
				std::cerr << "Warning: Removing unsupported validation layer: " << it->c_str() << std::endl;
				it = mValidationLayers.erase(it);
			} else
				it++;
	}

	// Add debug extensions if needed

	if (mValidationLayers.contains("VK_LAYER_KHRONOS_validation")) {
		mExtensions.emplace(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		mExtensions.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		mExtensions.emplace(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	std::vector<const char*> instanceExts;
	std::vector<const char*> validationLayers;
	for (const std::string& s : mExtensions) instanceExts.push_back(s.c_str());
	for (const std::string& v : mValidationLayers) validationLayers.push_back(v.c_str());

	// create instance

	vk::ApplicationInfo appInfo = {};
	appInfo.pApplicationName = "Rose";
	appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
	appInfo.pEngineName = "Rose";
	appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
	appInfo.apiVersion = mContext.enumerateInstanceVersion();
	mInstance = vk::raii::Instance(mContext, vk::InstanceCreateInfo({}, &appInfo, validationLayers, instanceExts));

	mVulkanApiVersion = appInfo.apiVersion;

	std::cout << "Vulkan " << VK_VERSION_MAJOR(mVulkanApiVersion) << "." << VK_VERSION_MINOR(mVulkanApiVersion) << "." << VK_VERSION_PATCH(mVulkanApiVersion) << std::endl;

	if (mValidationLayers.contains("VK_LAYER_KHRONOS_validation-messenger")) {
		std::cout << "Creating debug messenger" << std::endl;
		mDebugMessenger = vk::raii::DebugUtilsMessengerEXT(mInstance, vk::DebugUtilsMessengerCreateInfoEXT({},
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
			DebugCallback));
	}
}

}