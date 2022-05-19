/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include "mali_kbase_config_platform.h"
#include "mali_kbase_runtime_pm.h"

/* list of clocks required by GPU */
static const char * const mt8183_gpu_clks[] = {
	"clk_mux",
	"clk_main_parent",
	"clk_sub_parent",
	"subsys_mfg_cg",
};

const struct mtk_hw_config mt8183_hw_config = {
	.num_pm_domains = 3,
	.num_clks = ARRAY_SIZE(mt8183_gpu_clks),
	.clk_names = mt8183_gpu_clks,
	.mfg_compatible_name = "mediatek,mt8183-mfgcfg",
	.reg_mfg_timestamp = 0x130,
	.top_tsvalueb_en = 0x3,
	.vgpu_min_microvolt = 625000,
	.vgpu_max_microvolt = 825000,
	.vsram_gpu_min_microvolt = 850000,
	.vsram_gpu_max_microvolt = 925000,
	.bias_min_microvolt = 100000,
	.bias_max_microvolt = 250000,
	.supply_tolerance_microvolt = 125,
	.gpu_freq_min_khz = 300000,
	.gpu_freq_max_khz = 800000,
	.auto_suspend_delay_ms = 50,
};

struct mtk_platform_context mt8183_platform_context = {
	.config = &mt8183_hw_config,
};

struct kbase_pm_callback_conf mt8183_pm_callbacks = {
	.power_on_callback = kbase_pm_callback_power_on,
	.power_off_callback = kbase_pm_callback_power_off,
	.power_suspend_callback = kbase_pm_callback_suspend,
	.power_resume_callback = kbase_pm_callback_resume,
#ifdef KBASE_PM_RUNTIME
	.power_runtime_init_callback = kbase_pm_runtime_callback_init,
	.power_runtime_term_callback = kbase_pm_runtime_callback_term,
	.power_runtime_on_callback = kbase_pm_runtime_callback_on,
	.power_runtime_off_callback = kbase_pm_runtime_callback_off,
#else				/* KBASE_PM_RUNTIME */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,
#endif				/* KBASE_PM_RUNTIME */
};

#ifdef CONFIG_MALI_VALHALL_DEVFREQ
#ifdef CONFIG_REGULATOR
static bool get_step_volt(unsigned long *step_volt, unsigned long *target_volt,
			  int nr_regulators, bool inc)
{
	int i;
	/*
	 * TODO: Get these bias voltages as parameters in a separate patch.
	 * For now we just directly reference the values from the global HW
	 * configs to avoid unnecessary changes.
	 */
	unsigned long bias_min_microvolt = mt8183_hw_config.bias_min_microvolt;
	unsigned long bias_max_microvolt = mt8183_hw_config.bias_max_microvolt;

	for (i = 0; i < nr_regulators; i++)
		if (step_volt[i] != target_volt[i])
			break;

	if (i == nr_regulators)
		return false;

	/* Do one round of *caterpillar move* - shrink the tail as much to the
	 * head as possible, and then step ahead as far as possible.
	 * Depending on the direction of voltage transition, a reversed
	 * sequence of extend-and-shrink may apply, which leads to the same
	 * result in the end.
	 */
	if (inc) {
		step_volt[0] = min(target_volt[0],
				   step_volt[1] - bias_min_microvolt);
		step_volt[1] = min(target_volt[1],
				   step_volt[0] + bias_max_microvolt);
	} else {
		step_volt[0] = max(target_volt[0],
				   step_volt[1] - bias_max_microvolt);
		step_volt[1] = max(target_volt[1],
				   step_volt[0] + bias_min_microvolt);
	}
	return true;
}

static int set_voltages(struct kbase_device *kbdev, unsigned long *voltages,
			bool inc)
{
	struct mtk_platform_context *ctx = kbdev->platform_context;
	const struct mtk_hw_config *cfg = ctx->config;
	unsigned long step_volt[BASE_MAX_NR_CLOCKS_REGULATORS];
	const unsigned long reg_min_volt[BASE_MAX_NR_CLOCKS_REGULATORS] = {
		cfg->vgpu_min_microvolt,
		cfg->vsram_gpu_min_microvolt,
	};
	const unsigned long reg_max_volt[BASE_MAX_NR_CLOCKS_REGULATORS] = {
		cfg->vgpu_max_microvolt,
		cfg->vsram_gpu_max_microvolt,
	};
	int i, err;

	/* Nothing to do if the direction of voltage transition is incorrect. */
	if ((inc && kbdev->current_voltages[0] > voltages[0]) ||
	    (!inc && kbdev->current_voltages[0] < voltages[0]))
		return 0;

	for (i = 0; i < kbdev->nr_regulators; ++i)
		step_volt[i] = kbdev->current_voltages[i];

	while (get_step_volt(step_volt, voltages, kbdev->nr_regulators, inc)) {
		for (i = 0; i < kbdev->nr_regulators; i++) {
			if (kbdev->current_voltages[i] == step_volt[i])
				continue;

			/* Assuming valid max voltages are always positive. */
			if (reg_max_volt[i] > 0 &&
			    (step_volt[i] < reg_min_volt[i] ||
			     step_volt[i] > reg_max_volt[i])) {
				dev_warn(kbdev->dev, "Clamp invalid voltage: "
					 "%lu of regulator %d into [%lu, %lu]",
					 step_volt[i], i,
					 reg_min_volt[i],
					 reg_max_volt[i]);

				step_volt[i] = clamp_val(step_volt[i],
							 reg_min_volt[i],
							 reg_max_volt[i]);
			}

			err = regulator_set_voltage(kbdev->regulators[i],
						    step_volt[i],
						    step_volt[i] + cfg->supply_tolerance_microvolt);

			if (err) {
				dev_err(kbdev->dev,
					"Failed to set reg %d voltage err:(%d)\n",
					i, err);
				return err;
			}
			kbdev->current_voltages[i] = step_volt[i];
		}
	}

	return 0;
}
#endif

#endif

static int platform_init(struct kbase_device *kbdev)
{
	int err;
	struct mtk_platform_context *ctx = &mt8183_platform_context;
	const struct mtk_hw_config *cfg = ctx->config;

	kbdev->platform_context = ctx;

	if (WARN_ON(cfg->num_pm_domains <= 0))
		return -EINVAL;
	kbdev->num_pm_domains = cfg->num_pm_domains;

	err = kbase_pm_domain_init(kbdev);
	if (err)
		return err;

	err = mtk_mfgsys_init(kbdev);
	if (err)
		return err;

	err = mtk_set_frequency(kbdev, cfg->gpu_freq_max_khz * 1000);
	if (err)
		return err;

#ifdef CONFIG_MALI_VALHALL_DEVFREQ
	kbdev->devfreq_ops.set_frequency = mtk_set_frequency;
#ifdef CONFIG_REGULATOR
	kbdev->devfreq_ops.set_voltages = set_voltages;
#endif
	kbdev->devfreq_ops.voltage_range_check = mtk_voltage_range_check;
#endif

	return 0;
}

struct kbase_platform_funcs_conf mt8183_platform_funcs = {
	.platform_init_func = platform_init,
	.platform_term_func = platform_term
};
