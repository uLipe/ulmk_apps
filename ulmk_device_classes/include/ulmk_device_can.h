/* SPDX-License-Identifier: MIT */
#ifndef ULMK_DEVICE_CAN_H
#define ULMK_DEVICE_CAN_H

#include <stdint.h>
#include <ulmk_device.h>

#define ULMK_DEV_CLASS_CAN		3u

#define ULMK_CAN_IOCTL_DIAG		(ULMK_DEV_REQ_IOCTL + 1u)

struct ulmk_can_frame {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  data[8];
	uint8_t  _pad[3];
};

static inline int ulmk_can_send(ulmk_dev_t *dev,
				const struct ulmk_can_frame *fr)
{
	if (!fr)
		return ULMK_EINVAL;
	return ulmk_write(dev, fr, sizeof(*fr));
}

static inline int ulmk_can_recv(ulmk_dev_t *dev, struct ulmk_can_frame *fr)
{
	if (!fr)
		return ULMK_EINVAL;
	return ulmk_read(dev, fr, sizeof(*fr));
}

#endif /* ULMK_DEVICE_CAN_H */
