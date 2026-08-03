ulmk_component_register(
	NAME         silicon_device_manager
	ENABLED      OFF
	SOURCES
		src/root_thread.c
	REQUIRES
		ulmk_device_manager
		ulmk_device_classes
	ROOT_THREAD
)
