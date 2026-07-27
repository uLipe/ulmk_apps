ulmk_component_register(
    NAME         ping_pong
    ENABLED      OFF
    SOURCES
        src/root_thread.c
        src/ping_pong.c
    INCLUDE_DIRS include
    ROOT_THREAD
)
