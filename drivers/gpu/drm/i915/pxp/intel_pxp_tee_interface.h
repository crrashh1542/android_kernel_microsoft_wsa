/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TEE_INTERFACE_H__
#define __INTEL_PXP_TEE_INTERFACE_H__

#include <linux/types.h>

#define PXP_TEE_APIVER 0x40002
#define PXP_TEE_ARB_CMDID 0x1e
#define PXP_TEE_ARB_PROTECTION_MODE 0x2
#define PXP_TEE_INVALIDATE_STREAM_KEY 0x00000007

/* PXP TEE message header */
struct pxp_tee_cmd_header {
	u32 api_version;
	u32 command_id;
	union {
		u32 status;
		u32 extdata;
#define PXP_CMDHDR_EXTDATA_SESSION_VALID GENMASK(0, 0)
#define PXP_CMDHDR_EXTDATA_APP_TYPE GENMASK(1, 1)
#define PXP_CMDHDR_EXTDATA_SESSION_ID GENMASK(17, 2)
	};
	/* Length of the message (excluding the header) */
	u32 buffer_len;
} __packed;

/* PXP TEE message input to create a arbitrary session */
struct pxp_tee_create_arb_in {
	struct pxp_tee_cmd_header header;
	u32 protection_mode;
	u32 session_id;
} __packed;

/* PXP TEE message output to create a arbitrary session */
struct pxp_tee_create_arb_out {
	struct pxp_tee_cmd_header header;
} __packed;

/* PXP TEE message to cleanup a session (input) */
struct pxp_inv_stream_key_in {
	struct pxp_tee_cmd_header header;
	u32 rsvd[3];
} __packed;

/* PXP TEE message to cleanup a session (output) */
struct pxp_inv_stream_key_out {
	struct pxp_tee_cmd_header header;
	u32 rsvd;
} __packed;

#endif /* __INTEL_PXP_TEE_INTERFACE_H__ */
