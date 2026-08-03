# ulmk_device_classes — portable device class contracts (policy).
#
# Lives in ulmk_apps, not in the kernel tree.  The kernel component
# ulmk_device_manager only provides the generic open/read/write/ioctl
# mechanism; class opcodes and helpers (display, input, can, …) are here.
cmake_minimum_required(VERSION 3.20)

ulmk_component_register(
	NAME         ulmk_device_classes
	ENABLED      OFF
	SOURCES
		src/stub.c
	INCLUDE_DIRS include
	REQUIRES
		ulmk_device_manager
)
