#include <vulkan/vulkan.hpp>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#if defined(USE_PLATFORM_QT)
# include <QGuiApplication>
#endif
#include "VulkanWindow.h"

using namespace std;


// constants
constexpr const char* appName = "12-qtWindow";


// shader code in SPIR-V binary
static const uint32_t vsSpirv[] = {
#include "shader.vert.spv"
};
static const uint32_t fsSpirv[] = {
#include "shader.frag.spv"
};


// global application data
class App {
public:

	App(int argc, char** argv);
	~App();

	void init();
	void recreateSwapchain(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);
	void frame();


#if defined(USE_PLATFORM_QT)
	// QGuiApplication needs to be constructed before Vulkan Instance as we need to know QGuiApplication::platformName and Vulkan required Instance extensions
	QGuiApplication qtApp;
#endif

	// Vulkan instance must be destructed as the last one. At least on Linux, it must be destroyed after the display connection
	vk::Instance instance;

	// window needs to be placed before swapchain, at least on Wayland platform
	VulkanWindow window;

	// Vulkan handles and objects
	// (they need to be placed in particular (not arbitrary) order
	// because they are destructed from the last one to the first one)
	vk::SurfaceKHR surface;
	vk::PhysicalDevice physicalDevice;
	uint32_t graphicsQueueFamily;
	uint32_t presentationQueueFamily;
	vk::Device device;
	vk::Queue graphicsQueue;
	vk::Queue presentationQueue;
	vk::SurfaceFormatKHR surfaceFormat;
	vk::RenderPass renderPass;
	vk::SwapchainKHR swapchain;
	vector<vk::ImageView> swapchainImageViews;
	vector<vk::Framebuffer> framebuffers;
	vk::CommandPool commandPool;
	vk::CommandBuffer commandBuffer;
	vk::Semaphore imageAvailableSemaphore;
	vk::Semaphore renderFinishedSemaphore;
	vk::ShaderModule vsModule;
	vk::ShaderModule fsModule;
	vk::PipelineLayout pipelineLayout;
	vk::Pipeline pipeline;

	size_t frameID = 0;
	enum class FrameUpdateMode { OnDemand, Continuous, MaxFrameRate };
	FrameUpdateMode frameUpdateMode = FrameUpdateMode::Continuous;

};


/// Construct application object
App::App(int argc, char** argv)
#if defined(USE_PLATFORM_QT)
	: qtApp(argc, argv)
#endif
{
	// process command-line arguments
	for(int i=1; i<argc; i++)
		if(strcmp(argv[i], "--on-demand") == 0)
			frameUpdateMode = FrameUpdateMode::OnDemand;
		else if(strcmp(argv[i], "--continuous") == 0)
			frameUpdateMode = FrameUpdateMode::Continuous;
		else if(strcmp(argv[i], "--max-frame-rate") == 0)
			frameUpdateMode = FrameUpdateMode::MaxFrameRate;
		else {
			if(strcmp(argv[i], "--help") != 0 && strcmp(argv[i], "-h") != 0)
				cout << "Unrecognized option: " << argv[i] << endl;
			cout << appName << " usage:\n"
					"   --help or -h:  usage information\n"
					"   --on-demand:   on demand window content refresh\n"
					"   --continuous:  constantly update window content\n"
					"   --max-frame-rate:  ignore screen update frequency and update\n"
					"                      window content as often as possible\n" << endl;
			exit(99);
		}
}


App::~App()
{
	if(device) {
		device.destroy(pipeline);
		device.destroy(pipelineLayout);
		device.destroy(fsModule);
		device.destroy(vsModule);
		device.destroy(renderFinishedSemaphore);
		device.destroy(imageAvailableSemaphore);
		device.destroy(commandPool);
		for(auto f : framebuffers)  device.destroy(f);
		for(auto v : swapchainImageViews)  device.destroy(v);
		device.destroy(swapchain);
		device.destroy(renderPass);
		device.destroy();
	}
#if !defined(USE_PLATFORM_QT) // In Qt, QWindow owns the surface and it will destroy the surface.
	if(instance)
		instance.destroy(surface);
#endif
	window.destroy();
	instance.destroy();
}


