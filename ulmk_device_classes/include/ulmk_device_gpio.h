/* SPDX-License-Identifier: MIT */
#ifndef ULMK_DEVICE_GPIO_H
#define ULMK_DEVICE_GPIO_H

#include <stdint.h>
#include <ulmk_device.h>

#define ULMK_DEV_CLASS_GPIO		6u

#define ULMK_GPIO_DIR_IN		0u
#define ULMK_GPIO_DIR_OUT		1u
#define ULMK_GPIO_PULL_NONE		0u
#define ULMK_GPIO_PULL_UP		1u
#define ULMK_GPIO_PULL_DOWN		2u

#define ULMK_GPIO_IOCTL_CONFIG		(ULMK_DEV_REQ_IOCTL + 1u)
#define ULMK_GPIO_IOCTL_GET		(ULMK_DEV_REQ_IOCTL + 2u)

struct ulmk_gpio_pin_val {
	uint16_t pin;
	int16_t  value;
};

static inline int ulmk_gpio_config(ulmk_dev_t *dev, uint16_t pin,
				   uint32_t dir, uint32_t pull)
{
	uint32_t args[3];

	args[0] = pin;
	args[1] = dir;
	args[2] = pull;
	return ulmk_ioctl(dev, ULMK_GPIO_IOCTL_CONFIG, args, 3u);
}

static inline int ulmk_gpio_set(ulmk_dev_t *dev, uint16_t pin, int value)
{
	struct ulmk_gpio_pin_val pv;

	pv.pin = pin;
	pv.value = (int16_t)value;
	return ulmk_write(dev, &pv, sizeof(pv));
}

static inline int ulmk_gpio_get(ulmk_dev_t *dev, uint16_t pin, int *value)
{
	uint32_t args[2];
	int rc;

	if (!value)
		return ULMK_EINVAL;
	args[0] = pin;
	args[1] = 0u;
	rc = ulmk_ioctl(dev, ULMK_GPIO_IOCTL_GET, args, 2u);
	if (rc != ULMK_OK)
		return rc;
	*value = (int)(int32_t)args[1];
	return ULMK_OK;
}

#endif /* ULMK_DEVICE_GPIO_H */
