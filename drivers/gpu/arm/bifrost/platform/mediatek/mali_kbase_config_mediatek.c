// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Mediatek Inc.

#include <mali_kbase_config.h>

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
