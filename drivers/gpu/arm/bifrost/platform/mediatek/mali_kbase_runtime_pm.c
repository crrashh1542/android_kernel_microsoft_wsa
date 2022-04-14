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

int map_mfg_base(struct mtk_platform_context *ctx)
{
	struct device_node *node;
	const struct mtk_hw_config *cfg = ctx->config;


	WARN_ON(cfg->mfg_compatible_name == NULL);
	node = of_find_compatible_node(NULL, NULL, cfg->mfg_compatible_name);
	if (!node)
		return -ENODEV;

	ctx->mfg_base_addr = of_iomap(node, 0);
	of_node_put(node);
	if (!ctx->mfg_base_addr)
		return -ENOMEM;

	return 0;
}

void unmap_mfg_base(struct mtk_platform_context *ctx)
{
	iounmap(ctx->mfg_base_addr);
}

void enable_timestamp_register(struct kbase_device *kbdev)
{
	struct mtk_platform_context *ctx = kbdev->platform_context;
	const struct mtk_hw_config *cfg = ctx->config;

	/* Set register MFG_TIMESTAMP to TOP_TSVALEUB_EN */
	writel(cfg->top_tsvalueb_en, ctx->mfg_base_addr + cfg->reg_mfg_timestamp);
}

void check_bus_idle(struct kbase_device *kbdev)
{
	struct mtk_platform_context *ctx = kbdev->platform_context;
	u32 val;

	/* Set register MFG_QCHANNEL_CON bit [1:0] = 0x1 */
	writel(0x1, ctx->mfg_base_addr + REG_MFG_QCHANNEL_CON);

	/* Set register MFG_DEBUG_SEL bit [7:0] = 0x3 */
	writel(0x3, ctx->mfg_base_addr + REG_MFG_DEBUG_SEL);

	/* Poll register MFG_DEBUG_TOP bit 2 = 0x1 */
	/* => 1 for bus idle, 0 for bus non-idle */
	do {
		val = readl(ctx->mfg_base_addr + REG_MFG_DEBUG_TOP);
	} while ((val & BUS_IDLE_BIT) != BUS_IDLE_BIT);
}

int kbase_pm_domain_init(struct kbase_device *kbdev)
{
	int err, i, num_domains;

	num_domains = of_count_phandle_with_args(kbdev->dev->of_node,
						 "power-domains",
						 "#power-domain-cells");

	if (WARN_ON(num_domains != kbdev->num_pm_domains)) {
		dev_err(kbdev->dev,
			"Incompatible power domain counts: %d provided, %d needed\n",
			num_domains, kbdev->num_pm_domains);
		return -EINVAL;
	}

	if (WARN_ON(num_domains > ARRAY_SIZE(kbdev->pm_domain_devs))) {
		dev_err(kbdev->dev,
			"Too many power domains: %d provided\n",
			num_domains);
		return -EINVAL;
	}

	/* Single power domain will be handled by the core. */
	if (num_domains < 2)
		return 0;

	for (i = 0; i < num_domains; i++) {
		kbdev->pm_domain_devs[i] =
			dev_pm_domain_attach_by_id(kbdev->dev, i);
		if (IS_ERR_OR_NULL(kbdev->pm_domain_devs[i])) {
			err = PTR_ERR(kbdev->pm_domain_devs[i]) ? : -ENODATA;
			kbdev->pm_domain_devs[i] = NULL;
			if (err == -EPROBE_DEFER) {
				dev_dbg(kbdev->dev,
					"Probe deferral for pm-domain %d\n",
					i);
			} else {
				dev_err(kbdev->dev,
					"failed to get pm-domain %d: %d\n",
					i, err);
			}
			goto err;
		}
	}

	return 0;
err:
	kbase_pm_domain_term(kbdev);
	return err;
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
	struct mtk_platform_context *ctx = kbdev->platform_context;

	unmap_mfg_base(ctx);
	kbdev->platform_context = NULL;
	kbase_pm_domain_term(kbdev);
}
