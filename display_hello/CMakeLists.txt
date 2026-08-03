ulmk_component_register(
	NAME         display_hello
	ENABLED      OFF
	SOURCES
		src/root_thread.c
	REQUIRES
		ulmk_device_manager
		ulmk_device_classes
	ROOT_THREAD
)
