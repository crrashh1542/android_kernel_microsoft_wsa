// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 Google LLC. */

#include "mali_kbase_runtime_pm.h"

void voltage_range_check(struct kbase_device *kbdev, unsigned long *volts)
{
	struct mtk_platform_context *ctx = kbdev->platform_context;
	const struct mtk_hw_config *cfg = ctx->config;

	if (volts[1] < volts[0] + cfg->bias_min_microvolt ||
	    volts[1] > volts[0] + cfg->bias_max_microvolt)
		volts[1] = volts[0] + cfg->bias_min_microvolt;
	volts[1] = clamp_t(unsigned long, volts[1],
			   cfg->vsram_gpu_min_microvolt,
			   cfg->vsram_gpu_max_microvolt);
}

void kbase_pm_domain_term(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kbdev->pm_domain_devs); i++)
		if (kbdev->pm_domain_devs[i])
			dev_pm_domain_detach(kbdev->pm_domain_devs[i], true);
}

int kbase_pm_runtime_callback_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_pm_runtime_callback_term(struct kbase_device *kbdev)
{
}

int kbase_pm_runtime_callback_on(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_pm_runtime_callback_off(struct kbase_device *kbdev)
{
}

void platform_term(struct kbase_device *kbdev)
{
	kbdev->platform_context = NULL;
	kbase_pm_domain_term(kbdev);
}
