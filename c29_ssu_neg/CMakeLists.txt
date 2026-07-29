# SPDX-License-Identifier: MIT
#
# Negative SSU isolation smoke — attempts privileged MMIO and expects kill.
# Full Gate D HIL scripts land with SSUMODE2 packaging.
ulmk_component_register(
    NAME         c29_ssu_neg
    ENABLED      OFF
    SOURCES
        src/root_thread.c
    ROOT_THREAD
)
