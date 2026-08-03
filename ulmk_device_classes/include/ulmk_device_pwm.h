/* SPDX-License-Identifier: MIT */
#ifndef ULMK_DEVICE_PWM_H
#define ULMK_DEVICE_PWM_H

#include <stdint.h>
#include <ulmk_device.h>

#define ULMK_DEV_CLASS_PWM		4u

#define ULMK_PWM_IOCTL_CONFIG		(ULMK_DEV_REQ_IOCTL + 1u)
#define ULMK_PWM_IOCTL_SET_DUTY		(ULMK_DEV_REQ_IOCTL + 2u)
#define ULMK_PWM_IOCTL_ENABLE		(ULMK_DEV_REQ_IOCTL + 3u)

static inline int ulmk_pwm_config(ulmk_dev_t *dev, uint32_t ch,
				  uint32_t freq_hz, uint32_t duty_permille)
{
	uint32_t args[3];

	args[0] = ch;
	args[1] = freq_hz;
	args[2] = duty_permille;
	return ulmk_ioctl(dev, ULMK_PWM_IOCTL_CONFIG, args, 3u);
}

static inline int ulmk_pwm_set_duty(ulmk_dev_t *dev, uint32_t ch,
				    uint32_t duty_permille)
{
	uint32_t args[2];

	args[0] = ch;
	args[1] = duty_permille;
	return ulmk_ioctl(dev, ULMK_PWM_IOCTL_SET_DUTY, args, 2u);
}

static inline int ulmk_pwm_enable(ulmk_dev_t *dev, uint32_t ch, int on)
{
	uint32_t args[2];

	args[0] = ch;
	args[1] = on ? 1u : 0u;
	return ulmk_ioctl(dev, ULMK_PWM_IOCTL_ENABLE, args, 2u);
}

#endif /* ULMK_DEVICE_PWM_H */
