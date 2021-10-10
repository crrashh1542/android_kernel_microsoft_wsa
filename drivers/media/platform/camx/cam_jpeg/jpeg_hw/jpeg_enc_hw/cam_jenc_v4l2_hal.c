// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, MM Solutions EAD. All rights reserved.
 *	Author: Atanas Filipov <afilipov@mm-sol.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/div64.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-sg.h>

#include "jpeg_enc_core.h"
#include "cam_mem_mgr.h"
#include "cam_jpeg_hw_mgr.h"
#include "cam_jpeg_dev.h"
#include "cam_jenc_v4l2_defs.h"
#include "cam_jenc_v4l2_hal.h"
#include "cam_smmu_api.h"

#define JENC_FE_CFG_BURST_LENGTH 7
#define JENC_WE_CFG_BURST_LENGTH 3

#define JPEG_ENC_HBLOCK_16	16
#define JPEG_ENC_WBLOCK_16	16
#define JPEG_ENC_HBLOCK_8	8
#define JPEG_ENC_WBLOCK_8	8

#define JENC_REG_SCALE_DEFAULT_STEP 0x200000

// QC JFIF Header and definition
struct qc_jfif {
	u8 soi_marker[2];

	u8 dqt_marker[2];
	u8 dqt_length[2];

	u8 dqt_id1[1];
	u8 dqt_table1[64];
	u8 dqt_id2[1];
	u8 dqt_table2[64];

	u8 sof0_marker[2];

	u8 sof0_component_length[2];
	u8 sof0_precision[1];

	u8 sof0_height[2];
	u8 sof0_width[2];

	u8 sof_0_number_components[1];

	u8 sof0_component_luma_id[1];
	u8 sof0_component_luma_subsample_factor[1];
	u8 sof0_component_luma_quantization_selector[1];

	u8 sof0_component_cb_id[1];
	u8 sof0_component_cb_subsample_factor[1];
	u8 sof0_component_cb_quantization_selector[1];

	u8 sof0_component_cr_id[1];
	u8 sof0_component_cr_subsample_factor[1];
	u8 sof0_component_cr_quantization_selector[1];

	u8 dht_marker[2];
	u8 dht_length[2];

	u8 dht_luma_dc_index[1];
	u8 dht_luma_dc_bits[16];
	u8 dht_luma_dc_values[12];

	u8 dht_luma_ac_index[1];
	u8 dht_luma_ac_bits[16];
	u8 dht_luma_ac_values[162];

	u8 dht_chroma_dc_index[1];
	u8 dht_chrome_dc_bits[16];
	u8 dht_chrome_dc_values[12];

	u8 dht_chroma_ac_index[1];
	u8 dht_chroma_ac_bits[16];
	u8 dht_chroma_ac_values[162];

	u8 sos_marker[2];
	u8 sos_length[2];

	u8 sos_number_components[1];

	u8 sos_luma_dc_ac_table_indexes[2];
	u8 sos_cb_dc_ac_table_indexes[2];
	u8 sos_cr_dc_ac_table_indexes[2];

	u8 sos_subsampling[2];
	u8 sos_dummy[1];
};

static const struct qc_jfif jfif = {
	.soi_marker = { 0xff, 0xd8 },

	.dqt_marker = { 0xff, 0xdb },
	.dqt_length = { 0, 0x84 },

	.dqt_id1 = { 0 },
	.dqt_table1 = {
		0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01,
		0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04,
		0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04, 0x04,
		0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05,
		0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06, 0x07,
		0x09, 0x07, 0x06, 0x06, 0x08, 0x0b, 0x08, 0x09,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x06, 0x08, 0x0b,
		0x0c, 0x0b, 0x0a, 0x0c, 0x09, 0x0a, 0x0a, 0x0a,
	},

	.dqt_id2 = { 1 },
	.dqt_table2 = {
		0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x05, 0x03,
		0x03, 0x05, 0x0a, 0x07, 0x06, 0x07, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
		0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a
	},

	.sof0_marker = { 0xff, 0xc0 },

	.sof0_component_length = { 0, 0x11 },
	.sof0_precision = { 8 },

	.sof0_height = { 0 },
	.sof0_width = { 0 },

	.sof_0_number_components = { 3 },

	.sof0_component_luma_id = { 1 },
	.sof0_component_luma_subsample_factor = { 0x22 },
	.sof0_component_luma_quantization_selector  = { 0x0 },

	.sof0_component_cb_id = { 2 },
	.sof0_component_cb_subsample_factor = { 0x11 },
	.sof0_component_cb_quantization_selector = { 0x1 },

	.sof0_component_cr_id = { 3 },
	.sof0_component_cr_subsample_factor = { 0x11 },
	.sof0_component_cr_quantization_selector = { 0x1 },

	.dht_marker = { 0xff, 0xc4 },

	.dht_length = { 1, 0xa2 },

	.dht_luma_dc_index = { 0 },
	.dht_luma_dc_bits = {
		0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
	},
	.dht_luma_dc_values = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
	},
	.dht_luma_ac_index = { 0x10 },
	.dht_luma_ac_bits = {
		0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
	},
	.dht_luma_ac_values = {
		0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
		0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
		0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
		0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
		0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
		0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
		0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
		0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
		0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
		0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
		0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
		0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
		0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
		0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
		0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
		0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
		0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
		0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
		0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
		0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
		0xf9, 0xfa
	},

	.dht_chroma_dc_index = { 1 },
	.dht_chrome_dc_bits = {
		0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
	},
	.dht_chrome_dc_values = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
	},
	.dht_chroma_ac_index = { 0x11 },
	.dht_chroma_ac_bits = {
		0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77
	},
	.dht_chroma_ac_values = {
		0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
		0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
		0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
		0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
		0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
		0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
		0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
		0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
		0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
		0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
		0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
		0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
		0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
		0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
		0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
		0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
		0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
		0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
		0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
		0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
		0xf9, 0xfa
	},

	.sos_marker = { 0xff, 0xda },

	.sos_length =  { 0, 0xc },
	.sos_number_components = { 3 },

	.sos_luma_dc_ac_table_indexes = { 1, 0 },

	.sos_cb_dc_ac_table_indexes = { 0x2, 0x11 },
	.sos_cr_dc_ac_table_indexes = { 0x3, 0x11 },
	.sos_subsampling = { 0, 0x3f },
	.sos_dummy = { 0 },
};

struct jenc_reg_info {
	u32 offs;
	u32 mask;
	u32 shift;
};

