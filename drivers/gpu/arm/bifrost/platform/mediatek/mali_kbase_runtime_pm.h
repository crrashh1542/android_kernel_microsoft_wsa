/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 Google LLC. */

#ifndef _MALI_KBASE_RUNTIME_PM_H_
#define _MALI_KBASE_RUNTIME_PM_H_

#include <linux/pm_domain.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>

/**
 * mtk_hw_config - config of the hardware specific constants
 * @num_pm_domains: number of GPU power domains
 * @vgpu_min_microvolt: Minimal required voltage for vgpu.
 * @vgpu_max_microvolt: Maximal acceptable voltage for vgpu.
 * @vsram_gpu_min_microvolt: Minimal required voltage for vsram-gpu.
 * @vsram_gpu_max_microvolt: Maximal acceptable voltage for vsram-gpu.
 * @bias_min_microvolt: Minimal required voltage bias between vgpu and vsram-gpu.
 * @bias_max_microvolt: Maximal acceptable voltage bias between vgpu and vsram-gpu.
 *		        @bias_min_microvolt <= vsram - vgpu <= @bias_max_microvolt
 * @supply_tolerance_microvolt: The voltage diff tolerance.
 */
struct mtk_hw_config {
	/* Power domain */
	int num_pm_domains;

	/* Voltage configuration for PMIC regulators */
	unsigned long vgpu_min_microvolt;
	unsigned long vgpu_max_microvolt;
	unsigned long vsram_gpu_min_microvolt;
	unsigned long vsram_gpu_max_microvolt;
	unsigned long bias_min_microvolt;
	unsigned long bias_max_microvolt;
	unsigned long supply_tolerance_microvolt;
};

/**
 * mtk_platform_context - MediaTek platform context
 * @clks: GPU clocks
 * @num_clks: number of GPU clocks
 * @g_mfg_base: MFG base address
 * @is_powered: GPU on/off status
 * @config: pointer to the hardware config struct
 *
 * This holds general platform information e.g. data probed from device tree,
 * predefined hardware config etc.
 */
struct mtk_platform_context {
	struct clk_bulk_data *clks;
	int num_clks;
	void __iomem *g_mfg_base;
	bool is_powered;

	const struct mtk_hw_config *config;
};

void voltage_range_check(struct kbase_device *kbdev, unsigned long *volts);

void kbase_pm_domain_term(struct kbase_device *kbdev);
int kbase_pm_runtime_callback_init(struct kbase_device *kbdev);
void kbase_pm_runtime_callback_term(struct kbase_device *kbdev);
int kbase_pm_runtime_callback_on(struct kbase_device *kbdev);
void kbase_pm_runtime_callback_off(struct kbase_device *kbdev);

void platform_term(struct kbase_device *kbdev);
#endif /* _MALI_KBASE_RUNTIME_PM_H_ */
