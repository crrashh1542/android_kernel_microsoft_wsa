/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 Google LLC. */

#ifndef _MALI_KBASE_RUNTIME_PM_H_
#define _MALI_KBASE_RUNTIME_PM_H_

#include <linux/pm_domain.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>

void kbase_pm_domain_term(struct kbase_device *kbdev);
int kbase_pm_runtime_callback_init(struct kbase_device *kbdev);
void kbase_pm_runtime_callback_term(struct kbase_device *kbdev);
int kbase_pm_runtime_callback_on(struct kbase_device *kbdev);
void kbase_pm_runtime_callback_off(struct kbase_device *kbdev);

void platform_term(struct kbase_device *kbdev);
#endif /* _MALI_KBASE_RUNTIME_PM_H_ */