static const u32 jenc_quant_table[] = {
	0x8000, 0xffff, 0xffff, 0x8000, 0x8000, 0x4000, 0x3333, 0x2aaa,
	0xffff, 0xffff, 0xffff, 0x8000, 0x5555, 0x2aaa, 0x2aaa, 0x2aaa,
	0xffff, 0xffff, 0x8000, 0x8000, 0x4000, 0x2aaa, 0x2492, 0x2aaa,
	0xffff, 0x8000, 0x8000, 0x5555, 0x3333, 0x1c71, 0x2000, 0x2aaa,
	0x8000, 0x8000, 0x4000, 0x2aaa, 0x2492, 0x1745, 0x1999, 0x2000,
	0x8000, 0x4000, 0x2aaa, 0x2aaa, 0x2000, 0x1999, 0x1745, 0x1c71,
	0x3333, 0x2aaa, 0x2000, 0x1c71, 0x1999, 0x1555, 0x1555, 0x1999,
	0x2492, 0x1c71, 0x1999, 0x1999, 0x1745, 0x1999, 0x1999, 0x1999,
	0x8000, 0x8000, 0x8000, 0x3333, 0x1999, 0x1999, 0x1999, 0x1999,
	0x8000, 0x8000, 0x5555, 0x2492, 0x1999, 0x1999, 0x1999, 0x1999,
	0x8000, 0x5555, 0x2aaa, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999,
	0x3333, 0x2492, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999,
	0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999,
	0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999,
	0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999,
	0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999, 0x1999
};

static int jenc_hal_dma_buffer_put(struct jenc_context *jctx,
				   struct vb2_buffer *vb2)
{
	struct jenc_bufq *bq = &jctx->bufq[TYPE2ID(vb2->type)];
	int rc, pln;

	for (pln = 0; pln < vb2->num_planes; pln++) {
		if (bq->mapped[pln].iova && bq->mapped[pln].length) {
			rc = cam_smmu_unmap_ext_buff(jctx->iommu_hdl,
						     bq->mapped[pln].iova,
						     bq->mapped[pln].length,
						     bq->mapped[pln].reg_id);
			bq->mapped[pln].iova   = 0;
			bq->mapped[pln].length = 0;
			bq->mapped[pln].reg_id = 0;
			if (rc) {
				dev_err(jctx->dev, "Unmap ext buff failed");
				return rc;
			}
		}
	}

	return 0;
}

static int jenc_hal_dma_buffer_get(struct jenc_context *jctx,
				   struct vb2_buffer *vb2, int plane,
				   dma_addr_t *dma_addr)
{
	struct jenc_bufq *bq = &jctx->bufq[TYPE2ID(vb2->type)];
	struct sg_table *sgt;
	size_t length;
	union {
		dma_addr_t iova;
		u32 *mptr;
	} addr;

	addr.mptr = vb2_plane_vaddr(vb2, plane);
	if (!addr.mptr) {
		dev_err(jctx->dev, "Get plane vaddr failed\n");
		return -EINVAL;
	}

	sgt = vb2_dma_sg_plane_desc(vb2, plane);
	if (!sgt) {
		dev_err(jctx->dev, "Get plane sgt descriptor failed\n");
		return -ENODEV;
	}

	cam_smmu_map_ext_buff(sgt, jctx->iommu_hdl, &addr.iova, &length,
			      CAM_SMMU_REGION_IO);
	if (!length)
		return -EINVAL;

	bq->mapped[plane].reg_id = CAM_SMMU_REGION_IO;
	bq->mapped[plane].iova	 = addr.iova;
	bq->mapped[plane].length = length;

	*dma_addr = addr.iova;

	return 0;
}

static const struct jenc_reg_info fe_pl_ptr[] = {
	{
		JENC_PLN0_RD_PNTR_REG,
		JENC_PLN0_RD_PNTR_PNTR_MASK,
		JENC_PLN0_RD_PNTR_PNTR_SHIFT
	},
	{
		JENC_PLN1_RD_PNTR_REG,
		JENC_PLN1_RD_PNTR_PNTR_MASK,
		JENC_PLN1_RD_PNTR_PNTR_SHIFT
	},
	{
		JENC_PLN2_RD_PNTR_REG,
		JENC_PLN2_RD_PNTR_PNTR_MASK,
		JENC_PLN2_RD_PNTR_PNTR_SHIFT
	}
};

