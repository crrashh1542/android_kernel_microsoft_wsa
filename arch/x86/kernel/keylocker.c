// SPDX-License-Identifier: GPL-2.0-only

/*
 * Setup Key Locker feature and support internal wrapping key
 * management.
 */

#include <linux/random.h>
#include <linux/poison.h>

#include <asm/fpu/api.h>
#include <asm/keylocker.h>
#include <asm/tlbflush.h>

static __initdata struct keylocker_setup_data {
	struct iwkey key;
} kl_setup;

static void __init generate_keylocker_data(void)
{
	get_random_bytes(&kl_setup.key.integrity_key,  sizeof(kl_setup.key.integrity_key));
	get_random_bytes(&kl_setup.key.encryption_key, sizeof(kl_setup.key.encryption_key));
}

void __init destroy_keylocker_data(void)
{
	memset(&kl_setup.key, KEY_DESTROY, sizeof(kl_setup.key));
}

static void __init load_keylocker(void)
{
	kernel_fpu_begin();
	load_xmm_iwkey(&kl_setup.key);
	kernel_fpu_end();
}

/**
 * setup_keylocker - Enable the feature.
 * @c:		A pointer to struct cpuinfo_x86
 */
void __ref setup_keylocker(struct cpuinfo_x86 *c)
{
	if (!cpu_feature_enabled(X86_FEATURE_KEYLOCKER))
		goto out;

	if (cpu_feature_enabled(X86_FEATURE_HYPERVISOR)) {
		pr_debug("x86/keylocker: Not compatible with a hypervisor.\n");
		goto disable;
	}

	cr4_set_bits(X86_CR4_KEYLOCKER);

	if (c == &boot_cpu_data) {
		u32 eax, ebx, ecx, edx;

		cpuid_count(KEYLOCKER_CPUID, 0, &eax, &ebx, &ecx, &edx);
		/*
		 * Check the feature readiness via CPUID. Note that the
		 * CPUID AESKLE bit is conditionally set only when CR4.KL
		 * is set.
		 */
		if (!(ebx & KEYLOCKER_CPUID_EBX_AESKLE) ||
		    !(eax & KEYLOCKER_CPUID_EAX_SUPERVISOR)) {
			pr_debug("x86/keylocker: Not fully supported.\n");
			goto disable;
		}

		generate_keylocker_data();
	}

	load_keylocker();

	pr_info_once("x86/keylocker: Enabled.\n");
	return;

disable:
	setup_clear_cpu_cap(X86_FEATURE_KEYLOCKER);
	pr_info_once("x86/keylocker: Disabled.\n");
out:
	/* Make sure the feature disabled for kexec-reboot. */
	cr4_clear_bits(X86_CR4_KEYLOCKER);
}
