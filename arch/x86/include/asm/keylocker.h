/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_KEYLOCKER_H
#define _ASM_KEYLOCKER_H

#ifndef __ASSEMBLY__

#include <linux/bits.h>
#include <asm/fpu/types.h>

/**
 * struct iwkey - A temporary internal wrapping key storage.
 * @integrity_key:	A 128-bit key to check that key handles have not
 *			been tampered with.
 * @encryption_key:	A 256-bit encryption key used in
 *			wrapping/unwrapping a clear text key.
 *
 * This storage should be flushed immediately after loaded.
 */
struct iwkey {
	struct reg_128_bit integrity_key;
	struct reg_128_bit encryption_key[2];
};

#define KEYLOCKER_CPUID			0x019
#define KEYLOCKER_CPUID_EAX_SUPERVISOR	BIT(0)
#define KEYLOCKER_CPUID_EBX_AESKLE	BIT(0)
#define KEYLOCKER_CPUID_EBX_WIDE	BIT(2)
#define KEYLOCKER_CPUID_EBX_BACKUP	BIT(4)

#endif /*__ASSEMBLY__ */
#endif /* _ASM_KEYLOCKER_H */
