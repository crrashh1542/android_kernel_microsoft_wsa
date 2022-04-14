/*
 * Copyright (C) 2020 MediaTek Inc.
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
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include "mali_kbase_config_platform.h"
#include "mali_kbase_runtime_pm.h"

const struct mtk_hw_config mt8192_hw_config = {
	.num_pm_domains = 5,
	.mfg_compatible_name = "mediatek,mt8192-mfgcfg",
	.reg_mfg_timestamp = 0x130,
	.top_tsvalueb_en = 0x3,
	.vgpu_min_microvolt = 562500,
	.vgpu_max_microvolt = 843750,
	.vsram_gpu_min_microvolt = 750000,
	.vsram_gpu_max_microvolt = 843750,
	.bias_min_microvolt = 0,
	.bias_max_microvolt = 250000,
	.supply_tolerance_microvolt = 125,
	.gpu_freq_min_khz = 358000,
	.gpu_freq_max_khz = 950000,
	.auto_suspend_delay_ms = 50,
};

struct mtk_platform_context mt8192_platform_context = {
	.config = &mt8192_hw_config,
};

enum gpu_clk_idx {mux, main, sub, cg};
/* list of clocks required by GPU */
static const char * const gpu_clocks[] = {
	"clk_mux",
	"clk_main_parent",
	"clk_sub_parent",
	"subsys_mfg_cg",
};

static int kbase_pm_callback_power_on(struct kbase_device *kbdev)
{
	int error, i;
	struct mtk_platform_context *mfg = kbdev->platform_context;

	if (mfg->is_powered) {
		dev_dbg(kbdev->dev, "mali_device is already powered\n");
		return 0;
	}

	for (i = 0; i < kbdev->nr_regulators; i++) {
		error = regulator_enable(kbdev->regulators[i]);
		if (error < 0) {
			dev_err(kbdev->dev,
				"Power on reg %d failed error = %d\n",
				i, error);
			return error;
		}
	}

	for (i = 0; i < kbdev->num_pm_domains; i++) {
		error = pm_runtime_get_sync(kbdev->pm_domain_devs[i]);
		if (error < 0) {
			dev_err(kbdev->dev,
				"Power on core %d failed (err: %d)\n",
				i+1, error);
			return error;
		}
	}

	error = clk_bulk_prepare_enable(mfg->num_clks, mfg->clks);
	if (error < 0) {
		dev_err(kbdev->dev,
			"gpu clock enable failed (err: %d)\n",
			error);
		return error;
	}

	mfg->is_powered = true;

	return 1;
}

static void kbase_pm_callback_power_off(struct kbase_device *kbdev)
{
	int error, i;
	struct mtk_platform_context *mfg = kbdev->platform_context;

	if (!mfg->is_powered) {
		dev_dbg(kbdev->dev, "mali_device is already powered off\n");
		return;
	}

	mfg->is_powered = false;

	check_bus_idle(kbdev);

	clk_bulk_disable_unprepare(mfg->num_clks, mfg->clks);

	for (i = kbdev->num_pm_domains - 1; i >= 0; i--) {
		pm_runtime_mark_last_busy(kbdev->pm_domain_devs[i]);
		error = pm_runtime_put_autosuspend(kbdev->pm_domain_devs[i]);
		if (error < 0)
			dev_err(kbdev->dev,
				"Power off core %d failed (err: %d)\n",
				i+1, error);
	}

	for (i = kbdev->nr_regulators - 1; i >= 0; i--) {
		error = regulator_disable(kbdev->regulators[i]);
		if (error < 0)
			dev_err(kbdev->dev,
				"Power off reg %d failed error = %d\n",
				i, error);
	}
}

static void kbase_pm_callback_resume(struct kbase_device *kbdev)
{
	kbase_pm_callback_power_on(kbdev);
}

static void kbase_pm_callback_suspend(struct kbase_device *kbdev)
{
	kbase_pm_callback_power_off(kbdev);
}

