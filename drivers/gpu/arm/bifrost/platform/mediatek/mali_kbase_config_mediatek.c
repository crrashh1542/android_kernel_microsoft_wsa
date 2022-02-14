// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Mediatek Inc.

#include <linux/module.h>
#include <linux/of_device.h>

#include <mali_kbase_config.h>

#include "mali_kbase_config_platform.h"

static struct kbase_platform_config dummy_platform_config;

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &dummy_platform_config;
}

#ifndef CONFIG_OF
int kbase_platform_register(void)
{
	return 0;
}

void kbase_platform_unregister(void)
{
}
#endif

#ifdef CONFIG_OF
static const struct kbase_platform_specific_conf mediatek_mt8186_data = {
	.pm_callbacks = &mt8186_pm_callbacks,
	.platform_funcs = &mt8186_platform_funcs,
};

const struct of_device_id kbase_dt_ids[] = {
	{ .compatible = "arm,malit6xx" },
	{ .compatible = "arm,mali-midgard" },
	{ .compatible = "arm,mali-bifrost" },
	{ .compatible = "arm,mali-valhall" },
	{ .compatible = "mediatek,mt8186-mali", .data = &mediatek_mt8186_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, kbase_dt_ids);
#endif
