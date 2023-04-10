/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Implement AES algorithm using Intel AES Key Locker instructions.
 *
 * Most code is based from the AES-NI implementation, aesni-intel_asm.S
 *
 */

#include <linux/linkage.h>
#include <asm/inst.h>
#include <asm/frame.h>
#include "aes-intel_asm.S"

.text

#define STATE1	%xmm0
#define STATE2	%xmm1
#define STATE3	%xmm2
#define STATE4	%xmm3
#define STATE5	%xmm4
#define STATE6	%xmm5
#define STATE7	%xmm6
#define STATE8	%xmm7
#define STATE	STATE1

#define IV	%xmm9
#define KEY	%xmm10
#define BSWAP_MASK %xmm11
#define CTR	%xmm12
#define INC	%xmm13

#ifdef __x86_64__
#define IN1	%xmm8
#define IN2	%xmm9
#define IN3	%xmm10
#define IN4	%xmm11
#define IN5	%xmm12
#define IN6	%xmm13
#define IN7	%xmm14
#define IN8	%xmm15
#define IN	IN1
#define TCTR_LOW %r11
#else
#define IN	%xmm1
#endif

#ifdef __x86_64__
#define AREG	%rax
#define HANDLEP	%rdi
#define OUTP	%rsi
#define KLEN	%r9d
#define INP	%rdx
#define T1	%r10
#define LEN	%rcx
#define IVP	%r8
#else
#define AREG	%eax
#define HANDLEP	%edi
#define OUTP	AREG
#define KLEN	%ebx
#define INP	%edx
#define T1	%ecx
#define LEN	%esi
#define IVP	%ebp
#endif

#define UKEYP	OUTP
#define GF128MUL_MASK %xmm11

/*
 * int aeskl_setkey(struct crypto_aes_ctx *ctx, const u8 *in_key, unsigned int key_len)
 */
SYM_FUNC_START(aeskl_setkey)
	FRAME_BEGIN
#ifndef __x86_64__
	push HANDLEP
	movl (FRAME_OFFSET+8)(%esp),  HANDLEP	# ctx
	movl (FRAME_OFFSET+12)(%esp), UKEYP	# in_key
	movl (FRAME_OFFSET+16)(%esp), %edx	# key_len
#endif
	movl %edx, 480(HANDLEP)
	movdqu (UKEYP), STATE1
	mov $1, %eax
	cmp $16, %dl
	je .Lsetkey_128

	movdqu 0x10(UKEYP), STATE2
	encodekey256 %eax, %eax
	movdqu STATE4, 0x30(HANDLEP)
	jmp .Lsetkey_end
.Lsetkey_128:
	encodekey128 %eax, %eax

.Lsetkey_end:
	movdqu STATE1, (HANDLEP)
	movdqu STATE2, 0x10(HANDLEP)
	movdqu STATE3, 0x20(HANDLEP)

	xor AREG, AREG
#ifndef __x86_64__
	popl HANDLEP
#endif
	FRAME_END
	RET
SYM_FUNC_END(aeskl_setkey)

/*
 * int _aeskl_enc(const void *ctx, u8 *dst, const u8 *src)
 */
SYM_FUNC_START(_aeskl_enc)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl HANDLEP
	pushl KLEN
	movl (FRAME_OFFSET+12)(%esp), HANDLEP	# ctx
	movl (FRAME_OFFSET+16)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+20)(%esp), INP	# src
#endif
	movdqu (INP), STATE
	movl 480(HANDLEP), KLEN

	cmp $16, KLEN
	je .Lenc_128
	aesenc256kl (HANDLEP), STATE
	jz .Lenc_err
	jmp .Lenc_noerr
.Lenc_128:
	aesenc128kl (HANDLEP), STATE
	jz .Lenc_err

.Lenc_noerr:
	xor AREG, AREG
	jmp .Lenc_end
.Lenc_err:
	mov $1, AREG
.Lenc_end:
	movdqu STATE, (OUTP)
#ifndef __x86_64__
	popl KLEN
	popl HANDLEP
#endif
	FRAME_END
	RET
SYM_FUNC_END(_aeskl_enc)

/*
 * int _aeskl_dec(const void *ctx, u8 *dst, const u8 *src)
 */
SYM_FUNC_START(_aeskl_dec)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl HANDLEP
	pushl KLEN
	movl (FRAME_OFFSET+12)(%esp), HANDLEP	# ctx
	movl (FRAME_OFFSET+16)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+20)(%esp), INP	# src
#endif
	movdqu (INP), STATE
	mov 480(HANDLEP), KLEN

	cmp $16, KLEN
	je .Ldec_128
	aesdec256kl (HANDLEP), STATE
	jz .Ldec_err
	jmp .Ldec_noerr
.Ldec_128:
	aesdec128kl (HANDLEP), STATE
	jz .Ldec_err

.Ldec_noerr:
	xor AREG, AREG
	jmp .Ldec_end
.Ldec_err:
	mov $1, AREG
.Ldec_end:
	movdqu STATE, (OUTP)
#ifndef __x86_64__
	popl KLEN
	popl HANDLEP
#endif
	FRAME_END
	RET
SYM_FUNC_END(_aeskl_dec)

