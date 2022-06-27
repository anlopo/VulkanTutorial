#include <vulkan/vulkan.hpp>
#include <iostream>

using namespace std;


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
						"05-commandSubmission",  // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					0, nullptr,  // no extensions
				});

		// find compatible devices
		// (the device must have a queue supporting graphics operations)
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<tuple<vk::PhysicalDevice, uint32_t>> compatibleDevices;
		for(vk::PhysicalDevice pd : deviceList) {

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
		cout << "Submiting work..." << endl;
		graphicsQueue.submit(
			vk::SubmitInfo(  // submits
				0, nullptr, nullptr,       // waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask
				1, &commandBuffer.get(),   // commandBufferCount, pCommandBuffers
				0, nullptr                 // signalSemaphoreCount, pSignalSemaphores
			),
			renderingFinishedFence.get()  // fence
		);

		// wait for the work
		cout << "Waiting for the work..." << endl;
		vk::Result r = device->waitForFences(
			renderingFinishedFence.get(),  // fences (vk::ArrayProxy)
			VK_TRUE,       // waitAll
			uint64_t(3e9)  // timeout (3s)
		);
		if(r == vk::Result::eTimeout)
			throw std::runtime_error("GPU timeout. Task is probably hanging.");

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
