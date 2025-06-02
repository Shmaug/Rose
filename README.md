Lightweight Vulkan/GLFW/ImGui wrapper.

# Usage

`WindowedApp` is the top-level component which creates a vulkan instance/device and with a window. Creating a window and a render loop is as simple as instantiating a `WindowedApp` object and calling `Run()`:

```C++
#include <Rose/Core/WindowedApp.hpp>

using namespace Rose;

int main(int argc, const char** argv) {
	WindowedApp app("MyRenderer", {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		// Other device extensions go here
	});

	ImageView renderTarget;

	// Menu bar

	app.AddMenuItem("File", [&]() {
		if (ImGui::MenuItem("Open")) {
			// TODO: Open a file
		}
	});

	// Widgets

	app.AddWidget(
		"Viewport", // title
		[&]() {
			// get command context for current swapchain image
			CommandContext& context = *app.contexts[app.swapchain->ImageIndex()];

			// get extent of widget window
			const float2 extentf = std::bit_cast<float2>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<float2>(ImGui::GetWindowContentRegionMin());
			const uint2 extent = uint2(extentf);
			if (extent.x == 0 || extent.y == 0) return;

			// create renderTarget
			if (!renderTarget || renderTarget.Extent().x != extent.x || renderTarget.Extent().y != extent.y) {
				renderTarget = ImageView::Create(
					Image::Create(context.GetDevice(), ImageInfo{
						.format = vk::Format::eR8G8B8A8Unorm,
						.extent = uint3(extent, 1),
						.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
						.queueFamilies = { context.QueueFamily() } }
					)
				);
			}

			// Draw renderTarget using ImGui
			ImGui::Image(Gui::GetTextureID(renderTarget, vk::Filter::eNearest), std::bit_cast<ImVec2>(extentf));

			RenderSomething(context, renderTarget);
		},
		true, // start open
		WindowedApp::WidgetFlagBits::eNoBorders
	);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}
```

# Structure

* `Core/` contains the main engine high-level abstraction for the vulkan-hpp API, as well as the top-level `WindowedApp`.
* `Algorithm/` contains implementations for common GPU operations.
* `Render/` contains a basic rasterizer and path tracer.
* `Scene/` implements a scene graph and glTF loader.
* `WorkGraph/` implements a work graph API (WIP, does not work).

# Building

[Vulkan SDK](https://vulkan.lunarg.com/) is required.

If your version of the Vulkan SDK does not include [Slang](https://github.com/shader-slang/slang) (before VulkanSDK version 1.3.296.0), slang must be installed manually.

On Ubuntu, the following packages are required:
`libxrandr-dev`
`libxineramade-dev`
`libxcursor-dev`
`libxi-dev`
`libxcb-keysyms1-dev`

All other dependencies are included under `thirdparty/`