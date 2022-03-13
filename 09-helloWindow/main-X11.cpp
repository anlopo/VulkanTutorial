#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <array>
#include <iostream>

using namespace std;

// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;

// Display and Window handle
static struct UniqueDisplay {
	Display* handle = nullptr;
	~UniqueDisplay()  { if(handle) XCloseDisplay(handle); }
	operator Display*() const  { return handle; }
} display;
struct UniqueWindow {
	Window handle = 0;
	~UniqueWindow()  { if(handle) XDestroyWindow(display, handle); }
	operator Window() const  { return handle; }
} window;


int main(int,char**)
{
	// catch exceptions
	// (vulkan.hpp functions throws if they fail)
	try {

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"09-helloWindow-X11",    // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					2,           // enabled extension count
					array<const char*, 2>{  // enabled extension names
						"VK_KHR_surface",
						"VK_KHR_xlib_surface"
					}.data(),
				});

		// open X connection
		display.handle = XOpenDisplay(nullptr);
		if(display == nullptr)
			throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");

		// create window
		Screen* screen = XDefaultScreenOfDisplay(display);
		XSetWindowAttributes attr;
		attr.event_mask = ExposureMask | StructureNotifyMask | VisibilityChangeMask;
		window.handle =
			XCreateWindow(
				display,  // display
				DefaultRootWindow(display.handle),  // parent
				0, 0,  // x, y
				XWidthOfScreen(screen)/2, XHeightOfScreen(screen)/2,  // width, height
				0,  // border_width
				CopyFromParent,  // depth
				InputOutput,  // class
				CopyFromParent,  // visual
				CWEventMask,  // valuemask
				&attr  // attributes
			);
		XSetStandardProperties(display, window, "Hello window!", "Hello window!", None, NULL, 0, NULL);
		Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
		XSetWMProtocols(display, window, &wmDeleteMessage, 1);
		XMapWindow(display, window);

		// create surface
		vk::UniqueSurfaceKHR surface =
			instance->createXlibSurfaceKHRUnique(
				vk::XlibSurfaceCreateInfoKHR(vk::XlibSurfaceCreateFlagsKHR(), display, window)
			);

		// get VisualID
		XWindowAttributes a;
		XGetWindowAttributes(display, window, &a);
		VisualID v = XVisualIDFromVisual(a.visual);

		// find compatible devices
		// (On Windows, all graphics adapters capable of monitor output are usually compatible devices.
		// On Linux X11 platform, only one graphics adapter is compatible device (the one that
		// renders the window).
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<string> compatibleDevices;
		for(vk::PhysicalDevice pd : deviceList) {
			uint32_t c;
			pd.getQueueFamilyProperties(&c, nullptr, vk::DispatchLoaderStatic());
			for(uint32_t i=0; i<c; i++)
				if(pd.getXlibPresentationSupportKHR(i, display, v)) {
					compatibleDevices.push_back(pd.getProperties().deviceName);
					break;
				}
		}
		cout << "Compatible devices:" << endl;
		for(string& name : compatibleDevices)
			cout << "   " << name << endl;

		// run event loop
		while(true) {
			XEvent e;
			XNextEvent(display,&e);
			if(e.type==ClientMessage&&ulong(e.xclient.data.l[0])==wmDeleteMessage)
				break;
		}

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