void App::init()
{
	// Vulkan instance
	instance =
		vk::createInstance(
			vk::InstanceCreateInfo{
				vk::InstanceCreateFlags(),  // flags
				&(const vk::ApplicationInfo&)vk::ApplicationInfo{
					appName,                 // application name
					VK_MAKE_VERSION(0,0,0),  // application version
					nullptr,                 // engine name
					VK_MAKE_VERSION(0,0,0),  // engine version
					VK_API_VERSION_1_0,      // api version
				},
				0, nullptr,  // no layers
				VulkanWindow::requiredExtensionCount(),  // enabled extension count
				VulkanWindow::requiredExtensionNames(),  // enabled extension names
			});

	// create surface
	surface = window.init(instance, {1024, 768}, appName);

	// find compatible devices
	// (On Windows, all graphics adapters capable of monitor output are usually compatible devices.
	// On Linux X11 platform, only one graphics adapter is compatible device (the one that
	// renders the window).
	vector<vk::PhysicalDevice> deviceList = instance.enumeratePhysicalDevices();
	vector<tuple<vk::PhysicalDevice, uint32_t>> compatibleDevicesSingleQueue;
	vector<tuple<vk::PhysicalDevice, uint32_t, uint32_t>> compatibleDevicesTwoQueues;
	for(vk::PhysicalDevice pd : deviceList) {

		// skip devices without VK_KHR_swapchain
		auto extensionList = pd.enumerateDeviceExtensionProperties();
		for(vk::ExtensionProperties& e : extensionList)
			if(strcmp(e.extensionName, "VK_KHR_swapchain") == 0)
				goto swapchainSupported;
		continue;
		swapchainSupported:

		// select queues (for graphics rendering and for presentation)
		uint32_t graphicsQueueFamily = UINT32_MAX;
		uint32_t presentationQueueFamily = UINT32_MAX;
		vector<vk::QueueFamilyProperties> queueFamilyList = pd.getQueueFamilyProperties();
		uint32_t i = 0;
		for(uint32_t i=0, c=uint32_t(queueFamilyList.size()); i<c; i++) {
			bool p = pd.getSurfaceSupportKHR(i, surface) != 0;
			if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics) {
				if(p) {
					compatibleDevicesSingleQueue.emplace_back(pd, i);
					goto nextDevice;
				}
				if(graphicsQueueFamily == UINT32_MAX)
					graphicsQueueFamily = i;
			}
			else {
				if(p)
					if(presentationQueueFamily == UINT32_MAX)
						presentationQueueFamily = i;
			}
		}
		compatibleDevicesTwoQueues.emplace_back(pd, graphicsQueueFamily, presentationQueueFamily);
		nextDevice:;
	}
	cout << "Compatible devices:" << endl;
	for(auto& t : compatibleDevicesSingleQueue)
		cout << "   " << get<0>(t).getProperties().deviceName << endl;
	for(auto& t:compatibleDevicesTwoQueues)
		cout << "   " << get<0>(t).getProperties().deviceName << endl;

	// choose device
	if(compatibleDevicesSingleQueue.size() > 0) {
		auto t = compatibleDevicesSingleQueue.front();
		physicalDevice = get<0>(t);
		graphicsQueueFamily = get<1>(t);
		presentationQueueFamily = graphicsQueueFamily;
	}
	else if(compatibleDevicesTwoQueues.size() > 0) {
		auto t = compatibleDevicesTwoQueues.front();
		physicalDevice = get<0>(t);
		graphicsQueueFamily = get<1>(t);
		presentationQueueFamily = get<2>(t);
	}
	else
		throw runtime_error("No compatible devices.");
	cout << "Using device:\n"
			"   " << physicalDevice.getProperties().deviceName << endl;

	// create device
	device =
		physicalDevice.createDevice(
			vk::DeviceCreateInfo{
				vk::DeviceCreateFlags(),  // flags
				compatibleDevicesSingleQueue.size()>0 ? uint32_t(1) : uint32_t(2),  // queueCreateInfoCount
				array{  // pQueueCreateInfos
					vk::DeviceQueueCreateInfo{
						vk::DeviceQueueCreateFlags(),
						graphicsQueueFamily,
						1,
						&(const float&)1.f,
					},
					vk::DeviceQueueCreateInfo{
						vk::DeviceQueueCreateFlags(),
						presentationQueueFamily,
						1,
						&(const float&)1.f,
					},
				}.data(),
				0, nullptr,  // no layers
				1,           // number of enabled extensions
				array<const char*,1>{"VK_KHR_swapchain"}.data(),  // enabled extension names
				nullptr,    // enabled features
			}
		);

	// get queues
	graphicsQueue = device.getQueue(graphicsQueueFamily, 0);
	presentationQueue = device.getQueue(presentationQueueFamily, 0);

	// choose surface format
	constexpr array supportedSurfaceFormats{
		vk::SurfaceFormatKHR{ vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
		vk::SurfaceFormatKHR{ vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
		vk::SurfaceFormatKHR{ vk::Format::eA8B8G8R8SrgbPack32, vk::ColorSpaceKHR::eSrgbNonlinear },
	};
	vector<vk::SurfaceFormatKHR> availableSurfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
		for(vk::SurfaceFormatKHR sf : availableSurfaceFormats)
			cout << vk::to_string(sf.format) << " " << vk::to_string(sf.colorSpace) << endl;
	if(availableSurfaceFormats.size()==1 && availableSurfaceFormats[0].format==vk::Format::eUndefined)
		// Vulkan spec allowed single eUndefined value until 1.1.111 (2019-06-10)
		// with the meaning you can use any valid vk::Format value;
		// now it is forbidden, but let's handle any old driver
		surfaceFormat = supportedSurfaceFormats[0];
	else {
		for(vk::SurfaceFormatKHR sf : availableSurfaceFormats) {
			auto it = std::find(supportedSurfaceFormats.begin(), supportedSurfaceFormats.end(), sf);
			if(it != supportedSurfaceFormats.end()) {
				surfaceFormat = *it;
				goto surfaceFormatFound;
			}
		}
		if(availableSurfaceFormats.size() == 0)  // Vulkan must return at least one format (this is mandated since Vulkan 1.0.37 (2016-10-10), but was missing before probably because of omission)
			throw std::runtime_error("Vulkan error: getSurfaceFormatsKHR() returned empty list.");
		surfaceFormat = availableSurfaceFormats[0];
	surfaceFormatFound:;
	}

	// render pass
	renderPass =
		device.createRenderPass(
			vk::RenderPassCreateInfo(
				vk::RenderPassCreateFlags(),  // flags
				1,                            // attachmentCount
				array{  // pAttachments
					vk::AttachmentDescription(
						vk::AttachmentDescriptionFlags(),  // flags
						surfaceFormat.format,              // format
						vk::SampleCountFlagBits::e1,       // samples
						vk::AttachmentLoadOp::eClear,      // loadOp
						vk::AttachmentStoreOp::eStore,     // storeOp
						vk::AttachmentLoadOp::eDontCare,   // stencilLoadOp
						vk::AttachmentStoreOp::eDontCare,  // stencilStoreOp
						vk::ImageLayout::eUndefined,       // initialLayout
						vk::ImageLayout::ePresentSrcKHR    // finalLayout
					),
				}.data(),
				1,  // subpassCount
				array{  // pSubpasses
					vk::SubpassDescription(
						vk::SubpassDescriptionFlags(),     // flags
						vk::PipelineBindPoint::eGraphics,  // pipelineBindPoint
						0,        // inputAttachmentCount
						nullptr,  // pInputAttachments
						1,        // colorAttachmentCount
						array{  // pColorAttachments
							vk::AttachmentReference(
								0,  // attachment
								vk::ImageLayout::eColorAttachmentOptimal  // layout
							),
						}.data(),
						nullptr,  // pResolveAttachments
						nullptr,  // pDepthStencilAttachment
						0,        // preserveAttachmentCount
						nullptr   // pPreserveAttachments
					),
				}.data(),
				1,  // dependencyCount
				array{  // pDependencies
					vk::SubpassDependency(
						VK_SUBPASS_EXTERNAL,   // srcSubpass
						0,                     // dstSubpass
						vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),  // srcStageMask
						vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),  // dstStageMask
						vk::AccessFlags(),     // srcAccessMask
						vk::AccessFlags(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite),  // dstAccessMask
						vk::DependencyFlags()  // dependencyFlags
					),
				}.data()
			)
		);

	// semaphores
	imageAvailableSemaphore =
		device.createSemaphore(
			vk::SemaphoreCreateInfo(
				vk::SemaphoreCreateFlags()  // flags
			)
		);
	renderFinishedSemaphore =
		device.createSemaphore(
			vk::SemaphoreCreateInfo(
				vk::SemaphoreCreateFlags()  // flags
			)
		);

	// commandPool and commandBuffer
	commandPool =
		device.createCommandPool(
			vk::CommandPoolCreateInfo(
				vk::CommandPoolCreateFlagBits::eTransient |  // flags
					vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				graphicsQueueFamily  // queueFamilyIndex
			)
		);
	commandBuffer =
		device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo(
				commandPool,  // commandPool
				vk::CommandBufferLevel::ePrimary,  // level
				1  // commandBufferCount
			)
		)[0];

	// create shader modules
	vsModule =
		device.createShaderModule(
			vk::ShaderModuleCreateInfo(
				vk::ShaderModuleCreateFlags(),  // flags
				sizeof(vsSpirv),  // codeSize
				vsSpirv  // pCode
			)
		);
	fsModule =
		device.createShaderModule(
			vk::ShaderModuleCreateInfo(
				vk::ShaderModuleCreateFlags(),  // flags
				sizeof(fsSpirv),  // codeSize
				fsSpirv  // pCode
			)
		);

	// pipeline layout
	pipelineLayout =
		device.createPipelineLayout(
			vk::PipelineLayoutCreateInfo{
				vk::PipelineLayoutCreateFlags(),  // flags
				0,       // setLayoutCount
				nullptr, // pSetLayouts
				0,       // pushConstantRangeCount
				nullptr  // pPushConstantRanges
			}
		);
}


