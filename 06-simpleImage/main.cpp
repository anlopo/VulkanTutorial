#include <vulkan/vulkan.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>

using namespace std;

// constants
static const vk::Extent2D imageExtent(128, 128);


// Vulkan instance
// (it must be destructed as the last one)
static vk::UniqueInstance instance;

// Vulkan handles and objects
// (they need to be placed in particular (not arbitrary) order
// because they are destructed from the last one to the first one)
static vk::PhysicalDevice physicalDevice;
static uint32_t graphicsQueueFamily;
static vk::UniqueDevice device;
static vk::Queue graphicsQueue;
static vk::UniqueRenderPass renderPass;
static vk::UniqueImage framebufferImage;
static vk::UniqueDeviceMemory framebufferImageMemory;
static vk::UniqueImageView frameImageView;
static vk::UniqueFramebuffer framebuffer;
static vk::UniqueCommandPool commandPool;
static vk::UniqueCommandBuffer commandBuffer;
static vk::UniqueFence renderingFinishedFence;


/// main function of the application
int main(int, char**)
{
	// catch exceptions
	// (vulkan.hpp functions throw if they fail)
	try {

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"06-simpleImage",        // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					0, nullptr,  // no extensions
				});

		// find compatible devices
		// (the device must have a queue supporting graphics operations and support for linear tiling)
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<tuple<vk::PhysicalDevice, uint32_t>> compatibleDevices;
		for(vk::PhysicalDevice pd : deviceList) {

		#if 0
			// check linear tiling support
			vk::FormatProperties fp = pd.getFormatProperties(vk::Format::eR8G8B8A8Unorm);
			if(!(fp.linearTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachment))
				continue;
		#endif

			// select queue for graphics rendering
			vector<vk::QueueFamilyProperties> queueFamilyList = pd.getQueueFamilyProperties();
			for(uint32_t i=0, c=uint32_t(queueFamilyList.size()); i<c; i++) {
				if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics) {
					compatibleDevices.emplace_back(pd, i);
					break;
				}
			}
		}

		// print devices
		cout << "Vulkan devices:" << endl;
		for(vk::PhysicalDevice pd : deviceList)
			cout << "   " << pd.getProperties().deviceName << endl;
		cout << "Compatible devices:" << endl;
		for(auto& t : compatibleDevices)
			cout << "   " << get<0>(t).getProperties().deviceName << endl;

		// choose device
		if(compatibleDevices.empty())
			throw runtime_error("No compatible devices.");
		physicalDevice = get<0>(compatibleDevices.front());
		graphicsQueueFamily = get<1>(compatibleDevices.front());
		cout << "Using device:\n"
		        "   " << physicalDevice.getProperties().deviceName << endl;

		// create device
		device =
			physicalDevice.createDeviceUnique(
				vk::DeviceCreateInfo{
					vk::DeviceCreateFlags(),  // flags
					1,                        // queueCreateInfoCount
					array{                    // pQueueCreateInfos
						vk::DeviceQueueCreateInfo{
							vk::DeviceQueueCreateFlags(),  // flags
							graphicsQueueFamily,  // queueFamilyIndex
							1,                    // queueCount
							&(const float&)1.f,   // pQueuePriorities
						},
					}.data(),
					0, nullptr,  // no layers
					0, nullptr,  // number of enabled extensions, enabled extension names
					nullptr,     // enabled features
				}
			);

		// get queues
		graphicsQueue = device->getQueue(graphicsQueueFamily, 0);


		// render pass
		renderPass =
			device->createRenderPassUnique(
				vk::RenderPassCreateInfo(
					vk::RenderPassCreateFlags(),  // flags
					1,                            // attachmentCount
					array{  // pAttachments
						vk::AttachmentDescription(
							vk::AttachmentDescriptionFlags(),  // flags
							vk::Format::eR8G8B8A8Unorm,        // format
							vk::SampleCountFlagBits::e1,       // samples
							vk::AttachmentLoadOp::eClear,      // loadOp
							vk::AttachmentStoreOp::eStore,     // storeOp
							vk::AttachmentLoadOp::eDontCare,   // stencilLoadOp
							vk::AttachmentStoreOp::eDontCare,  // stencilStoreOp
							vk::ImageLayout::eUndefined,       // initialLayout
							vk::ImageLayout::eGeneral          // finalLayout
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
							array{    // pColorAttachments
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
					0,  // dependencyCount
					nullptr  // pDependencies
				)
			);

		// images
		framebufferImage =
			device->createImageUnique(
				vk::ImageCreateInfo(
					vk::ImageCreateFlags(),       // flags
					vk::ImageType::e2D,           // imageType
					vk::Format::eR8G8B8A8Unorm,   // format
					vk::Extent3D(imageExtent.width, imageExtent.height, 1),  // extent
					1,                            // mipLevels
					1,                            // arrayLayers
					vk::SampleCountFlagBits::e1,  // samples
					vk::ImageTiling::eLinear,     // tiling
					vk::ImageUsageFlagBits::eColorAttachment,  // usage
					vk::SharingMode::eExclusive,  // sharingMode
					0,                            // queueFamilyIndexCount
					nullptr,                      // pQueueFamilyIndices
					vk::ImageLayout::eUndefined   // initialLayout
				)
			);

		// memory for images
		auto allocateMemory =
			[](vk::Image image, vk::MemoryPropertyFlags requiredFlags) -> vk::UniqueDeviceMemory{
				vk::MemoryRequirements memoryRequirements = device->getImageMemoryRequirements(image);
				vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
				for(uint32_t i=0; i<memoryProperties.memoryTypeCount; i++)
					if(memoryRequirements.memoryTypeBits & (1<<i))
						if((memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
							return
								device->allocateMemoryUnique(
									vk::MemoryAllocateInfo(
										memoryRequirements.size,  // allocationSize
										i                         // memoryTypeIndex
									)
								);
				throw std::runtime_error("No suitable memory type found for image.");
			};
		framebufferImageMemory = allocateMemory(framebufferImage.get(), vk::MemoryPropertyFlagBits::eHostVisible);
		device->bindImageMemory(
			framebufferImage.get(),        // image
			framebufferImageMemory.get(),  // memory
			0                              // memoryOffset
		);

		// image view
		frameImageView =
			device->createImageViewUnique(
				vk::ImageViewCreateInfo(
					vk::ImageViewCreateFlags(),  // flags
					framebufferImage.get(),      // image
					vk::ImageViewType::e2D,      // viewType
					vk::Format::eR8G8B8A8Unorm,  // format
					vk::ComponentMapping(),      // components
					vk::ImageSubresourceRange(   // subresourceRange
						vk::ImageAspectFlagBits::eColor,  // aspectMask
						0,  // baseMipLevel
						1,  // levelCount
						0,  // baseArrayLayer
						1   // layerCount
					)
				)
			);

		// framebuffers
		framebuffer =
			device->createFramebufferUnique(
				vk::FramebufferCreateInfo(
					vk::FramebufferCreateFlags(),  // flags
					renderPass.get(),              // renderPass
					1,&frameImageView.get(),       // attachmentCount, pAttachments
					imageExtent.width,             // width
					imageExtent.height,            // height
					1  // layers
				)
			);


		// command pool
		commandPool =
			device->createCommandPoolUnique(
				vk::CommandPoolCreateInfo(
					vk::CommandPoolCreateFlags(),  // flags
					graphicsQueueFamily            // queueFamilyIndex
				)
			);

		// allocate command buffer
		commandBuffer = std::move(
			device->allocateCommandBuffersUnique(
				vk::CommandBufferAllocateInfo(
					commandPool.get(),                 // commandPool
					vk::CommandBufferLevel::ePrimary,  // level
					1                                  // commandBufferCount
				)
			)[0]);

		// begin command buffer
		commandBuffer->begin(
			vk::CommandBufferBeginInfo(
				vk::CommandBufferUsageFlagBits::eOneTimeSubmit,  // flags
				nullptr  // pInheritanceInfo
			)
		);

		// begin render pass
		commandBuffer->beginRenderPass(
			vk::RenderPassBeginInfo(
				renderPass.get(),   // renderPass
				framebuffer.get(),  // framebuffer
				vk::Rect2D(vk::Offset2D(0,0), imageExtent),  // renderArea
				1,      // clearValueCount
				array{  // pClearValues
					vk::ClearValue(array<float,4>{0.f,1.f,0.f,1.f}),
				}.data()
			),
			vk::SubpassContents::eInline
		);

		// end render pass
		commandBuffer->endRenderPass();

		// end command buffer
		commandBuffer->end();


		// fence
		renderingFinishedFence =
			device->createFenceUnique(
				vk::FenceCreateInfo{
					vk::FenceCreateFlags()  // flags
				}
			);

		// submit work
		graphicsQueue.submit(
			vk::SubmitInfo(  // submits
				0, nullptr, nullptr,       // waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask
				1, &commandBuffer.get(),   // commandBufferCount, pCommandBuffers
				0, nullptr                 // signalSemaphoreCount, pSignalSemaphores
			),
			renderingFinishedFence.get()  // fence
		);

		// wait for the work
		vk::Result r = device->waitForFences(
			renderingFinishedFence.get(),  // fences (vk::ArrayProxy)
			VK_TRUE,       // waitAll
			uint64_t(3e9)  // timeout (3s)
		);
		if(r == vk::Result::eTimeout)
			throw std::runtime_error("GPU timeout. Task is probably hanging.");


		// map memory
		struct MappedMemoryDeleter { void operator()(void*) { device->unmapMemory(framebufferImageMemory.get()); } } mappedMemoryDeleter;
		unique_ptr<void, MappedMemoryDeleter> m(
			device->mapMemory(framebufferImageMemory.get(), 0, VK_WHOLE_SIZE, vk::MemoryMapFlags()),  // pointer
			mappedMemoryDeleter  // deleter
		);

		// invalidate caches to fetch a new content
		// (this is required as we might be using non-coherent memory, see vk::MemoryPropertyFlagBits::eHostCoherent)
		device->invalidateMappedMemoryRanges(
			vk::MappedMemoryRange(
				framebufferImageMemory.get(),  // memory
				0,  // offset
				VK_WHOLE_SIZE  // size
			)
		);

		// get image memory layout
		vk::SubresourceLayout framebufferImageLayout=
			device->getImageSubresourceLayout(
				framebufferImage.get(),  // image
				vk::ImageSubresource{    // subresource
					vk::ImageAspectFlagBits::eColor,  // aspectMask
					0,  // mipLevel
					0   // arrayLayer
				}
			);


		// open the output file
		cout << "Writing \"image.bmp\"..." << endl;
		fstream s("image.bmp", fstream::out | fstream::binary);

		// write BitmapFileHeader
		struct BitmapFileHeader {
			uint16_t type = 0x4d42;
			uint16_t sizeLo;
			uint16_t sizeHi;
			uint16_t reserved1 = 0;
			uint16_t reserved2 = 0;
			uint16_t offsetLo;
			uint16_t offsetHi;
		};
		static_assert(sizeof(BitmapFileHeader)==14, "Wrong alignment of BitmapFileHeader members.");

		uint32_t imageDataSize = imageExtent.width*imageExtent.height*4;
		uint32_t fileSize = imageDataSize+14+40+2;
		BitmapFileHeader bitmapFileHeader = {
			0x4d42,
			uint16_t(fileSize&0xffff),
			uint16_t(fileSize>>16),
			0, 0,
			14+40+2,  // equal to sum of sizeof(BitmapFileHeader), sizeof(BitmapInfoHeader) and 2 (as alignment)
			0
		};
		s.write(reinterpret_cast<char*>(&bitmapFileHeader), sizeof(BitmapFileHeader));

		// write BitmapInfoHeader
		struct BitmapInfoHeader {
			uint32_t size = 40;
			int32_t  width;
			int32_t  height;
			uint16_t numPlanes = 1;
			uint16_t bpp = 32;
			uint32_t compression = 0;  // 0 - no compression
			uint32_t imageDataSize;
			int32_t  xPixelsPerMeter;
			int32_t  yPixelsPerMeter;
			uint32_t numColorsInPalette = 0;  // no colors in color palette
			uint32_t numImportantColors = 0;  // all colors are important
		};
		static_assert(sizeof(BitmapInfoHeader)==40, "Wrong size of BitmapInfoHeader.");

		BitmapInfoHeader bitmapInfoHeader = {
			40,
			int32_t(imageExtent.width),
			-int32_t(imageExtent.height),
			1, 32, 0,
			imageDataSize,
			2835, 2835,  // roughly 72 DPI
			0, 0
		};
		s.write(reinterpret_cast<char*>(&bitmapInfoHeader), sizeof(BitmapInfoHeader));
		s.write(array<char,2>{'\0','\0'}.data(), 2);

		// write image data,
		// line by line
		const char* linePtr = reinterpret_cast<char*>(m.get());
		char b[4];
		for(auto y=imageExtent.height; y>0; y--) {
			for(size_t i=0, e=imageExtent.width*4; i<e; i+=4) {
				b[0] = linePtr[i+2];
				b[1] = linePtr[i+1];
				b[2] = linePtr[i+0];
				b[3] = linePtr[i+3];
				s.write(b, 4);
			}
			linePtr += framebufferImageLayout.rowPitch;
		}
		cout << "Done." << endl;

	// catch exceptions
	} catch(vk::Error& e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	} catch(exception& e) {
		cout << "Failed because of exception: " << e.what() << endl;
	} catch(...) {
		cout << "Failed because of unspecified exception." << endl;
	}

	// wait device idle
	// this is important if there was an exception and device is still busy
	// (device need to be idle before destruction of buffers and other stuff)
	if(device)
		device->waitIdle();

	return 0;
}
