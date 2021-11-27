macro(GuiConfigure APP_SOURCES APP_INCLUDES deps defines)

	# detect recommended GUI type
	if(WIN32)
		set(guiTypeDetected "Win32")
	elseif(UNIX)
		find_package(Wayland)
		if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)
			set(guiTypeDetected "Wayland")
		else()
			find_package(X11)
			if(X11_FOUND)
				set(guiTypeDetected "Xlib")
			endif()
		endif()
	endif()

	# set GUI_TYPE if not already set or if set to "default" string
	string(TOLOWER "${GUI_TYPE}" guiTypeLowerCased)
	if(NOT GUI_TYPE OR "${guiTypeLowerCased}" STREQUAL "default")
		set(GUI_TYPE ${guiTypeDetected} CACHE STRING "Gui type. Accepted values: default, Win32, Xlib, or Wayland." FORCE)
	endif()

	# give error on invalid GUI_TYPE
	set(guiList "Win32" "Xlib" "Wayland")
	if(NOT "${GUI_TYPE}" IN_LIST guiList)
		message(FATAL_ERROR "GUI_TYPE value is invalid. It must be set to default, Win32, Xlib, or Wayland.")
	endif()


	if("${GUI_TYPE}" STREQUAL "Win32")

		# configure for Win32
		set(${defines} ${${defines}} VK_USE_PLATFORM_WIN32_KHR)

	elseif("${GUI_TYPE}" STREQUAL "Xlib")

		# configure for Xlib
		find_package(X11 REQUIRED)
		set(${deps} ${${deps}} X11)
		set(${defines} ${${defines}} VK_USE_PLATFORM_XLIB_KHR)

	elseif("${GUI_TYPE}" STREQUAL "Wayland")

		# configure for Wayland
		find_package(Wayland REQUIRED)
		if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)

			add_custom_command(OUTPUT xdg-shell-client-protocol.h
			                   COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h)
			add_custom_command(OUTPUT xdg-shell-protocol.c
			                   COMMAND ${Wayland_SCANNER} private-code  ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c)
			add_custom_command(OUTPUT xdg-decoration-client-protocol.h
			                   COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h)
			add_custom_command(OUTPUT xdg-decoration-protocol.c
			                   COMMAND ${Wayland_SCANNER} private-code  ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c)

			list(APPEND ${APP_SOURCES}  xdg-shell-protocol.c        xdg-decoration-protocol.c)
			list(APPEND ${APP_INCLUDES} xdg-shell-client-protocol.h xdg-decoration-client-protocol.h)
			set(${deps} ${${deps}} Wayland::client -lrt)
			set(${defines} ${${defines}} VK_USE_PLATFORM_WAYLAND_KHR)

		else()
			message(FATAL_ERROR "Not all Wayland variables were detected properly.")
		endif()

	endif()

endmacro()