/** Recreate swapchain and pipeline callback method.
 *  The method is usually called after the window resize and on the application start. */
void App::recreateSwapchain(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent)
{
	// clear resources
	for(auto f : framebuffers)  device.destroy(f);
	for(auto v : swapchainImageViews)  device.destroy(v);
	framebuffers.clear();
	swapchainImageViews.clear();

	// create new swapchain
	constexpr uint32_t requestedImageCount = 2;
	cout<<"Recreating swapchain "<<newSurfaceExtent.width<<"x"<<newSurfaceExtent.height<<", surfaceCapabilities: "
			<<surfaceCapabilities.currentExtent.width<<"x"<<surfaceCapabilities.currentExtent.height
			<<" and min: "<<surfaceCapabilities.minImageCount<<" max: "<<surfaceCapabilities.maxImageCount<<endl;
	swapchain =
		device.createSwapchainKHR(
			vk::SwapchainCreateInfoKHR(
				vk::SwapchainCreateFlagsKHR(),  // flags
				surface,                        // surface
				surfaceCapabilities.maxImageCount==0  // minImageCount
					? max(requestedImageCount, surfaceCapabilities.minImageCount)
					: clamp(requestedImageCount, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount),
				surfaceFormat.format,           // imageFormat
				surfaceFormat.colorSpace,       // imageColorSpace
				newSurfaceExtent,               // imageExtent
				1,                              // imageArrayLayers
				vk::ImageUsageFlagBits::eColorAttachment,  // imageUsage
				(graphicsQueueFamily==presentationQueueFamily) ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent, // imageSharingMode
				(graphicsQueueFamily==presentationQueueFamily) ? uint32_t(0) : uint32_t(2),  // queueFamilyIndexCount
				(graphicsQueueFamily==presentationQueueFamily) ? nullptr : array<uint32_t,2>{graphicsQueueFamily,presentationQueueFamily}.data(),  // pQueueFamilyIndices
				surfaceCapabilities.currentTransform,    // preTransform
				vk::CompositeAlphaFlagBitsKHR::eOpaque,  // compositeAlpha
				[](App& app)  // presentMode
					{
						if(VulkanWindow::mailboxPresentModePreferred)
							return vk::PresentModeKHR::eMailbox;
						else
						{
							// Fifo is used unless MaxFrameRate was requested
							if(app.frameUpdateMode != FrameUpdateMode::MaxFrameRate)
								return vk::PresentModeKHR::eFifo;

							// for MaxFrameRate, try Mailbox and Immediate if they are available
							vector<vk::PresentModeKHR> modes = app.physicalDevice.getSurfacePresentModesKHR(app.surface);
							if(find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) != modes.end())
								return vk::PresentModeKHR::eMailbox;
							if(find(modes.begin(), modes.end(), vk::PresentModeKHR::eImmediate) != modes.end())
								return vk::PresentModeKHR::eImmediate;

							// Fifo is always supported
							return vk::PresentModeKHR::eFifo;
						}
					}(*this),
				VK_TRUE,   // clipped
				swapchain  // oldSwapchain
			)
		);

	// swapchain images and image views
	vector<vk::Image> swapchainImages = device.getSwapchainImagesKHR(swapchain);
	swapchainImageViews.reserve(swapchainImages.size());
	for(vk::Image image : swapchainImages)
		swapchainImageViews.emplace_back(
			device.createImageView(
				vk::ImageViewCreateInfo(
					vk::ImageViewCreateFlags(),  // flags
					image,                       // image
					vk::ImageViewType::e2D,      // viewType
					surfaceFormat.format,        // format
					vk::ComponentMapping(),      // components
					vk::ImageSubresourceRange(   // subresourceRange
						vk::ImageAspectFlagBits::eColor,  // aspectMask
						0,  // baseMipLevel
						1,  // levelCount
						0,  // baseArrayLayer
						1   // layerCount
					)
				)
			)
		);

	// framebuffers
	framebuffers.reserve(swapchainImages.size());
	for(size_t i=0, c=swapchainImages.size(); i<c; i++)
		framebuffers.emplace_back(
			device.createFramebuffer(
				vk::FramebufferCreateInfo(
					vk::FramebufferCreateFlags(),  // flags
					renderPass,  // renderPass
					1,  // attachmentCount
					&swapchainImageViews[i],  // pAttachments
					newSurfaceExtent.width,  // width
					newSurfaceExtent.height,  // height
					1  // layers
				)
			)
		);

	// pipeline
	pipeline =
		device.createGraphicsPipeline(
			nullptr,  // pipelineCache
			vk::GraphicsPipelineCreateInfo(
				vk::PipelineCreateFlags(),  // flags

				// shader stages
				2,  // stageCount
				array{  // pStages
					vk::PipelineShaderStageCreateInfo{
						vk::PipelineShaderStageCreateFlags(),  // flags
						vk::ShaderStageFlagBits::eVertex,  // stage
						vsModule,  // module
						"main",  // pName
						nullptr  // pSpecializationInfo
					},
					vk::PipelineShaderStageCreateInfo{
						vk::PipelineShaderStageCreateFlags(),  // flags
						vk::ShaderStageFlagBits::eFragment,  // stage
						fsModule,  // module
						"main",  // pName
						nullptr  // pSpecializationInfo
					},
				}.data(),

				// vertex input
				&(const vk::PipelineVertexInputStateCreateInfo&)vk::PipelineVertexInputStateCreateInfo{  // pVertexInputState
					vk::PipelineVertexInputStateCreateFlags(),  // flags
					0,        // vertexBindingDescriptionCount
					nullptr,  // pVertexBindingDescriptions
					0,        // vertexAttributeDescriptionCount
					nullptr   // pVertexAttributeDescriptions
				},

				// input assembly
				&(const vk::PipelineInputAssemblyStateCreateInfo&)vk::PipelineInputAssemblyStateCreateInfo{  // pInputAssemblyState
					vk::PipelineInputAssemblyStateCreateFlags(),  // flags
					vk::PrimitiveTopology::eTriangleList,  // topology
					VK_FALSE  // primitiveRestartEnable
				},

				// tessellation
				nullptr, // pTessellationState

				// viewport
				&(const vk::PipelineViewportStateCreateInfo&)vk::PipelineViewportStateCreateInfo{  // pViewportState
					vk::PipelineViewportStateCreateFlags(),  // flags
					1,  // viewportCount
					array{  // pViewports
						vk::Viewport(0.f, 0.f, float(newSurfaceExtent.width), float(newSurfaceExtent.height), 0.f, 1.f),
					}.data(),
					1,  // scissorCount
					array{  // pScissors
						vk::Rect2D(vk::Offset2D(0,0), newSurfaceExtent)
					}.data(),
				},

				// rasterization
				&(const vk::PipelineRasterizationStateCreateInfo&)vk::PipelineRasterizationStateCreateInfo{  // pRasterizationState
					vk::PipelineRasterizationStateCreateFlags(),  // flags
					VK_FALSE,  // depthClampEnable
					VK_FALSE,  // rasterizerDiscardEnable
					vk::PolygonMode::eFill,  // polygonMode
					vk::CullModeFlagBits::eNone,  // cullMode
					vk::FrontFace::eCounterClockwise,  // frontFace
					VK_FALSE,  // depthBiasEnable
					0.f,  // depthBiasConstantFactor
					0.f,  // depthBiasClamp
					0.f,  // depthBiasSlopeFactor
					1.f   // lineWidth
				},

				// multisampling
				&(const vk::PipelineMultisampleStateCreateInfo&)vk::PipelineMultisampleStateCreateInfo{  // pMultisampleState
					vk::PipelineMultisampleStateCreateFlags(),  // flags
					vk::SampleCountFlagBits::e1,  // rasterizationSamples
					VK_FALSE,  // sampleShadingEnable
					0.f,       // minSampleShading
					nullptr,   // pSampleMask
					VK_FALSE,  // alphaToCoverageEnable
					VK_FALSE   // alphaToOneEnable
				},

				// depth and stencil
				nullptr,  // pDepthStencilState

				// blending
				&(const vk::PipelineColorBlendStateCreateInfo&)vk::PipelineColorBlendStateCreateInfo{  // pColorBlendState
					vk::PipelineColorBlendStateCreateFlags(),  // flags
					VK_FALSE,  // logicOpEnable
					vk::LogicOp::eClear,  // logicOp
					1,  // attachmentCount
					array{  // pAttachments
						vk::PipelineColorBlendAttachmentState{
							VK_FALSE,  // blendEnable
							vk::BlendFactor::eZero,  // srcColorBlendFactor
							vk::BlendFactor::eZero,  // dstColorBlendFactor
							vk::BlendOp::eAdd,       // colorBlendOp
							vk::BlendFactor::eZero,  // srcAlphaBlendFactor
							vk::BlendFactor::eZero,  // dstAlphaBlendFactor
							vk::BlendOp::eAdd,       // alphaBlendOp
							vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
								vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA  // colorWriteMask
						},
					}.data(),
					array<float,4>{0.f,0.f,0.f,0.f}  // blendConstants
				},

				nullptr,  // pDynamicState
				pipelineLayout,  // layout
				renderPass,  // renderPass
				0,  // subpass
				vk::Pipeline(nullptr),  // basePipelineHandle
				-1 // basePipelineIndex
			)
		).value;
}


