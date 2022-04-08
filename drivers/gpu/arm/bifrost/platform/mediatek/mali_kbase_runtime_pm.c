// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 Google LLC. */

#include "mali_kbase_runtime_pm.h"

void pm_domain_term(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kbdev->pm_domain_devs); i++)
		if (kbdev->pm_domain_devs[i])
			dev_pm_domain_detach(kbdev->pm_domain_devs[i], true);
}

int kbase_device_runtime_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_device_runtime_disable(struct kbase_device *kbdev)
{
}

int pm_callback_runtime_on(struct kbase_device *kbdev)
{
	return 0;
}

void pm_callback_runtime_off(struct kbase_device *kbdev)
{
}

void platform_term(struct kbase_device *kbdev)
{
	kbdev->platform_context = NULL;
	pm_domain_term(kbdev);
}
