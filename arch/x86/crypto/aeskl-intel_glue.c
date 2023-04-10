// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for AES Key Locker instructions. This file contains glue
 * code and the real AES implementation is in aeskl-intel_asm.S.
 *
 * Most code is based on AES-NI glue code, aesni-intel_glue.c
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/xts.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/simd.h>
#include <asm/simd.h>
#include <asm/cpu_device_id.h>
#include <asm/fpu/api.h>
#include <asm/keylocker.h>

#include "aes-intel_glue.h"
#include "aesni-intel_glue.h"

asmlinkage int aeskl_setkey(struct crypto_aes_ctx *ctx, const u8 *in_key, unsigned int key_len);

asmlinkage int _aeskl_enc(const void *ctx, u8 *out, const u8 *in);
asmlinkage int _aeskl_dec(const void *ctx, u8 *out, const u8 *in);

static int __maybe_unused aeskl_setkey_common(struct crypto_tfm *tfm, void *raw_ctx,
					      const u8 *in_key, unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = aes_ctx(raw_ctx);
	int err;

	if (!crypto_simd_usable())
		return -EBUSY;

	if (key_len != AES_KEYSIZE_128 && key_len != AES_KEYSIZE_192 &&
	    key_len != AES_KEYSIZE_256)
		return -EINVAL;

	kernel_fpu_begin();
	if (unlikely(key_len == AES_KEYSIZE_192)) {
		pr_warn_once("AES-KL does not support 192-bit key. Use AES-NI.\n");
		err = aesni_set_key(ctx, in_key, key_len);
	} else {
		if (!valid_keylocker())
			err = -ENODEV;
		else
			err = aeskl_setkey(ctx, in_key, key_len);
	}
	kernel_fpu_end();

	return err;
}

static inline u32 keylength(const void *raw_ctx)
{
	struct crypto_aes_ctx *ctx = aes_ctx((void *)raw_ctx);

	return ctx->key_length;
}

static inline int aeskl_enc(const void *ctx, u8 *out, const u8 *in)
{
	if (unlikely(keylength(ctx) == AES_KEYSIZE_192))
		return -EINVAL;
	else if (!valid_keylocker())
		return -ENODEV;
	else if (_aeskl_enc(ctx, out, in))
		return -EINVAL;
	else
		return 0;
}

static inline int aeskl_dec(const void *ctx, u8 *out, const u8 *in)
{
	if (unlikely(keylength(ctx) == AES_KEYSIZE_192))
		return -EINVAL;
	else if (!valid_keylocker())
		return -ENODEV;
	else if (_aeskl_dec(ctx, out, in))
		return -EINVAL;
	else
		return 0;
}

static int __init aeskl_init(void)
{
	if (!valid_keylocker())
		return -ENODEV;

	/*
	 * AES-KL itself does not depend on AES-NI. But AES-KL does not
	 * support 192-bit keys. To make itself AES-compliant, it falls
	 * back to AES-NI.
	 */
	if (!boot_cpu_has(X86_FEATURE_AES))
		return -ENODEV;

	return 0;
}

static void __exit aeskl_exit(void)
{
	return;
}

late_initcall(aeskl_init);
module_exit(aeskl_exit);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, AES Key Locker implementation");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("aes");