static const struct jenc_reg_info fe_int_hinit[] = {
	{
		JENC_PLN0_RD_HINIT_INT_REG,
		JENC_PLN0_RD_HINIT_INT_INTEGER_MASK,
		JENC_PLN0_RD_HINIT_INT_INTEGER_SHIFT
	},
	{
		JENC_PLN1_RD_HINIT_INT_REG,
		JENC_PLN1_RD_HINIT_INT_INTEGER_MASK,
		JENC_PLN1_RD_HINIT_INT_INTEGER_SHIFT
	},
	{
		JENC_PLN2_RD_HINIT_INT_REG,
		JENC_PLN2_RD_HINIT_INT_INTEGER_MASK,
		JENC_PLN2_RD_HINIT_INT_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info fe_frac_hinit[] = {
	{
		JENC_PLN0_RD_HINIT_REG,
		JENC_PLN0_RD_HINIT_FRACTIONAL_MASK,
		JENC_PLN0_RD_HINIT_FRACTIONAL_SHIFT
	},
	{
		JENC_PLN1_RD_HINIT_REG,
		JENC_PLN1_RD_HINIT_FRACTIONAL_MASK,
		JENC_PLN1_RD_HINIT_FRACTIONAL_SHIFT
	},
	{
		JENC_PLN2_RD_HINIT_REG,
		JENC_PLN2_RD_HINIT_FRACTIONAL_MASK,
		JENC_PLN2_RD_HINIT_FRACTIONAL_SHIFT
	}
};

static const struct jenc_reg_info fe_int_vinit[] = {
	{
		JENC_PLN0_RD_VINIT_INT_REG,
		JENC_PLN0_RD_VINIT_INT_INTEGER_MASK,
		JENC_PLN0_RD_VINIT_INT_INTEGER_SHIFT
	},
	{
		JENC_PLN1_RD_VINIT_INT_REG,
		JENC_PLN1_RD_VINIT_INT_INTEGER_MASK,
		JENC_PLN1_RD_VINIT_INT_INTEGER_SHIFT
	},
	{
		JENC_PLN2_RD_VINIT_INT_REG,
		JENC_PLN2_RD_VINIT_INT_INTEGER_MASK,
		JENC_PLN2_RD_VINIT_INT_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info fe_frac_vinit[] = {
	{
		JENC_PLN0_RD_VINIT_REG,
		JENC_PLN0_RD_VINIT_FRACTIONAL_MASK,
		JENC_PLN0_RD_VINIT_FRACTIONAL_SHIFT
	},
	{
		JENC_PLN1_RD_VINIT_REG,
		JENC_PLN1_RD_VINIT_FRACTIONAL_MASK,
		JENC_PLN1_RD_VINIT_FRACTIONAL_SHIFT
	},
	{
		JENC_PLN2_RD_VINIT_REG,
		JENC_PLN2_RD_VINIT_FRACTIONAL_MASK,
		JENC_PLN2_RD_VINIT_FRACTIONAL_SHIFT
	},
};

static const struct jenc_reg_info fe_buff_width[] = {
	{
		JENC_PLN0_RD_BUFFER_SIZE_REG,
		JENC_PLN0_RD_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN0_RD_BUFFER_SIZE_WIDTH_SHIFT
	},
	{
		JENC_PLN1_RD_BUFFER_SIZE_REG,
		JENC_PLN1_RD_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN1_RD_BUFFER_SIZE_WIDTH_SHIFT
	},
	{
		JENC_PLN2_RD_BUFFER_SIZE_REG,
		JENC_PLN2_RD_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN2_RD_BUFFER_SIZE_WIDTH_SHIFT
	}
};

static const struct jenc_reg_info fe_buff_height[] = {
	{
		JENC_PLN0_RD_BUFFER_SIZE_REG,
		JENC_PLN0_RD_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN0_RD_BUFFER_SIZE_HEIGHT_SHIFT
	},
	{
		JENC_PLN1_RD_BUFFER_SIZE_REG,
		JENC_PLN1_RD_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN1_RD_BUFFER_SIZE_HEIGHT_SHIFT
	},
	{
		JENC_PLN2_RD_BUFFER_SIZE_REG,
		JENC_PLN2_RD_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN2_RD_BUFFER_SIZE_HEIGHT_SHIFT
	}
};

static const struct jenc_reg_info fe_stride[] = {
	{
		JENC_PLN0_RD_STRIDE_REG,
		JENC_PLN0_RD_STRIDE_STRIDE_MASK,
		JENC_PLN0_RD_STRIDE_STRIDE_SHIFT
	},
	{
		JENC_PLN1_RD_STRIDE_REG,
		JENC_PLN1_RD_STRIDE_STRIDE_MASK,
		JENC_PLN1_RD_STRIDE_STRIDE_SHIFT
	},
	{
		JENC_PLN2_RD_STRIDE_REG,
		JENC_PLN2_RD_STRIDE_STRIDE_MASK,
		JENC_PLN2_RD_STRIDE_STRIDE_SHIFT
	}
};

static const struct jenc_reg_info fe_cfg_planes[] = {
	{
		JENC_FE_CFG_REG,
		JENC_FE_CFG_PLN0_EN_MASK,
		JENC_FE_CFG_PLN0_EN_SHIFT
	},
	{
		JENC_FE_CFG_REG,
		JENC_FE_CFG_PLN1_EN_MASK,
		JENC_FE_CFG_PLN1_EN_SHIFT
	},
	{
		JENC_FE_CFG_REG,
		JENC_FE_CFG_PLN2_EN_MASK,
		JENC_FE_CFG_PLN2_EN_SHIFT
	},
};

static const struct jenc_reg_info fe_clr_planes_queue[] = {
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_RD_PLN0_QUEUE_MASK,
		JENC_CMD_CLEAR_RD_PLN0_QUEUE_SHIFT
	},
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_RD_PLN1_QUEUE_MASK,
		JENC_CMD_CLEAR_RD_PLN1_QUEUE_SHIFT
	},
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_RD_PLN2_QUEUE_MASK,
		JENC_CMD_CLEAR_RD_PLN2_QUEUE_SHIFT
	},
};

static int jenc_hal_setup_fe_addr(struct jenc_context *jctx,
				  struct vb2_buffer *vb2)
{
	dma_addr_t dma;
	int rc, pln = 0;

	rc = jenc_hal_dma_buffer_get(jctx, vb2, pln, &dma);
	if (rc) {
		dev_err(jctx->dev, "Buffer mapping failed rc:%d\n", rc);
		return rc;
	}

	jctx->rmap[fe_pl_ptr[pln].offs] = (dma << fe_pl_ptr[pln].shift) &
					  fe_pl_ptr[pln].mask;
	pln++;

	dma += vb2->planes[pln].data_offset;
	jctx->rmap[fe_pl_ptr[pln].offs] = (dma << fe_pl_ptr[pln].shift) &
					  fe_pl_ptr[pln].mask;

	return 0;
}

static void jenc_hal_setup_fe_crop(struct jenc_context *jctx,
				   struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *spx = &bq->vf.fmt.pix_mp;
	int i;

	for (i = 0; i < CAM_PACKET_MAX_PLANES; i++) {
		jctx->rmap[fe_int_hinit[i].offs]  = 0;
		jctx->rmap[fe_frac_hinit[i].offs] = 0;
	}

	if (jctx->sel.r.width != spx->width) {
		for (i = 0; i < spx->num_planes; i++) {
			u64 hinit = jctx->sel.r.left << (fe_int_hinit[i].shift +
				    fe_frac_hinit[i].shift);
			u32 hfrac = do_div(hinit, spx->width);

			jctx->rmap[fe_int_hinit[i].offs] =
				(hfrac << fe_int_hinit[i].shift) &
				fe_int_hinit[i].mask;

			jctx->rmap[fe_frac_hinit[i].offs] |=
				(hfrac << fe_frac_hinit[i].shift) &
				fe_frac_hinit[i].mask;
		}
	}

	for (i = 0; i < CAM_PACKET_MAX_PLANES; i++) {
		jctx->rmap[fe_int_vinit[i].offs]  = 0;
		jctx->rmap[fe_frac_vinit[i].offs] = 0;
	}

	if (jctx->sel.r.height != spx->height) {
		for (i = 0; i < spx->num_planes; i++) {
			u64 vinit = jctx->sel.r.left << (fe_int_vinit[i].shift +
				    fe_frac_vinit[i].shift);
			u32 vfrac = do_div(vinit, spx->width);

			jctx->rmap[fe_int_vinit[i].offs] =
				(vfrac << fe_int_vinit[i].shift) &
				fe_int_vinit[i].mask;

			jctx->rmap[fe_frac_vinit[i].offs] |=
				(vfrac << fe_frac_vinit[i].shift) &
				fe_frac_vinit[i].mask;
		}
	}
}

static void jenc_hal_setup_fe_size(struct jenc_context *jctx,
				   struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *mpx = &bq->vf.fmt.pix_mp;
	u32 width, height, stride;
	int i;

	for (i = 0; i < mpx->num_planes; i++) {
		width = (mpx->width - 1);
		jctx->rmap[fe_buff_width[i].offs] =
			(width << fe_buff_width[i].shift) &
			fe_buff_width[i].mask;

		height = (mpx->height >> i) - 1;
		jctx->rmap[fe_buff_height[i].offs] |=
			(height << fe_buff_height[i].shift) &
			fe_buff_height[i].mask;

		stride = mpx->plane_fmt[i].bytesperline;
		jctx->rmap[fe_stride[i].offs] = (stride <<
			fe_stride[i].shift) & fe_stride[i].mask;

		dev_dbg(jctx->dev, "%s ctx:%p plane:%d, W:0x%x H:0x%x\n",
			__func__, jctx, i,
			(jctx->rmap[fe_buff_width[i].offs] &
			fe_buff_width[i].mask) >> fe_buff_width[i].shift,
			(jctx->rmap[fe_buff_height[i].offs] &
			fe_buff_height[i].mask) >> fe_buff_height[i].shift);
	}
}

void jenc_hal_setup_fe_engine(struct jenc_context *jctx, struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *mpx = &bq->vf.fmt.pix_mp;
	union jenc_cmd pcmd_reg;
	union jenc_core_cfg core_cfg;
	union jenc_fe_cfg fe_cfg;
	int i;

	jenc_hal_setup_fe_crop(jctx, bq);

	jenc_hal_setup_fe_size(jctx, bq);

	fe_cfg.w32		= jctx->rmap[JENC_FE_CFG_REG];
	fe_cfg.memory_format	= JENC_FE_CFG_MEM_FMT_PPLANAR;
	fe_cfg.mal_boundary	= JENC_FE_CFG_MAL_BOUND_MAL_128_BYTES;
	fe_cfg.sixteen_mcu_en	= 1;
	fe_cfg.mcus_per_block	= 0;

	if (mpx->pixelformat == V4L2_PIX_FMT_NV21)
		fe_cfg.cbcr_order = 1;

	pcmd_reg.w32		= jctx->rmap[JENC_CMD_REG];

	for (i = 0; i < mpx->num_planes; i++) {
		fe_cfg.w32   |= fe_cfg_planes[i].mask;
		pcmd_reg.w32 |= fe_clr_planes_queue[i].mask;
	}

	jctx->rmap[JENC_FE_CFG_REG] = fe_cfg.w32;
	jctx->rmap[JENC_CMD_REG]    = pcmd_reg.w32;

	core_cfg.w32	   = jctx->rmap[JENC_CORE_CFG_REG];
	core_cfg.fe_enable = 1;

	jctx->rmap[JENC_CORE_CFG_REG] = core_cfg.w32;
}

static const struct jenc_reg_info we_pl_ptr[] = {
	{
		JENC_PLN0_WR_PNTR_REG,
		JENC_PLN0_WR_PNTR_PNTR_MASK,
		JENC_PLN0_WR_PNTR_PNTR_SHIFT
	},
	{
		JENC_PLN1_WR_PNTR_REG,
		JENC_PLN1_WR_PNTR_PNTR_MASK,
		JENC_PLN1_WR_PNTR_PNTR_SHIFT
	},
	{
		JENC_PLN2_WR_PNTR_REG,
		JENC_PLN2_WR_PNTR_PNTR_MASK,
		JENC_PLN2_WR_PNTR_PNTR_SHIFT
	}
};

static const struct jenc_reg_info we_int_hinit[] = {
	{
		JENC_PLN0_WR_HINIT_REG,
		JENC_PLN0_WR_HINIT_INTEGER_MASK,
		JENC_PLN0_WR_HINIT_INTEGER_SHIFT
	},
	{
		JENC_PLN1_WR_HINIT_REG,
		JENC_PLN1_WR_HINIT_INTEGER_MASK,
		JENC_PLN1_WR_HINIT_INTEGER_SHIFT
	},
	{
		JENC_PLN2_WR_HINIT_REG,
		JENC_PLN2_WR_HINIT_INTEGER_MASK,
		JENC_PLN2_WR_HINIT_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info we_int_hstep[] = {
	{
		JENC_PLN0_WR_HSTEP_REG,
		JENC_PLN0_WR_HSTEP_INTEGER_MASK,
		JENC_PLN0_WR_HSTEP_INTEGER_SHIFT
	},
	{
		JENC_PLN1_WR_HSTEP_REG,
		JENC_PLN1_WR_HSTEP_INTEGER_MASK,
		JENC_PLN1_WR_HSTEP_INTEGER_SHIFT
	},
	{
		JENC_PLN2_WR_HSTEP_REG,
		JENC_PLN2_WR_HSTEP_INTEGER_MASK,
		JENC_PLN2_WR_HSTEP_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info we_int_vinit[] = {
	{
		JENC_PLN0_WR_VINIT_REG,
		JENC_PLN0_WR_VINIT_INTEGER_MASK,
		JENC_PLN0_WR_VINIT_INTEGER_SHIFT
	},
	{
		JENC_PLN1_WR_VINIT_REG,
		JENC_PLN1_WR_VINIT_INTEGER_MASK,
		JENC_PLN1_WR_VINIT_INTEGER_SHIFT
	},
	{
		JENC_PLN2_WR_VINIT_REG,
		JENC_PLN2_WR_VINIT_INTEGER_MASK,
		JENC_PLN2_WR_VINIT_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info we_int_vstep[] = {
	{
		JENC_PLN0_WR_VSTEP_REG,
		JENC_PLN0_WR_VSTEP_INTEGER_MASK,
		JENC_PLN0_WR_VSTEP_INTEGER_SHIFT
	},
	{
		JENC_PLN1_WR_VSTEP_REG,
		JENC_PLN1_WR_VSTEP_INTEGER_MASK,
		JENC_PLN1_WR_VSTEP_INTEGER_SHIFT
	},
	{
		JENC_PLN2_WR_VSTEP_REG,
		JENC_PLN2_WR_VSTEP_INTEGER_MASK,
		JENC_PLN2_WR_VSTEP_INTEGER_SHIFT
	}
};

static const struct jenc_reg_info we_buff_width[] = {
	{
		JENC_PLN0_WR_BUFFER_SIZE_REG,
		JENC_PLN0_WR_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN0_WR_BUFFER_SIZE_WIDTH_SHIFT
	},
	{
		JENC_PLN1_WR_BUFFER_SIZE_REG,
		JENC_PLN1_WR_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN1_WR_BUFFER_SIZE_WIDTH_SHIFT
	},
	{
		JENC_PLN2_WR_BUFFER_SIZE_REG,
		JENC_PLN2_WR_BUFFER_SIZE_WIDTH_MASK,
		JENC_PLN2_WR_BUFFER_SIZE_WIDTH_SHIFT
	}
};

static const struct jenc_reg_info we_buff_height[] = {
	{
		JENC_PLN0_WR_BUFFER_SIZE_REG,
		JENC_PLN0_WR_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN0_WR_BUFFER_SIZE_HEIGHT_SHIFT
	},
	{
		JENC_PLN1_WR_BUFFER_SIZE_REG,
		JENC_PLN1_WR_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN1_WR_BUFFER_SIZE_HEIGHT_SHIFT
	},
	{
		JENC_PLN2_WR_BUFFER_SIZE_REG,
		JENC_PLN2_WR_BUFFER_SIZE_HEIGHT_MASK,
		JENC_PLN2_WR_BUFFER_SIZE_HEIGHT_SHIFT
	}
};

static const struct jenc_reg_info we_stride[] = {
	{
		JENC_PLN0_WR_STRIDE_REG,
		JENC_PLN0_WR_STRIDE_STRIDE_MASK,
		JENC_PLN0_WR_STRIDE_STRIDE_SHIFT
	},
	{
		JENC_PLN1_WR_STRIDE_REG,
		JENC_PLN1_WR_STRIDE_STRIDE_MASK,
		JENC_PLN1_WR_STRIDE_STRIDE_SHIFT
	},
	{
		JENC_PLN2_WR_STRIDE_REG,
		JENC_PLN2_WR_STRIDE_STRIDE_MASK,
		JENC_PLN2_WR_STRIDE_STRIDE_SHIFT
	}
};

static const struct jenc_reg_info we_cfg_planes[] = {
	{
		JENC_WE_CFG_REG,
		JENC_WE_CFG_PLN0_EN_MASK,
		JENC_WE_CFG_PLN0_EN_SHIFT
	},
	{
		JENC_WE_CFG_REG,
		JENC_WE_CFG_PLN1_EN_MASK,
		JENC_WE_CFG_PLN1_EN_SHIFT
	},
	{
		JENC_WE_CFG_REG,
		JENC_WE_CFG_PLN2_EN_MASK,
		JENC_WE_CFG_PLN2_EN_SHIFT
	},
};

static const struct jenc_reg_info we_clr_planes_queue[] = {
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_WR_PLN0_QUEUE_MASK,
		JENC_CMD_CLEAR_WR_PLN0_QUEUE_SHIFT
	},
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_WR_PLN1_QUEUE_MASK,
		JENC_CMD_CLEAR_WR_PLN1_QUEUE_SHIFT
	},
	{
		JENC_CMD_REG,
		JENC_CMD_CLEAR_WR_PLN2_QUEUE_MASK,
		JENC_CMD_CLEAR_WR_PLN2_QUEUE_SHIFT
	},
};

static int jenc_hal_setup_we_addr(struct jenc_context *jctx,
				  struct vb2_buffer *vb2)
{
	struct jenc_bufq *dbq = &jctx->bufq[JENC_DST_ID];
	dma_addr_t dma;
	struct qc_jfif *mptr;
	int rc, pln = 0;

	rc = jenc_hal_dma_buffer_get(jctx, vb2, pln, &dma);
	if (rc) {
		dev_err(jctx->dev, "Buffer mapping failed rc:%d\n", rc);
		return rc;
	}

	mptr = vb2_plane_vaddr(vb2, pln);
	if (!mptr) {
		dev_err(jctx->dev, "Get plane vaddr failed\n");
		return -EINVAL;
	}

	memcpy((void *)mptr, (void *)&jfif, min_t(size_t,
						  vb2->planes[pln].length,
						  sizeof(jfif)));
	mptr->sof0_width[0] = (dbq->vf.fmt.pix_mp.width & 0xFF00) >> 8;
	mptr->sof0_width[1] = dbq->vf.fmt.pix_mp.width & 0x00FF;
	mptr->sof0_height[0] = (dbq->vf.fmt.pix_mp.height & 0xFF00) >> 8;
	mptr->sof0_height[1] = dbq->vf.fmt.pix_mp.height & 0x00FF;

	dma += offsetof(struct qc_jfif, sos_dummy) + sizeof(jfif.sos_dummy);

	jctx->rmap[we_pl_ptr[pln].offs] = (dma << we_pl_ptr[pln].shift) &
					  we_pl_ptr[pln].mask;
	return 0;
}

static void jenc_hal_setup_we_crop(struct jenc_context *jctx,
				   struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *dpx = &bq->vf.fmt.pix_mp;
	u64 hinit, vinit;
	u32 hstep, vstep;
	int i;

	for (i = 0; i < CAM_PACKET_MAX_PLANES; i++) {
		jctx->rmap[we_int_hinit[i].offs] = 0;
		jctx->rmap[we_int_hstep[i].offs] = 0;
	}

	for (i = 0; i < dpx->num_planes; i++) {
		hinit = 0;
		jctx->rmap[we_int_hinit[i].offs] =
			(hinit << we_int_hinit[i].shift) &
			we_int_hinit[i].mask;

		hstep = dpx->width;
		jctx->rmap[we_int_hstep[i].offs] =
			(hstep << we_int_hstep[i].shift) &
			we_int_hstep[i].mask;
	}

	for (i = 0; i < CAM_PACKET_MAX_PLANES; i++) {
		jctx->rmap[we_int_vinit[i].offs] = 0;
		jctx->rmap[we_int_vstep[i].offs] = 0;
	}

	for (i = 0; i < dpx->num_planes; i++) {
		vinit = 0;
		jctx->rmap[we_int_vinit[i].offs] =
			(vinit << we_int_vinit[i].shift) &
			we_int_vinit[i].mask;

		vstep = dpx->height;
		jctx->rmap[we_int_vstep[i].offs] =
			(vstep << we_int_vstep[i].shift) &
			we_int_vstep[i].mask;
	}
}

static void jenc_hal_setup_we_size(struct jenc_context *jctx,
				   struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *mpx = &bq->vf.fmt.pix_mp;
	u32 stride;
	int i;

	for (i = 0; i < mpx->num_planes; i++) {
		jctx->rmap[we_buff_width[i].offs] =
			((mpx->width - 1) << we_buff_width[i].shift) &
			we_buff_width[i].mask;
		jctx->rmap[we_buff_height[i].offs] |=
			((mpx->height - 1) << we_buff_height[i].shift) &
			we_buff_height[i].mask;

		stride = mpx->plane_fmt[i].bytesperline;
		jctx->rmap[we_stride[i].offs] = (stride <<
			we_stride[i].shift) & we_stride[i].mask;

		dev_dbg(jctx->dev, "%s ctx:%p plane:%d, W:0x%x H:0x%x\n",
			__func__, jctx, i,
			(jctx->rmap[we_buff_width[i].offs] &
			we_buff_width[i].mask) >> we_buff_width[i].shift,
			(jctx->rmap[we_buff_height[i].offs] &
			we_buff_height[i].mask) >> we_buff_height[i].shift);
	}
}

void jenc_hal_setup_we_engine(struct jenc_context *jctx, struct jenc_bufq *bq)
{
	struct v4l2_pix_format_mplane *mpx = &bq->vf.fmt.pix_mp;
	union jenc_cmd pcmd_reg;
	union jenc_pln_wr_blk_cfg pln_wr_blk_cfg;
	union jenc_core_cfg core_cfg;
	union jenc_we_cfg we_cfg;
	union jenc_cfg cfg;
	int i;

	jenc_hal_setup_we_crop(jctx, bq);

	jenc_hal_setup_we_size(jctx, bq);

	we_cfg.w32		= jctx->rmap[JENC_WE_CFG_REG];
	we_cfg.memory_format	= JENC_WE_CFG_MEM_FMT_PLANAR;
	we_cfg.mal_boundary	= JENC_WE_CFG_MAL_BOUND_MAL_1K_BYTES;

	pcmd_reg.w32		= jctx->rmap[JENC_CMD_REG];

	for (i = 0; i < mpx->num_planes; i++) {
		we_cfg.w32   |= we_cfg_planes[i].mask;
		pcmd_reg.w32 |= we_clr_planes_queue[i].mask;
	}

	jctx->rmap[JENC_WE_CFG_REG] = we_cfg.w32;
	jctx->rmap[JENC_CMD_REG]    = pcmd_reg.w32;

	core_cfg.w32	    = jctx->rmap[JENC_CORE_CFG_REG];
	core_cfg.we_enable  = 1;
	core_cfg.enc_enable = 1;
	core_cfg.mode	    = 1;

	jctx->rmap[JENC_CORE_CFG_REG] = core_cfg.w32;

	cfg.image_format   = 3;
	cfg.apply_eoi	   = 1;

	jctx->rmap[JENC_CFG_REG] = cfg.w32;

	pln_wr_blk_cfg.blocks_per_row = ALIGN(mpx->width / 128, 16);
	pln_wr_blk_cfg.blocks_per_col = ALIGN(mpx->height / 16, 16);

	jctx->rmap[JENC_PLN0_WR_BLOCK_CFG_REG] = pln_wr_blk_cfg.w32;
}

static const struct jenc_reg_info enc_int_hstep[] = {
	{
		JENC_SCALE_PLN0_HSTEP_REG,
		JENC_SCALE_PLN0_HSTEP_INTEGER_MASK,
		JENC_SCALE_PLN0_HSTEP_INTEGER_SHIFT,
	},
	{
		JENC_SCALE_PLN1_HSTEP_REG,
		JENC_SCALE_PLN1_HSTEP_INTEGER_MASK,
		JENC_SCALE_PLN1_HSTEP_INTEGER_SHIFT,
	},
	{
		JENC_SCALE_PLN2_HSTEP_REG,
		JENC_SCALE_PLN2_HSTEP_INTEGER_MASK,
		JENC_SCALE_PLN2_HSTEP_INTEGER_SHIFT,
	}
};

static const struct jenc_reg_info enc_frac_hstep[] = {
	{
		JENC_SCALE_PLN0_HSTEP_REG,
		JENC_SCALE_PLN0_HSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN0_HSTEP_FRACTIONAL_SHIFT,
	},
	{
		JENC_SCALE_PLN1_HSTEP_REG,
		JENC_SCALE_PLN1_HSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN1_HSTEP_FRACTIONAL_SHIFT,
	},
	{
		JENC_SCALE_PLN2_HSTEP_REG,
		JENC_SCALE_PLN2_HSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN2_HSTEP_FRACTIONAL_SHIFT,
	}
};

static const struct jenc_reg_info enc_int_vstep[] = {
	{
		JENC_SCALE_PLN0_VSTEP_REG,
		JENC_SCALE_PLN0_VSTEP_INTEGER_MASK,
		JENC_SCALE_PLN0_VSTEP_INTEGER_SHIFT,
	},
	{
		JENC_SCALE_PLN1_VSTEP_REG,
		JENC_SCALE_PLN1_VSTEP_INTEGER_MASK,
		JENC_SCALE_PLN1_VSTEP_INTEGER_SHIFT,
	},
	{
		JENC_SCALE_PLN2_VSTEP_REG,
		JENC_SCALE_PLN2_VSTEP_INTEGER_MASK,
		JENC_SCALE_PLN2_VSTEP_INTEGER_SHIFT,
	}
};

static const struct jenc_reg_info enc_frac_vstep[] = {
	{
		JENC_SCALE_PLN0_VSTEP_REG,
		JENC_SCALE_PLN0_VSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN0_VSTEP_FRACTIONAL_SHIFT,
	},
	{
		JENC_SCALE_PLN1_VSTEP_REG,
		JENC_SCALE_PLN1_VSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN1_VSTEP_FRACTIONAL_SHIFT,
	},
	{
		JENC_SCALE_PLN2_VSTEP_REG,
		JENC_SCALE_PLN2_VSTEP_FRACTIONAL_MASK,
		JENC_SCALE_PLN2_VSTEP_FRACTIONAL_SHIFT,
	}
};

static void jenc_hal_setup_enc_scale(struct jenc_context *jctx)
{
	struct jenc_bufq *sbq = &jctx->bufq[JENC_SRC_ID];
	struct jenc_bufq *dbq = &jctx->bufq[JENC_DST_ID];
	struct v4l2_pix_format_mplane *spx = &sbq->vf.fmt.pix_mp;
	struct v4l2_pix_format_mplane *dpx = &dbq->vf.fmt.pix_mp;
	union jenc_img_size img_size;
	union jenc_scale_cfg scale_cfg = {0};
	union jenc_scale_pln_out_cfg pln_out_cfg = {0};
	union jenc_fe_vbpad_cfg vbpad_cfg = {0};
	u32 h_blk, w_blk;
	int i = 0;

	if (!scale_cfg.hscale_enable && !scale_cfg.vscale_enable) {
		for (i = 0; i < CAM_PACKET_MAX_PLANES; i++) {
			jctx->rmap[enc_int_hstep[i].offs] =
				JENC_REG_SCALE_DEFAULT_STEP;
			jctx->rmap[enc_int_vstep[i].offs] =
				JENC_REG_SCALE_DEFAULT_STEP;
		}
	} else {
		if (scale_cfg.hscale_enable) {
			u64 hratio = jctx->sel.r.width <<
				(enc_int_hstep[i].shift +
				 enc_frac_hstep[i].shift);

			do_div(hratio, spx->width);
			for (i = 0; i < dpx->num_planes; i++) {
				jctx->rmap[enc_int_hstep[i].offs] =
					(hratio << enc_int_hstep[i].shift) &
					enc_int_hstep[i].mask;
				jctx->rmap[enc_int_hstep[i].offs] =
					(hratio << enc_frac_hstep[i].shift) &
					enc_frac_hstep[i].mask;
			}
		}

		if (scale_cfg.vscale_enable) {
			u64 vratio = jctx->sel.r.width <<
				(enc_int_vstep[i].shift +
				 enc_frac_vstep[i].shift);

			do_div(vratio, spx->height);
			for (i = 0; i < dpx->num_planes; i++) {
				jctx->rmap[enc_int_vstep[i].offs] =
					(vratio << enc_int_vstep[i].shift) &
					enc_int_vstep[i].mask;
				jctx->rmap[enc_int_vstep[i].offs] =
					(vratio << enc_frac_vstep[i].shift) &
					enc_frac_vstep[i].mask;
			}
		}
	}

	// H2V2
	if (spx->pixelformat == V4L2_PIX_FMT_NV12 ||
	    spx->pixelformat == V4L2_PIX_FMT_NV21) {
		h_blk = JPEG_ENC_HBLOCK_16;
		w_blk = JPEG_ENC_WBLOCK_16;
	} else if (spx->pixelformat == V4L2_PIX_FMT_NV16 ||
		   spx->pixelformat == V4L2_PIX_FMT_NV61) {
		h_blk = JPEG_ENC_HBLOCK_8;
		w_blk = JPEG_ENC_WBLOCK_16;
	} else {
		h_blk = JPEG_ENC_HBLOCK_8;
		w_blk = JPEG_ENC_WBLOCK_8;
	}

	if (jctx->sel.r.width > dpx->width ||
	    jctx->sel.r.height > dpx->height)
		scale_cfg.subsample_en = 1;

	if (jctx->sel.r.width < dpx->width ||
	    jctx->sel.r.height < dpx->height)
		scale_cfg.upsample_en = 1;

	jctx->rmap[JENC_SCALE_CFG_REG] = scale_cfg.w32;

	pln_out_cfg.block_width	 = w_blk - 1;
	pln_out_cfg.block_height = h_blk - 1;
	jctx->rmap[JENC_SCALE_PLN0_OUT_CFG_REG] = pln_out_cfg.w32;

	pln_out_cfg.block_width  = JPEG_ENC_WBLOCK_8 - 1;
	pln_out_cfg.block_height = JPEG_ENC_HBLOCK_8 - 1;
	jctx->rmap[JENC_SCALE_PLN1_OUT_CFG_REG] = pln_out_cfg.w32;

	pln_out_cfg.block_width  = JPEG_ENC_WBLOCK_8 - 1;
	pln_out_cfg.block_height = JPEG_ENC_HBLOCK_8 - 1;
	jctx->rmap[JENC_SCALE_PLN2_OUT_CFG_REG] = pln_out_cfg.w32;

	img_size.encode_width  = ALIGN(dpx->width, JPEG_ENC_WBLOCK_16) /
				 w_blk - 1;
	img_size.encode_height = ALIGN(dpx->height, JPEG_ENC_HBLOCK_16) /
				 h_blk - 1;

	jctx->rmap[JENC_IMAGE_SIZE_REG] = img_size.w32;

	vbpad_cfg.block_row = img_size.encode_height;
	jctx->rmap[JENC_FE_VBPAD_REG] = vbpad_cfg.w32;
}

static void jenc_hal_apply_settings(struct jenc_context *jctx)
{
	u32 rid = JENC_CORE_CFG_REG;
	struct cam_jpeg_rw_pair wr_pair[1];

	for ( ; rid < ARRAY_SIZE(jctx->rmap); rid += sizeof(jctx->rmap[0])) {
		if (/*!jctx->rmap[rid] ||*/
		    (rid >= JENC_DMI_CFG_REG && rid <= JENC_DMI_DATA_REG))
			continue;

		wr_pair[0].off = rid;
		wr_pair[0].val = jctx->rmap[rid];
		if (cam_jpeg_enc_write(jctx->jenc->hw_priv, wr_pair,
				       ARRAY_SIZE(wr_pair)))
			break;
	}
}

static void jenc_hal_apply_defaults(struct jenc_context *jctx)
{
	jctx->rmap[JENC_HW_CAP_REG] = (JENC_HW_CAP_ENCODE_MASK		<<
				       JENC_HW_CAP_ENCODE_SHIFT)	|
				      (JENC_HW_CAP_DECODE_MASK		<<
				       JENC_HW_CAP_DECODE_SHIFT);

	jctx->rmap[JENC_IRQ_MASK_REG] = JENC_IRQ_MASK_MASK_MASK;

	jctx->rmap[JENC_CORE_CFG_REG] = JENC_CORE_CFG_TESTBUS_ENABLE_MASK;

	jctx->rmap[JENC_FE_CFG_REG] = JENC_FE_CFG_MAL_EN_MASK		  |
				      JENC_FE_CFG_BOTTOM_VPAD_EN_MASK	  |
				      ((JENC_FE_CFG_BURST_LENGTH	  <<
				      JENC_FE_CFG_BURST_LENGTH_MAX_SHIFT) &
				      JENC_FE_CFG_BURST_LENGTH_MAX_MASK);

	jctx->rmap[JENC_WE_CFG_REG] = JENC_FE_CFG_CBCR_ORDER_MASK	  |
				      JENC_WE_CFG_POP_BUFF_ON_EOS_MASK	  |
				      ((JENC_WE_CFG_BURST_LENGTH	  <<
				      JENC_WE_CFG_BURST_LENGTH_MAX_SHIFT) &
				      JENC_WE_CFG_BURST_LENGTH_MAX_MASK);
}

static void jenc_hal_apply_dmi_tables(struct jenc_context *jctx)
{
	struct cam_jpeg_rw_pair wr_pair[1];
	union jenc_dmi_cfg  pcfg = { .w32 = 0x00000011 };
	union jenc_dmi_addr addr = { .w32 = 0x00000000 };
	int i;

	// DMI upload start sequence
	wr_pair[0].off = JENC_DMI_CFG_REG;
	wr_pair[0].val = pcfg.w32;
	cam_jpeg_enc_write(jctx->jenc->hw_priv, wr_pair, ARRAY_SIZE(wr_pair));

	wr_pair[0].off = JENC_DMI_ADDR_REG;
	wr_pair[0].val = addr.w32;
	cam_jpeg_enc_write(jctx->jenc->hw_priv, wr_pair, ARRAY_SIZE(wr_pair));

	// DMI data upload
	for (i = 0; i < ARRAY_SIZE(jenc_quant_table); i++) {
		wr_pair[0].off = JENC_DMI_DATA_REG;
		wr_pair[0].val = jenc_quant_table[i];
		if (cam_jpeg_enc_write(jctx->jenc->hw_priv, wr_pair,
				       ARRAY_SIZE(wr_pair)))
			break;
	}

	// DMI upload end sequence
	wr_pair[0].off = JENC_DMI_ADDR_REG;
	wr_pair[0].val = pcfg.w32;
	cam_jpeg_enc_write(jctx->jenc->hw_priv, wr_pair, ARRAY_SIZE(wr_pair));
}

struct jenc_bufq *jenc_hal_get_bufq(struct jenc_context *jctx,
				    enum jenc_vid_id id)
{
	return &jctx->bufq[id];
}

void jenc_hal_process_done(struct jenc_context *jctx, bool cleanup)
{
	unsigned long flags;
	struct cam_jpeg_set_irq_cb irq_cd = {0};
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	enum vb2_buffer_state bstate = (cleanup) ? VB2_BUF_STATE_ERROR :
						   VB2_BUF_STATE_DONE;

	do {
		spin_lock_irqsave(&jctx->irqlock, flags);
		src_vb = v4l2_m2m_src_buf_remove(jctx->fh.m2m_ctx);
		spin_unlock_irqrestore(&jctx->irqlock, flags);
		if (!src_vb)
			break;

		v4l2_m2m_buf_done(src_vb, bstate);
	} while (cleanup);

	do {
		spin_lock_irqsave(&jctx->irqlock, flags);
		dst_vb = v4l2_m2m_dst_buf_remove(jctx->fh.m2m_ctx);
		spin_unlock_irqrestore(&jctx->irqlock, flags);
		if (!dst_vb)
			break;

		v4l2_m2m_buf_done(dst_vb, bstate);
	} while (cleanup);

	if (bstate == VB2_BUF_STATE_DONE) {
		jenc_hal_dma_buffer_put(jctx, &src_vb->vb2_buf);
		jenc_hal_dma_buffer_put(jctx, &dst_vb->vb2_buf);
	}

	cam_jpeg_enc_process_cmd(jctx->jenc->hw_priv, CAM_JPEG_CMD_SET_IRQ_CB,
				 &irq_cd, sizeof(irq_cd));
}

static int32_t jenc_hal_process_irq(u32 irq_status, s32 result_size, void *data)
{
	struct jenc_context *jctx = data;

	dev_dbg(jctx->dev, "%s irq:%x size:%d ctx:%p\n", __func__, irq_status,
		result_size, data);

	jctx->result_size = result_size;

	jenc_hal_process_done(jctx, false);

	v4l2_m2m_job_finish(jctx->jenc->m2m_dev, jctx->fh.m2m_ctx);

	return 0;
}

int jenc_hal_process_exec(struct jenc_context *jctx,
			  struct vb2_v4l2_buffer *src_vb,
			  struct vb2_v4l2_buffer *dst_vb)
{
	struct jenc_bufq *sbq = &jctx->bufq[JENC_SRC_ID];
	struct jenc_bufq *dbq = &jctx->bufq[JENC_DST_ID];
	struct cam_jpeg_set_irq_cb irq_cd;
	int rc;

	dev_dbg(jctx->dev, "%s(%d)\n", __func__, __LINE__);

	rc = jenc_hal_setup_fe_addr(jctx, &src_vb->vb2_buf);
	if (rc)
		return rc;

	jenc_hal_setup_fe_engine(jctx, sbq);

	rc = jenc_hal_setup_we_addr(jctx, &dst_vb->vb2_buf);
	if (rc)
		return rc;

	jenc_hal_setup_we_engine(jctx, dbq);

	jenc_hal_setup_enc_scale(jctx);

	jenc_hal_apply_settings(jctx);
	irq_cd.jpeg_hw_mgr_cb = jenc_hal_process_irq;
	irq_cd.data	      = jctx;
	irq_cd.b_set_cb	      = 1;
	rc = cam_jpeg_enc_process_cmd(jctx->jenc->hw_priv,
				      CAM_JPEG_CMD_SET_IRQ_CB, &irq_cd,
				      sizeof(irq_cd));
	if (rc)
		return rc;

	return cam_jpeg_enc_start_hw(jctx->jenc->hw_priv, NULL, 0);
}

int jenc_hal_prepare(struct jenc_context *jctx, bool hw_reset)
{
	int rc = 0;

	if (hw_reset) {
		rc = cam_jpeg_enc_init_hw(jctx->jenc->hw_priv, NULL, 0);
		if (rc)
			return rc;

		rc = cam_jpeg_enc_reset_hw(jctx->jenc->hw_priv, NULL, 0);
		if (rc)
			return rc;
	}

	jenc_hal_apply_defaults(jctx);

	jenc_hal_apply_dmi_tables(jctx);

	return rc;
}

void jenc_hal_release(struct jenc_context *jctx)
{
	cam_jpeg_enc_deinit_hw(jctx->jenc->hw_priv, NULL, 0);
}