void App::frame()
{
	// print FPS after each 120 frames
	if(frameID % 120 == 0) {
		static chrono::high_resolution_clock::time_point firstFrameTS;
		if(frameID > 0) {
			auto timeDelta = chrono::duration<double>(chrono::high_resolution_clock::now() - firstFrameTS);
			cout << "FPS: " << setprecision(4) << 120.f/timeDelta.count()
					<< ", total frames rendered: " << frameID << endl;
		}
		firstFrameTS = chrono::high_resolution_clock::now();
	}

	// acquire image
	uint32_t imageIndex;
	vk::Result r =
		device.acquireNextImageKHR(
			swapchain,                        // swapchain
			numeric_limits<uint64_t>::max(),  // timeout
			imageAvailableSemaphore,          // semaphore to signal
			vk::Fence(nullptr),               // fence to signal
			&imageIndex                       // pImageIndex
		);
	if(r != vk::Result::eSuccess) {
		if(r == vk::Result::eSuboptimalKHR) {
			window.scheduleSwapchainResize();
			cout<<"acquireSuboptimal"<<endl;
		} else if(r == vk::Result::eErrorOutOfDateKHR) {
			window.scheduleSwapchainResize();
			cout<<"acquireOutOfDate"<<endl;
			return;
		} else
			throw runtime_error("Vulkan function vkAcquireNextImageKHR failed.");
	}

	// increment frame counter
	frameID++;

	// record command buffer
	commandBuffer.begin(
		vk::CommandBufferBeginInfo(
			vk::CommandBufferUsageFlagBits::eOneTimeSubmit,  // flags
			nullptr  // pInheritanceInfo
		)
	);
	commandBuffer.beginRenderPass(
		vk::RenderPassBeginInfo(
			renderPass,  // renderPass
			framebuffers[imageIndex],  // framebuffer
			vk::Rect2D(vk::Offset2D(0,0), window.surfaceExtent()),  // renderArea
			1,  // clearValueCount
			&(const vk::ClearValue&)vk::ClearValue(  // pClearValues
				vk::ClearColorValue(array<float,4>{0.f,0.f,0.f,1.f})
			)
		),
		vk::SubpassContents::eInline
	);

	// rendering commands
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);  // bind pipeline
	commandBuffer.draw(3,1,0,uint32_t(frameID));  // draw single triangle

	// end render pass and command buffer
	commandBuffer.endRenderPass();
	commandBuffer.end();

	// submit frame
	graphicsQueue.submit(
		vk::ArrayProxy<const vk::SubmitInfo>(
			1,
			&(const vk::SubmitInfo&)vk::SubmitInfo(
				1,                         // waitSemaphoreCount
				&imageAvailableSemaphore,  // pWaitSemaphores
				&(const vk::PipelineStageFlags&)vk::PipelineStageFlags(  // pWaitDstStageMask
					vk::PipelineStageFlagBits::eColorAttachmentOutput),
				1,                         // commandBufferCount
				&commandBuffer,            // pCommandBuffers
				1,                         // signalSemaphoreCount
				&renderFinishedSemaphore   // pSignalSemaphores
			)
		),
		vk::Fence(nullptr)
	);

	// present
	r =
		presentationQueue.presentKHR(
			&(const vk::PresentInfoKHR&)vk::PresentInfoKHR(
				1, &renderFinishedSemaphore,  // waitSemaphoreCount+pWaitSemaphores
				1, &swapchain, &imageIndex,  // swapchainCount+pSwapchains+pImageIndices
				nullptr  // pResults
			)
		);
	if(r != vk::Result::eSuccess) {
		if(r == vk::Result::eSuboptimalKHR) {
			window.scheduleSwapchainResize();
			cout<<"presentSuboptimal"<<endl;
		} else if(r == vk::Result::eErrorOutOfDateKHR) {
			window.scheduleSwapchainResize();
			cout<<"presentOutOfDate"<<endl;
			return;
		} else
			throw runtime_error("Vulkan function vkQueuePresentKHR failed.");
	}

	// wait for completion
	presentationQueue.waitIdle();
	if(frameUpdateMode != FrameUpdateMode::OnDemand)
		window.scheduleNextFrame();
}


/// main function of the application
int main(int argc, char** argv)
{
	// catch exceptions
	// (vulkan.hpp fuctions throw if they fail)
	try {

		App app(argc, argv);
		app.init();
		VulkanWindow w;
		vk::SurfaceKHR s = w.init(app.instance, {1024, 768}, appName);
		app.window.setRecreateSwapchainCallback(
			bind(
				&App::recreateSwapchain,
				&app,
				placeholders::_1,
				placeholders::_2
			)
		);
		app.window.setFrameCallback(
			bind(&App::frame, &app),
			app.physicalDevice,
			app.device,
			app.surface
		);
		VulkanWindow::mainLoop();

	// catch exceptions
	} catch(vk::Error &e) {
		cout<<"Failed because of Vulkan exception: "<<e.what()<<endl;
	} catch(exception &e) {
		cout<<"Failed because of exception: "<<e.what()<<endl;
	} catch(...) {
		cout<<"Failed because of unspecified exception."<<endl;
	}

	return 0;
}
