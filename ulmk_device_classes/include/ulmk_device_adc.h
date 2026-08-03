/* SPDX-License-Identifier: MIT */
#ifndef ULMK_DEVICE_ADC_H
#define ULMK_DEVICE_ADC_H

#include <stdint.h>
#include <ulmk_device.h>

#define ULMK_DEV_CLASS_ADC		5u

#define ULMK_ADC_IOCTL_CONFIG		(ULMK_DEV_REQ_IOCTL + 1u)
#define ULMK_ADC_IOCTL_SELECT		(ULMK_DEV_REQ_IOCTL + 2u)

static inline int ulmk_adc_config(ulmk_dev_t *dev, uint32_t ch)
{
	uint32_t args[1];

	args[0] = ch;
	return ulmk_ioctl(dev, ULMK_ADC_IOCTL_CONFIG, args, 1u);
}

static inline int ulmk_adc_select(ulmk_dev_t *dev, uint32_t ch)
{
	uint32_t args[1];

	args[0] = ch;
	return ulmk_ioctl(dev, ULMK_ADC_IOCTL_SELECT, args, 1u);
}

static inline int ulmk_adc_read(ulmk_dev_t *dev, uint16_t *out)
{
	int n;

	if (!out)
		return ULMK_EINVAL;
	n = ulmk_read(dev, out, sizeof(*out));
	if (n < 0)
		return n;
	if (n != (int)sizeof(*out))
		return ULMK_EINVAL;
	return ULMK_OK;
}

#endif /* ULMK_DEVICE_ADC_H */