struct kbase_pm_callback_conf mt8192_pm_callbacks = {
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


static int mali_mfgsys_init(struct kbase_device *kbdev)
{
	int err, i;
	unsigned long volt;
	struct mtk_platform_context *mfg = kbdev->platform_context;
	const struct mtk_hw_config *cfg = mfg->config;

	kbdev->num_pm_domains = cfg->num_pm_domains;

	err = kbase_pm_domain_init(kbdev);
	if (err < 0)
		return err;

	for (i = 0; i < kbdev->nr_regulators; i++)
		if (kbdev->regulators[i] == NULL)
			return -EINVAL;

	mfg->num_clks = ARRAY_SIZE(gpu_clocks);
	mfg->clks = devm_kcalloc(kbdev->dev, mfg->num_clks,
				     sizeof(*mfg->clks), GFP_KERNEL);

	if (!mfg->clks)
		return -ENOMEM;

	for (i = 0; i < mfg->num_clks; ++i)
		mfg->clks[i].id = gpu_clocks[i];

	err = devm_clk_bulk_get(kbdev->dev, mfg->num_clks, mfg->clks);
	if (err != 0) {
		dev_err(kbdev->dev,
			"clk_bulk_get error: %d\n",
			err);
		return err;
	}

	for (i = 0; i < kbdev->nr_regulators; i++) {
		volt = (i == 0) ? cfg->vgpu_max_microvolt : cfg->vsram_gpu_max_microvolt;
		err = regulator_set_voltage(kbdev->regulators[i],
			volt, volt + cfg->supply_tolerance_microvolt);
		if (err < 0) {
			dev_err(kbdev->dev,
				"Regulator %d set voltage failed: %d\n",
				i, err);
			return err;
		}
#ifdef CONFIG_MALI_VALHALL_DEVFREQ
		kbdev->current_voltages[i] = volt;
#endif
	}

	err = map_mfg_base(mfg);
	if (err) {
		dev_err(kbdev->dev, "Cannot find mfgcfg node\n");
		return err;
	}

	mfg->is_powered = false;

	return 0;
}

#ifdef CONFIG_MALI_VALHALL_DEVFREQ
static int set_frequency(struct kbase_device *kbdev, unsigned long freq)
{
	int err;
	struct mtk_platform_context *mfg = kbdev->platform_context;

	if (kbdev->current_freqs[0] != freq) {
		err = clk_set_parent(mfg->clks[mux].clk, mfg->clks[sub].clk);
		if (err) {
			dev_err(kbdev->dev, "Failed to select sub clock src\n");
			return err;
		}

		err = clk_set_rate(mfg->clks[main].clk, freq);
		if (err) {
			dev_err(kbdev->dev,
				"Failed to set clock rate: %lu (err: %d)\n",
				freq, err);
			return err;
		}
		kbdev->current_freqs[0] = freq;

		err = clk_set_parent(mfg->clks[mux].clk, mfg->clks[main].clk);
		if (err) {
			dev_err(kbdev->dev, "Failed to select main clock src\n");
			return err;
		}
	}

	return 0;
}
#endif

static int platform_init(struct kbase_device *kbdev)
{
	int err, i;
	struct mtk_platform_context *mfg = &mt8192_platform_context;
	const struct mtk_hw_config *cfg = mfg->config;

	kbdev->platform_context = mfg;

	err = mali_mfgsys_init(kbdev);
	if (err)
		return err;

	for (i = 0; i < kbdev->num_pm_domains; i++) {
		pm_runtime_set_autosuspend_delay(kbdev->pm_domain_devs[i],
						 cfg->auto_suspend_delay_ms);
		pm_runtime_use_autosuspend(kbdev->pm_domain_devs[i]);
	}

	err = clk_set_parent(mfg->clks[mux].clk, mfg->clks[sub].clk);
	if (err) {
		dev_err(kbdev->dev, "Failed to select sub clock src\n");
		return err;
	}

	err = clk_set_rate(mfg->clks[main].clk, cfg->gpu_freq_max_khz * 1000);
	if (err) {
		dev_err(kbdev->dev, "Failed to set clock %d kHz\n", cfg->gpu_freq_max_khz);
		return err;
	}

	err = clk_set_parent(mfg->clks[mux].clk, mfg->clks[main].clk);
	if (err) {
		dev_err(kbdev->dev, "Failed to select main clock src\n");
		return err;
	}

#ifdef CONFIG_MALI_VALHALL_DEVFREQ
	kbdev->devfreq_ops.set_frequency = set_frequency;
	kbdev->devfreq_ops.voltage_range_check = voltage_range_check;
#endif

	return 0;
}

struct kbase_platform_funcs_conf mt8192_platform_funcs = {
	.platform_init_func = platform_init,
	.platform_term_func = platform_term
};
