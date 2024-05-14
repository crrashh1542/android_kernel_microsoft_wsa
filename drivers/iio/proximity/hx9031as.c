// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 NanjingTianyihexin Electronics Ltd.
 * http://www.tianyihexin.com
 *
 * Driver for NanjingTianyihexin HX9031AS & HX9023S Cap Sensor
 * Author: Yasin Lee <yasin.lee.x@gmail.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <asm-generic/unaligned.h>

#define SET_BIT(data, idx)	((data) |= 1 << (idx))
#define CLR_BIT(data, idx)	((data) &= ~(1 << (idx)))
#define CHK_BIT(data, idx)	((data) & (1 << (idx)))

#define HX9031AS_CHIP_ID 0x1D
#define HX9031AS_CH_NUM 5
#define HX9031AS_CH_USED 0x1F
#define CH_DATA_2BYTES 2
#define CH_DATA_3BYTES 3
#define CH_DATA_BYTES_MAX CH_DATA_3BYTES
#define HX9031AS_ODR_MS 200
#define TYHX_DELAY_MS(x) msleep(x)
#define HX9023S_ON_BOARD 0
#define HX9031AS_ON_BOARD 1
#define HX9031AS_CHIP_SELECT HX9023S_ON_BOARD
#if (HX9031AS_CHIP_SELECT == HX9023S_ON_BOARD)
#define CS0 0
#define CS1 2
#define CS2 4
#define CS3 6
#define CS4 8
#else
#define CS0 4
#define CS1 2
#define CS2 6
#define CS3 0
#define CS4 8
#endif
#define IGNORED 16

#define HX9031AS_GLOBAL_CTRL0                   0x00
#define HX9031AS_GLOBAL_CTRL1                   0x01
#define HX9031AS_PRF_CFG                        0x02
#define HX9031AS_CH0_CFG_7_0                    0x03
#define HX9031AS_CH0_CFG_9_8                    0x04
#define HX9031AS_CH1_CFG_7_0                    0x05
#define HX9031AS_CH1_CFG_9_8                    0x06
#define HX9031AS_CH2_CFG_7_0                    0x07
#define HX9031AS_CH2_CFG_9_8                    0x08
#define HX9031AS_CH3_CFG_7_0                    0x09
#define HX9031AS_CH3_CFG_9_8                    0x0A
#define HX9031AS_CH4_CFG_7_0                    0x0B
#define HX9031AS_CH4_CFG_9_8                    0x0C
#define HX9031AS_RANGE_7_0                      0x0D
#define HX9031AS_RANGE_9_8                      0x0E
#define HX9031AS_RANGE_18_16                    0x0F
#define HX9031AS_AVG0_NOSR0_CFG                 0x10
#define HX9031AS_NOSR12_CFG                     0x11
#define HX9031AS_NOSR34_CFG                     0x12
#define HX9031AS_AVG12_CFG                      0x13
#define HX9031AS_AVG34_CFG                      0x14
#define HX9031AS_OFFSET_DAC0_7_0                0x15
#define HX9031AS_OFFSET_DAC0_9_8                0x16
#define HX9031AS_OFFSET_DAC1_7_0                0x17
#define HX9031AS_OFFSET_DAC1_9_8                0x18
#define HX9031AS_OFFSET_DAC2_7_0                0x19
#define HX9031AS_OFFSET_DAC2_9_8                0x1A
#define HX9031AS_OFFSET_DAC3_7_0                0x1B
#define HX9031AS_OFFSET_DAC3_9_8                0x1C
#define HX9031AS_OFFSET_DAC4_7_0                0x1D
#define HX9031AS_OFFSET_DAC4_9_8                0x1E
#define HX9031AS_SAMPLE_NUM_7_0                 0x1F
#define HX9031AS_SAMPLE_NUM_9_8                 0x20
#define HX9031AS_INTEGRATION_NUM_7_0            0x21
#define HX9031AS_INTEGRATION_NUM_9_8            0x22
#define HX9031AS_GLOBAL_CTRL2                   0x23
#define HX9031AS_CH_NUM_CFG                     0x24
#define HX9031AS_DAC_SWAP_CFG                   0x25
#define HX9031AS_MOD_RST_CFG                    0x28
#define HX9031AS_LP_ALP_4_CFG                   0x29
#define HX9031AS_LP_ALP_1_0_CFG                 0x2A
#define HX9031AS_LP_ALP_3_2_CFG                 0x2B
#define HX9031AS_UP_ALP_1_0_CFG                 0x2C
#define HX9031AS_UP_ALP_3_2_CFG                 0x2D
#define HX9031AS_DN_UP_ALP_0_4_CFG              0x2E
#define HX9031AS_DN_ALP_2_1_CFG                 0x2F
#define HX9031AS_DN_ALP_4_3_CFG                 0x30
#define HX9031AS_INT_CAP_CFG                    0x31
#define HX9031AS_NDL_DLY_4_CFG                  0x33
#define HX9031AS_FORCE_NO_UP_CFG                0x35
#define HX9031AS_RAW_BL_RD_CFG                  0x38
#define HX9031AS_INTERRUPT_CFG                  0x39
#define HX9031AS_INTERRUPT_CFG1                 0x3A
#define HX9031AS_CALI_DIFF_CFG                  0x3B
#define HX9031AS_DITHER_CFG                     0x3C
#define HX9031AS_ANALOG_MEM0_WRDATA_7_0         0x40
#define HX9031AS_ANALOG_MEM0_WRDATA_15_8        0x41
#define HX9031AS_ANALOG_MEM0_WRDATA_23_16       0x42
#define HX9031AS_ANALOG_MEM0_WRDATA_31_24       0x43
#define HX9031AS_ANALOG_PWE_PULSE_CYCLE7_0      0x48
#define HX9031AS_ANALOG_PWE_PULSE_CYCLE12_8     0x49
#define HX9031AS_ANALOG_MEM_GLOBAL_CTRL         0x4A
#define HX9031AS_DEBUG_MEM_ADC_FSM              0x4B
#define HX9031AS_ANALOG_MEM_GLOBAL_CTRL1        0x4C
#define HX9031AS_VERION_ID                      0x5F
#define HX9031AS_DEVICE_ID                      0x60
#define HX9031AS_TC_FSM                         0x61
#define HX9031AS_FLAG_RD                        0x66
#define HX9031AS_CONV_TIMEOUT_CNT               0x6A
#define HX9031AS_PROX_STATUS                    0x6B
#define HX9031AS_PROX_INT_HIGH_CFG              0x6C
#define HX9031AS_PROX_INT_LOW_CFG               0x6D
#define HX9031AS_CAP_INI_CFG                    0x6E
#define HX9031AS_INT_WIDTH_CFG0                 0x6F
#define HX9031AS_INT_WIDTH_CFG1                 0x70
#define HX9031AS_INT_STATE_RD0                  0x71
#define HX9031AS_INT_STATE_RD1                  0x72
#define HX9031AS_INT_STATE_RD2                  0x73
#define HX9031AS_INT_STATE_RD3                  0x74
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH0_0       0x80
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH0_1       0x81
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH1_0       0x82
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH1_1       0x83
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH2_0       0x84
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH2_1       0x85
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH3_0       0x86
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH3_1       0x87
#define HX9031AS_PROX_LOW_DIFF_CFG_CH0_0        0x88
#define HX9031AS_PROX_LOW_DIFF_CFG_CH0_1        0x89
#define HX9031AS_PROX_LOW_DIFF_CFG_CH1_0        0x8A
#define HX9031AS_PROX_LOW_DIFF_CFG_CH1_1        0x8B
#define HX9031AS_PROX_LOW_DIFF_CFG_CH2_0        0x8C
#define HX9031AS_PROX_LOW_DIFF_CFG_CH2_1        0x8D
#define HX9031AS_PROX_LOW_DIFF_CFG_CH3_0        0x8E
#define HX9031AS_PROX_LOW_DIFF_CFG_CH3_1        0x8F
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH4_0       0x9E
#define HX9031AS_PROX_HIGH_DIFF_CFG_CH4_1       0x9F
#define HX9031AS_PROX_LOW_DIFF_CFG_CH4_0        0xA2
#define HX9031AS_PROX_LOW_DIFF_CFG_CH4_1        0xA3
#define HX9031AS_DSP_CONFIG_CTRL4               0x91
#define HX9031AS_DSP_CONFIG_CTRL6               0x93
#define HX9031AS_DSP_CONFIG_CTRL7               0x94
#define HX9031AS_DSP_CONFIG_CTRL8               0x95
#define HX9031AS_DSP_CONFIG_CTRL9               0x96
#define HX9031AS_DSP_CONFIG_CTRL10              0x97
#define HX9031AS_DSP_CONFIG_CTRL11              0x98
#define HX9031AS_LP_OUT_DELTA_THRES_CH1_CFG0    0xA0
#define HX9031AS_LP_OUT_DELTA_THRES_CH1_CFG1    0xA1
#define HX9031AS_LP_OUT_DELTA_THRES_CH3_CFG0    0xA4
#define HX9031AS_LP_OUT_DELTA_THRES_CH3_CFG1    0xA5
#define HX9031AS_LP_OUT_DELTA_THRES_CH4_CFG0    0xA6
#define HX9031AS_LP_OUT_DELTA_THRES_CH4_CFG1    0xA7
#define HX9031AS_PROX_THRES_SHIFT_CFG0          0xA8
#define HX9031AS_PROX_THRES_SHIFT_CFG1          0xA9
#define HX9031AS_PROX_THRES_SHIFT_CFG2          0xAA
#define HX9031AS_PROX_THRES_SHIFT_CFG3          0xAB
#define HX9031AS_PROX_THRES_SHIFT_CFG4          0xAC
#define HX9031AS_BL_IN_NO_UP_NUM_SEL0           0xAD
#define HX9031AS_BL_IN_NO_UP_NUM_SEL1           0xAE
#define HX9031AS_BL_IN_NO_UP_NUM_SEL2           0xAF
#define HX9031AS_BL_ALPHA_UP_DN_SEL             0xB2
#define HX9031AS_CH0_SAMP_CFG                   0xBF
#define HX9031AS_CH10_SCAN_FACTOR               0xC0
#define HX9031AS_CH32_SCAN_FACTOR               0xC1
#define HX9031AS_OFFSET_CALI_CTRL               0xC2
#define HX9031AS_OFFSET_CALI_CTRL1              0x90
#define HX9031AS_DSP_CONFIG_CTRL0               0xC3
#define HX9031AS_DSP_CONFIG_CTRL5               0x92
#define HX9031AS_CH10_DOZE_FACTOR               0xC4
#define HX9031AS_CH32_DOZE_FACTOR               0xC5
#define HX9031AS_CH10_PROX_FACTOR               0xC6
#define HX9031AS_CH4_FACTOR_CTRL                0xC7
#define HX9031AS_DSP_CONFIG_CTRL1               0xC8
#define HX9031AS_DSP_CONFIG_CTRL2               0xC9
#define HX9031AS_DSP_CONFIG_CTRL3               0xCA
#define HX9031AS_DEC_DATA0                      0xCB
#define HX9031AS_DEC_DATA1                      0xCC
#define HX9031AS_DEC_DATA2                      0xCD
#define HX9031AS_DEC_DATA3                      0xCE
#define HX9031AS_CAP_INI_CH0_0                  0xE0
#define HX9031AS_CAP_INI_CH0_1                  0xE1
#define HX9031AS_CAP_INI_CH0_2                  0x99
#define HX9031AS_CAP_INI_CH1_0                  0xE2
#define HX9031AS_CAP_INI_CH1_1                  0xE3
#define HX9031AS_CAP_INI_CH1_2                  0x9A
#define HX9031AS_CAP_INI_CH2_0                  0xE4
#define HX9031AS_CAP_INI_CH2_1                  0xE5
#define HX9031AS_CAP_INI_CH2_2                  0x9B
#define HX9031AS_CAP_INI_CH3_0                  0xE6
#define HX9031AS_CAP_INI_CH3_1                  0xE7
#define HX9031AS_CAP_INI_CH3_2                  0x9C
#define HX9031AS_CAP_INI_CH4_0                  0xB3
#define HX9031AS_CAP_INI_CH4_1                  0xB4
#define HX9031AS_CAP_INI_CH4_2                  0x9D
#define HX9031AS_RAW_BL_CH0_0                   0xE8
#define HX9031AS_RAW_BL_CH0_1                   0xE9
#define HX9031AS_RAW_BL_CH0_2                   0xEA
#define HX9031AS_RAW_BL_CH1_0                   0xEB
#define HX9031AS_RAW_BL_CH1_1                   0xEC
#define HX9031AS_RAW_BL_CH1_2                   0xED
#define HX9031AS_RAW_BL_CH2_0                   0xEE
#define HX9031AS_RAW_BL_CH2_1                   0xEF
#define HX9031AS_RAW_BL_CH2_2                   0xF0
#define HX9031AS_RAW_BL_CH3_0                   0xF1
#define HX9031AS_RAW_BL_CH3_1                   0xF2
#define HX9031AS_RAW_BL_CH3_2                   0xF3
#define HX9031AS_RAW_BL_CH4_0                   0xB5
#define HX9031AS_RAW_BL_CH4_1                   0xB6
#define HX9031AS_RAW_BL_CH4_2                   0xB7
#define HX9031AS_LP_DIFF_CH0_0                  0xF4
#define HX9031AS_LP_DIFF_CH0_1                  0xF5
#define HX9031AS_LP_DIFF_CH0_2                  0xF6
#define HX9031AS_LP_DIFF_CH1_0                  0xF7
#define HX9031AS_LP_DIFF_CH1_1                  0xF8
#define HX9031AS_LP_DIFF_CH1_2                  0xF9
#define HX9031AS_LP_DIFF_CH2_0                  0xFA
#define HX9031AS_LP_DIFF_CH2_1                  0xFB
#define HX9031AS_LP_DIFF_CH2_2                  0xFC
#define HX9031AS_LP_DIFF_CH3_0                  0xFD
#define HX9031AS_LP_DIFF_CH3_1                  0xFE
#define HX9031AS_LP_DIFF_CH3_2                  0xFF
#define HX9031AS_LP_DIFF_CH4_0                  0xB8
#define HX9031AS_LP_DIFF_CH4_1                  0xB9
#define HX9031AS_LP_DIFF_CH4_2                  0xBA
#define HX9031AS_REG_TO_ANA2                    0x50
#define HX9031AS_REG_TO_ANA3                    0x51
#define HX9031AS_REG_TO_ANA4                    0x52
#define HX9031AS_REG_TO_ANA5                    0x53
#define HX9031AS_REG_TO_ANA6                    0x82

struct hx9031as_threshold {
	int32_t near;
	int32_t far;
};

struct hx9031as_addr_val_pair {
	uint8_t addr;
	uint8_t val;
};

struct hx9031as_channel_info {
	char name[20];
	bool enabled;
	bool used;
	int state;
};

static struct hx9031as_addr_val_pair hx9031as_reg_init_list[] = {
	{ HX9031AS_CH_NUM_CFG,                 0x00 },
	{ HX9031AS_GLOBAL_CTRL0,               0x00 },
	{ HX9031AS_GLOBAL_CTRL2,               0x00 },

	{ HX9031AS_PRF_CFG,                    0x17 },
	{ HX9031AS_RANGE_7_0,                  0x11 },
	{ HX9031AS_RANGE_9_8,                  0x02 },
	{ HX9031AS_RANGE_18_16,                0x00 },

	{ HX9031AS_AVG0_NOSR0_CFG,             0x71 },
	{ HX9031AS_NOSR12_CFG,                 0x44 },
	{ HX9031AS_NOSR34_CFG,                 0x00 },
	{ HX9031AS_AVG12_CFG,                  0x33 },
	{ HX9031AS_AVG34_CFG,                  0x00 },

	{ HX9031AS_SAMPLE_NUM_7_0,             0x65 },
	{ HX9031AS_INTEGRATION_NUM_7_0,        0x65 },

	{ HX9031AS_LP_ALP_1_0_CFG,             0x22 },
	{ HX9031AS_LP_ALP_3_2_CFG,             0x22 },
	{ HX9031AS_LP_ALP_4_CFG,               0x02 },
	{ HX9031AS_UP_ALP_1_0_CFG,             0x88 },
	{ HX9031AS_UP_ALP_3_2_CFG,             0x88 },
	{ HX9031AS_DN_UP_ALP_0_4_CFG,          0x18 },
	{ HX9031AS_DN_ALP_2_1_CFG,             0x11 },
	{ HX9031AS_DN_ALP_4_3_CFG,             0x11 },

	{ HX9031AS_RAW_BL_RD_CFG,              0xF0 },
	{ HX9031AS_INTERRUPT_CFG,              0xFF },
	{ HX9031AS_INTERRUPT_CFG1,             0x3B },
	{ HX9031AS_CALI_DIFF_CFG,              0x07 },
	{ HX9031AS_DITHER_CFG,                 0x21 },
	{ HX9031AS_PROX_INT_HIGH_CFG,          0x01 },
	{ HX9031AS_PROX_INT_LOW_CFG,           0x01 },

	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH0_0,   0x40 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH0_1,   0x00 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH1_0,   0x40 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH1_1,   0x00 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH2_0,   0x40 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH2_1,   0x00 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH3_0,   0x40 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH3_1,   0x00 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH4_0,   0x40 },
	{ HX9031AS_PROX_HIGH_DIFF_CFG_CH4_1,   0x00 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH0_0,    0x20 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH0_1,    0x00 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH1_0,    0x20 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH1_1,    0x00 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH2_0,    0x20 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH2_1,    0x00 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH3_0,    0x20 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH3_1,    0x00 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH4_0,    0x20 },
	{ HX9031AS_PROX_LOW_DIFF_CFG_CH4_1,    0x00 },

	{ HX9031AS_PROX_THRES_SHIFT_CFG0,      0x00 },
	{ HX9031AS_PROX_THRES_SHIFT_CFG1,      0x00 },
	{ HX9031AS_PROX_THRES_SHIFT_CFG2,      0x00 },
	{ HX9031AS_PROX_THRES_SHIFT_CFG3,      0x00 },
	{ HX9031AS_PROX_THRES_SHIFT_CFG4,      0x00 },

	{ HX9031AS_CH10_SCAN_FACTOR,           0x00 },
	{ HX9031AS_CH32_SCAN_FACTOR,           0x00 },
	{ HX9031AS_CH10_DOZE_FACTOR,           0x00 },
	{ HX9031AS_CH32_DOZE_FACTOR,           0x00 },
	{ HX9031AS_CH4_FACTOR_CTRL,            0x00 },
	{ HX9031AS_DSP_CONFIG_CTRL1,           0x00 },
	{ HX9031AS_DSP_CONFIG_CTRL3,           0x00 },
};

struct hx9031as_data {
	struct mutex mutex;
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct regmap *regmap;
	unsigned long chan_prox_stat;
	bool trigger_enabled;
	struct {
		__be16 channels[HX9031AS_CH_NUM];

		s64 ts __aligned(8);

	} buffer;
	unsigned long chan_read;
	unsigned long chan_event; /*channel en bit*/

	struct hx9031as_threshold thres[HX9031AS_CH_NUM];
	struct hx9031as_channel_info *chs_info;
	uint32_t channel_used_flag;
	uint8_t ch_en_stat;
	int32_t raw[HX9031AS_CH_NUM];
	int32_t diff[HX9031AS_CH_NUM];
	int32_t lp[HX9031AS_CH_NUM];
	int32_t bl[HX9031AS_CH_NUM];
	uint16_t dac[HX9031AS_CH_NUM];
	bool sel_bl[HX9031AS_CH_NUM];
	bool sel_raw[HX9031AS_CH_NUM];
	bool sel_diff[HX9031AS_CH_NUM];
	bool sel_lp[HX9031AS_CH_NUM];
	uint8_t accuracy;
	uint32_t prox_state_reg;
};

static const struct iio_event_spec hx9031as_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

#define HX9031AS_CHANNEL(idx)				\
{								\
	.type = IIO_PROXIMITY,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
	.indexed = 1,						\
	.channel = idx,						\
	.address = 0,						\
	.event_spec = hx9031as_events,				\
	.num_event_specs = ARRAY_SIZE(hx9031as_events),		\
	.scan_index = idx,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_BE,				\
	},							\
}

static const struct iio_chan_spec hx9031as_channels[] = {
	HX9031AS_CHANNEL(0),
	HX9031AS_CHANNEL(1),
	HX9031AS_CHANNEL(2),
	HX9031AS_CHANNEL(3),
	HX9031AS_CHANNEL(4),
	IIO_CHAN_SOFT_TIMESTAMP(5),
};

static const uint32_t hx9031as_samp_freq_table[] = {
	2, 2, 4, 6, 8, 10, 14, 18, 22, 26,
	30, 34, 38, 42, 46, 50, 56, 62, 68, 74,
	80, 90, 100, 200, 300, 400, 600, 800, 1000, 2000,
	3000, 4000
};

static const struct regmap_range hx9031as_readable_reg_ranges[] = {
	regmap_reg_range(HX9031AS_DEVICE_ID, HX9031AS_DEVICE_ID),
	regmap_reg_range(HX9031AS_OFFSET_DAC0_7_0, HX9031AS_OFFSET_DAC4_9_8),
	regmap_reg_range(HX9031AS_RAW_BL_CH0_0, HX9031AS_RAW_BL_CH4_2),
	regmap_reg_range(HX9031AS_LP_DIFF_CH0_0, HX9031AS_LP_DIFF_CH4_2),
	regmap_reg_range(HX9031AS_PROX_STATUS, HX9031AS_PROX_STATUS),
	regmap_reg_range(HX9031AS_RAW_BL_RD_CFG, HX9031AS_RAW_BL_RD_CFG),
	regmap_reg_range(HX9031AS_INTERRUPT_CFG1, HX9031AS_INTERRUPT_CFG1),
	regmap_reg_range(HX9031AS_CH0_CFG_7_0, HX9031AS_CH4_CFG_9_8),
	regmap_reg_range(HX9031AS_PROX_HIGH_DIFF_CFG_CH4_0, HX9031AS_PROX_HIGH_DIFF_CFG_CH4_1),
	regmap_reg_range(HX9031AS_PROX_LOW_DIFF_CFG_CH4_0, HX9031AS_PROX_LOW_DIFF_CFG_CH4_1),
	regmap_reg_range(HX9031AS_PROX_HIGH_DIFF_CFG_CH0_0, HX9031AS_PROX_HIGH_DIFF_CFG_CH3_1),
	regmap_reg_range(HX9031AS_PROX_LOW_DIFF_CFG_CH0_0, HX9031AS_PROX_LOW_DIFF_CFG_CH3_1),
	regmap_reg_range(HX9031AS_CH_NUM_CFG, HX9031AS_CH_NUM_CFG),
	regmap_reg_range(HX9031AS_PRF_CFG, HX9031AS_PRF_CFG),
	regmap_reg_range(HX9031AS_DSP_CONFIG_CTRL1, HX9031AS_DSP_CONFIG_CTRL1),
};

static const struct regmap_access_table hx9031as_readable_regs = {
	.yes_ranges = hx9031as_readable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(hx9031as_readable_reg_ranges),
};

static const struct regmap_range hx9031as_writeable_reg_ranges[] = {
	regmap_reg_range(HX9031AS_RAW_BL_RD_CFG, HX9031AS_RAW_BL_RD_CFG),
	regmap_reg_range(HX9031AS_INTERRUPT_CFG1, HX9031AS_INTERRUPT_CFG1),
	regmap_reg_range(HX9031AS_CH0_CFG_7_0, HX9031AS_CH4_CFG_9_8),
	regmap_reg_range(HX9031AS_PROX_HIGH_DIFF_CFG_CH4_0, HX9031AS_PROX_HIGH_DIFF_CFG_CH4_1),
	regmap_reg_range(HX9031AS_PROX_LOW_DIFF_CFG_CH4_0, HX9031AS_PROX_LOW_DIFF_CFG_CH4_1),
	regmap_reg_range(HX9031AS_PROX_HIGH_DIFF_CFG_CH0_0, HX9031AS_PROX_HIGH_DIFF_CFG_CH3_1),
	regmap_reg_range(HX9031AS_PROX_LOW_DIFF_CFG_CH0_0, HX9031AS_PROX_LOW_DIFF_CFG_CH3_1),
	regmap_reg_range(HX9031AS_CH_NUM_CFG, HX9031AS_CH_NUM_CFG),
	regmap_reg_range(HX9031AS_PRF_CFG, HX9031AS_PRF_CFG),
	regmap_reg_range(HX9031AS_DSP_CONFIG_CTRL1, HX9031AS_DSP_CONFIG_CTRL1),
};

static const struct regmap_access_table hx9031as_writeable_regs = {
	.yes_ranges = hx9031as_writeable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(hx9031as_writeable_reg_ranges),
};

static const struct regmap_range hx9031as_volatile_reg_ranges[] = {
	regmap_reg_range(HX9031AS_DSP_CONFIG_CTRL1, HX9031AS_DSP_CONFIG_CTRL1),
};

static const struct regmap_access_table hx9031as_volatile_regs = {
	.yes_ranges = hx9031as_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(hx9031as_volatile_reg_ranges),
};

static const struct regmap_config hx9031as_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
	.wr_table = &hx9031as_writeable_regs,
	.rd_table = &hx9031as_readable_regs,
	.volatile_table = &hx9031as_volatile_regs,
};

static int hx9031as_data_lock(struct hx9031as_data *data, bool locked)
{
	int ret;

	if (locked) {
		ret = regmap_update_bits(data->regmap, HX9031AS_DSP_CONFIG_CTRL1, BIT(4), BIT(4));
		if (ret < 0) {
			dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
			return ret;
		}
	} else {
		ret = regmap_update_bits(data->regmap,
					HX9031AS_DSP_CONFIG_CTRL1,
					GENMASK(4, 3),
					0x00);
		if (ret < 0) {
			dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
			return ret;
		}
	}

	return ret;
}

static int hx9031as_get_id(struct hx9031as_data *data)
{
	int ret;
	uint32_t rxbuf[1];

	ret = regmap_read(data->regmap, HX9031AS_DEVICE_ID, rxbuf);
	if (ret < 0) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	dev_info(&data->client->dev, "id=0x%02X\n", rxbuf[0]);
	return 0;
}

static int hx9031as_ch_cfg(struct hx9031as_data *data)
{
	int ret;
	int i;
	uint16_t reg;
	uint8_t reg_list[HX9031AS_CH_NUM * 2];
	uint8_t ch_pos[HX9031AS_CH_NUM] = {CS0, CS1, CS2, CS3, CS4};
	uint8_t ch_neg[HX9031AS_CH_NUM] = {IGNORED, IGNORED, IGNORED, IGNORED, IGNORED};

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		reg = (uint16_t)((0x03 << ch_pos[i]) | (0x02 << ch_neg[i]));
		reg_list[i * 2] = (uint8_t)(reg);
		reg_list[i * 2 + 1] = (uint8_t)(reg >> 8);
	}

	ret = regmap_bulk_write(data->regmap, HX9031AS_CH0_CFG_7_0, reg_list, HX9031AS_CH_NUM * 2);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);

	return ret;
}

static int hx9031as_reg_init(struct hx9031as_data *data)
{
	int i = 0;
	int ret;

	while (i < (int)ARRAY_SIZE(hx9031as_reg_init_list)) {
		ret = regmap_bulk_write(data->regmap,
				hx9031as_reg_init_list[i].addr,
				&hx9031as_reg_init_list[i].val,
				1);
		if (ret) {
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
			return ret;
		}
		i++;
	}
	return ret;
}

static int32_t hx9031as_set_thres_near(struct hx9031as_data *data, uint8_t ch, int32_t val)
{
	int ret;
	uint8_t buf[2];

	val /= 32;
	buf[0] = val & 0xFF;
	buf[1] = (val >> 8) & 0x03;
	data->thres[ch].near = (val & 0x03FF) * 32;

	if (ch == 4) {
		ret = regmap_bulk_write(data->regmap, HX9031AS_PROX_HIGH_DIFF_CFG_CH4_0, buf, 2);
		if (ret)
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
	} else {
		ret = regmap_bulk_write(data->regmap,
					HX9031AS_PROX_HIGH_DIFF_CFG_CH0_0 + (ch * CH_DATA_2BYTES),
					buf,
					2);
		if (ret)
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
	}

	return ret;
}

static int32_t hx9031as_set_thres_far(struct hx9031as_data *data, uint8_t ch, int32_t val)
{
	int ret;
	uint8_t buf[2];

	val /= 32;
	buf[0] = val & 0xFF;
	buf[1] = (val >> 8) & 0x03;
	data->thres[ch].far = (val & 0x03FF) * 32;

	if (ch == 4) {
		ret = regmap_bulk_write(data->regmap, HX9031AS_PROX_LOW_DIFF_CFG_CH4_0, buf, 2);
		if (ret)
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
	} else {
		ret = regmap_bulk_write(data->regmap,
					HX9031AS_PROX_LOW_DIFF_CFG_CH0_0 + (ch * CH_DATA_2BYTES),
					buf,
					2);
		if (ret)
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
	}

	return ret;
}

static void hx9031as_get_prox_state(struct hx9031as_data *data)
{
	int ret;
	uint32_t buf[1];

	data->prox_state_reg = 0;
	ret = regmap_read(data->regmap, HX9031AS_PROX_STATUS, buf);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
	data->prox_state_reg = buf[0];
}

static void hx9031as_data_select(struct hx9031as_data *data)
{
	int ret;
	int i;
	uint32_t buf[1];

	ret = regmap_read(data->regmap, HX9031AS_RAW_BL_RD_CFG, buf);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);

	for (i = 0; i < 4; i++) {
		data->sel_diff[i] = CHK_BIT(buf[0], i);
		data->sel_lp[i] = !data->sel_diff[i];
		data->sel_bl[i] = CHK_BIT(buf[0], i + 4);
		data->sel_raw[i] = !data->sel_bl[i];
	}

	ret = regmap_read(data->regmap, HX9031AS_INTERRUPT_CFG1, buf);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);

	data->sel_diff[4] = CHK_BIT(buf[0], 2);
	data->sel_lp[4] = !data->sel_diff[4];
	data->sel_bl[4] = CHK_BIT(buf[0], 3);
	data->sel_raw[4] = !data->sel_bl[4];
}

static int hx9031as_sample(struct hx9031as_data *data)
{
	int ret;
	int i;
	uint8_t data_size;
	uint8_t offset_data_size;
	int32_t value;
	uint8_t rx_buf[HX9031AS_CH_NUM * CH_DATA_BYTES_MAX];

	hx9031as_data_lock(data, true);
	hx9031as_data_select(data);

	data_size = CH_DATA_3BYTES;

	/*ch0~ch3*/
	ret = regmap_bulk_read(data->regmap,
				HX9031AS_RAW_BL_CH0_0,
				rx_buf,
				(HX9031AS_CH_NUM * data_size) - data_size);
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	/*ch4*/
	ret = regmap_bulk_read(data->regmap,
				HX9031AS_RAW_BL_CH4_0,
				rx_buf + ((HX9031AS_CH_NUM * data_size) - data_size),
				data_size);
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		if (data->accuracy == 16) {
			value = get_unaligned_le16(&rx_buf[i * data_size + 1]);
			value = sign_extend32(value, 15);
		} else {
			value = get_unaligned_le24(&rx_buf[i * data_size]);
			value = sign_extend32(value, 23);
		}
		data->raw[i] = 0;
		data->bl[i] = 0;
		if (true == data->sel_raw[i])
			data->raw[i] = value;
		if (true == data->sel_bl[i])
			data->bl[i] = value;
	}

	/*ch0~ch3*/
	ret = regmap_bulk_read(data->regmap,
				HX9031AS_LP_DIFF_CH0_0,
				rx_buf,
				(HX9031AS_CH_NUM * data_size) - data_size);
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	/*ch4*/
	ret = regmap_bulk_read(data->regmap,
				HX9031AS_LP_DIFF_CH4_0,
				rx_buf + ((HX9031AS_CH_NUM * data_size) - data_size),
				data_size);
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		if (data->accuracy == 16) {
			value = get_unaligned_le16(&rx_buf[i * data_size + 1]);
			value = sign_extend32(value, 15);
		} else {
			value = get_unaligned_le24(&rx_buf[i * data_size]);
			value = sign_extend32(value, 23);
		}
		data->lp[i] = 0;
		data->diff[i] = 0;
		if (true == data->sel_lp[i])
			data->lp[i] = value;
		if (true == data->sel_diff[i])
			data->diff[i] = value;
	}

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		if (true == data->sel_lp[i] && true == data->sel_bl[i])
			data->diff[i] = data->lp[i] - data->bl[i];
	}

	/*offset dac*/
	offset_data_size = CH_DATA_2BYTES;
	ret = regmap_bulk_read(data->regmap,
				HX9031AS_OFFSET_DAC0_7_0,
				rx_buf,
				(HX9031AS_CH_NUM * offset_data_size));
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		value = get_unaligned_le16(&rx_buf[i * offset_data_size]);
		value = value & 0xFFF;
		data->dac[i] = value;
	}

	hx9031as_data_lock(data, false);
	return ret;
}

static int hx9031as_ch_en(struct hx9031as_data *data, uint8_t ch_id, bool en)
{
	int ret;
	uint32_t rx_buf[1];

	ret = regmap_read(data->regmap, HX9031AS_CH_NUM_CFG, rx_buf);
	if (ret) {
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);
		return ret;
	}
	data->ch_en_stat = rx_buf[0];

	if (en) {
		if (data->ch_en_stat == 0)
			data->prox_state_reg = 0;
		SET_BIT(data->ch_en_stat, ch_id);
		ret = regmap_bulk_write(data->regmap, HX9031AS_CH_NUM_CFG, &data->ch_en_stat, 1);
		if (ret) {
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
			return ret;
		}
		dev_info(&data->client->dev,
			"ch_en_stat=0x%02X (ch_%d enabled)\n",
			data->ch_en_stat,
			ch_id);
		TYHX_DELAY_MS(10);
	} else {
		CLR_BIT(data->ch_en_stat, ch_id);
		ret = regmap_bulk_write(data->regmap, HX9031AS_CH_NUM_CFG, &data->ch_en_stat, 1);
		if (ret) {
			dev_err(&data->client->dev, "[%s]i2c write failed\n", __func__);
			return ret;
		}
		dev_info(&data->client->dev,
			"ch_en_stat=0x%02X (ch_%d disabled)\n",
			data->ch_en_stat,
			ch_id);
	}
	return 0;
}

static int hx9031as_ch_en_hal(struct hx9031as_data *data, uint8_t ch_id, bool en)
{
	int ret;

	mutex_lock(&data->mutex);
	if (en) {
		dev_info(&data->client->dev,
			"enable ch_%d(name:%s)\n",
			ch_id,
			data->chs_info[ch_id].name);
		ret = hx9031as_ch_en(data, ch_id, en);
		if (ret) {
			dev_err(&data->client->dev, "channel enable failed\n");
			return ret;
		}
		data->chs_info[ch_id].state = 0;
		data->chs_info[ch_id].enabled = true;
	} else {
		dev_info(&data->client->dev,
			"disable ch_%d(name:%s)\n",
			ch_id,
			data->chs_info[ch_id].name);
		ret = hx9031as_ch_en(data, ch_id, en);
		if (ret) {
			dev_err(&data->client->dev, "channel enable failed\n");
			return ret;
		}
		data->chs_info[ch_id].state = 0;
		data->chs_info[ch_id].enabled = false;
	}
	mutex_unlock(&data->mutex);

	return 0;
}

static int hx9031as_update_chan_en(struct hx9031as_data *data,
				unsigned long chan_read,
				unsigned long chan_event)
{
	int i;
	unsigned long channels = chan_read | chan_event;

	if ((data->chan_read | data->chan_event) != channels) {
		for (i = 0; i < HX9031AS_CH_NUM; i++) {
			if ((data->channel_used_flag >> i) & 0x1) {
				if ((channels >> i) & 0x1)
					hx9031as_ch_en_hal(data, i, true);
				else
					hx9031as_ch_en_hal(data, i, false);
			}
		}
	}

	data->chan_read = chan_read;
	data->chan_event = chan_event;
	return 0;
}

static int hx9031as_get_proximity(struct hx9031as_data *data,
				const struct iio_chan_spec *chan,
				int *val)
{
	hx9031as_sample(data);
	hx9031as_get_prox_state(data);
	*val = data->diff[chan->channel];
	return IIO_VAL_INT;
}

static int hx9031as_get_samp_freq(struct hx9031as_data *data, int *val, int *val2)
{
	int ret;
	uint32_t odr;
	uint32_t buf[1];

	ret = regmap_read(data->regmap, HX9031AS_PRF_CFG, buf);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);

	odr = hx9031as_samp_freq_table[buf[0]];
	*val = 1000 / odr;
	*val2 = ((1000 % odr) * 1000000ULL) / odr;
	dev_info(&data->client->dev, "Period=%dms, Freq=%d.%dHz\n", odr, *val, *val2);

	return IIO_VAL_INT_PLUS_MICRO;
}

static int hx9031as_read_raw(struct iio_dev *indio_dev,
			const struct iio_chan_spec *chan,
			int *val,
			int *val2,
			long mask)
{
	struct hx9031as_data *data = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = hx9031as_get_proximity(data, chan, val);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return hx9031as_get_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int hx9031as_set_samp_freq(struct hx9031as_data *data, int val, int val2)
{
	int i;
	int ret;
	int period_ms;
	uint8_t buf[1];

	period_ms = div_u64(1000000000ULL, (val * 1000000ULL + val2));
	dev_info(&data->client->dev, "Freq=%d.%dHz, Period=%dms\n", val, val2, period_ms);

	for (i = 0; i < ARRAY_SIZE(hx9031as_samp_freq_table); i++) {
		if (period_ms == hx9031as_samp_freq_table[i]) {
			dev_info(&data->client->dev,
				"Period:%dms found! index=%d\n",
				period_ms,
				i);
			break;
		}
	}
	if (i == ARRAY_SIZE(hx9031as_samp_freq_table)) {
		dev_err(&data->client->dev, "Period:%dms NOT found!\n", period_ms);
		return -EINVAL;
	}

	buf[0] = i;
	ret = regmap_bulk_write(data->regmap, HX9031AS_PRF_CFG, &buf[0], 1);
	if (ret)
		dev_err(&data->client->dev, "[%s]i2c read failed\n", __func__);

	return ret;
}

static int hx9031as_write_raw(struct iio_dev *indio_dev,
			const struct iio_chan_spec *chan,
			int val,
			int val2,
			long mask)
{
	struct hx9031as_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	return hx9031as_set_samp_freq(data, val, val2);
}

static irqreturn_t hx9031as_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct hx9031as_data *data = iio_priv(indio_dev);

	if (data->trigger_enabled)
		iio_trigger_poll(data->trig);

	return IRQ_WAKE_THREAD;
}

static void hx9031as_push_events(struct iio_dev *indio_dev)
{
	struct hx9031as_data *data = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	unsigned long prox_changed;
	unsigned int chan;

	hx9031as_sample(data);
	hx9031as_get_prox_state(data);

	prox_changed = (data->chan_prox_stat ^ data->prox_state_reg) & data->chan_event;

	for_each_set_bit(chan, &prox_changed, HX9031AS_CH_NUM) {
		int dir;
		u64 ev;

		dir = (data->prox_state_reg & BIT(chan)) ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, chan, IIO_EV_TYPE_THRESH, dir);

		iio_push_event(indio_dev, ev, timestamp);
		dev_info(&data->client->dev,
			"chan=%d, dir=%d, prox_changed=0x%08lX, ev=0x%016llX\n",
			chan,
			dir,
			prox_changed,
			ev);
	}
	data->chan_prox_stat = data->prox_state_reg;
}

static irqreturn_t hx9031as_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct hx9031as_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	hx9031as_push_events(indio_dev);
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static int hx9031as_write_event_val(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info, int val, int val2)
{
	struct hx9031as_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return hx9031as_set_thres_far(data, chan->channel, val);
		case IIO_EV_DIR_FALLING:
			return hx9031as_set_thres_near(data, chan->channel, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int hx9031as_read_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct hx9031as_data *data = iio_priv(indio_dev);
	int en_state;

	en_state = !!(data->chan_event & BIT(chan->channel));
	dev_dbg(&data->client->dev,
		"chan_event=0x%016lX, ch%d=%d, en_state=%d\n",
		data->chan_event,
		chan->channel,
		data->chs_info[chan->channel].enabled,
		en_state);
	return en_state;
}

static int hx9031as_write_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				int state)
{
	struct hx9031as_data *data = iio_priv(indio_dev);

	if ((data->channel_used_flag >> chan->channel) & 0x1) {
		hx9031as_ch_en_hal(data, chan->channel, !!state);
		if (data->chs_info[chan->channel].enabled)
			data->chan_event = (data->chan_event | BIT(chan->channel));
		else
			data->chan_event = (data->chan_event & ~BIT(chan->channel));
	}
	return 0;
}

static const struct iio_info hx9031as_info = {
	.read_raw = hx9031as_read_raw,
	.write_raw = hx9031as_write_raw,
	.write_event_value = hx9031as_write_event_val,
	.read_event_config = hx9031as_read_event_config, /*get ch en flag*/
	.write_event_config = hx9031as_write_event_config, /*set ch en flag*/
};

static int hx9031as_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct hx9031as_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	if (state)
		enable_irq(data->client->irq);
	else if (!data->chan_read)
		disable_irq_nosync(data->client->irq);
	data->trigger_enabled = state;
	mutex_unlock(&data->mutex);
	return 0;
}

static const struct iio_trigger_ops hx9031as_trigger_ops = {
	.set_trigger_state = hx9031as_set_trigger_state,
};

static irqreturn_t hx9031as_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct hx9031as_data *data = iio_priv(indio_dev);
	__be16 val;
	int bit;
	int i = 0;

	mutex_lock(&data->mutex);
	hx9031as_sample(data);
	hx9031as_get_prox_state(data);

	for_each_set_bit(bit, indio_dev->active_scan_mask, indio_dev->masklength) {
		val = data->diff[indio_dev->channels[bit].channel];
		data->buffer.channels[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer, pf->timestamp);
	mutex_unlock(&data->mutex);

	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int hx9031as_buffer_preenable(struct iio_dev *indio_dev)
{
	struct hx9031as_data *data = iio_priv(indio_dev);
	unsigned long channels;
	int bit;

	mutex_lock(&data->mutex);
	for_each_set_bit(bit, indio_dev->active_scan_mask, indio_dev->masklength)
		__set_bit(indio_dev->channels[bit].channel, &channels);

	hx9031as_update_chan_en(data, channels, data->chan_event);
	mutex_unlock(&data->mutex);

	return 0;
}

static int hx9031as_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct hx9031as_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	hx9031as_update_chan_en(data, 0, data->chan_event);
	mutex_unlock(&data->mutex);
	return 0;
}

static const struct iio_buffer_setup_ops hx9031as_buffer_setup_ops = {
	.preenable = hx9031as_buffer_preenable,
	.postdisable = hx9031as_buffer_postdisable,
};

static int hx9031as_init_device(struct iio_dev *indio_dev)
{
	int ret;
	int i;
	struct hx9031as_data *data = iio_priv(indio_dev);

	ret = hx9031as_reg_init(data);
	if (ret)
		return ret;
	ret = hx9031as_ch_cfg(data);
	if (ret)
		return ret;
	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		hx9031as_set_thres_near(data, i, data->thres[i].near);
		hx9031as_set_thres_far(data, i, data->thres[i].far);
	}

	return ret;
}

static int hx9031as_probe(struct i2c_client *client)
{
	int ret = 0;
	int i;
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct hx9031as_data *data;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct hx9031as_data));
	if (!indio_dev) {
		ret = -ENOMEM;
		dev_err_probe(&client->dev, ret, "device alloc failed\n");
		return ret;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	data->ch_en_stat = 0x00;
	data->accuracy = 16;
	data->thres[0].near = 320;
	data->thres[0].far = 320;
	data->thres[1].near = 320;
	data->thres[1].far = 320;
	data->thres[2].near = 640;
	data->thres[2].far = 640;
	data->thres[3].near = 640;
	data->thres[3].far = 640;
	data->thres[4].near = 960;
	data->thres[4].far = 960;
	data->channel_used_flag = 0x1F;
	mutex_init(&data->mutex);

	data->chs_info = devm_kzalloc(&data->client->dev,
				sizeof(struct hx9031as_channel_info) * HX9031AS_CH_NUM,
				GFP_KERNEL);
	if (data->chs_info == NULL) {
		ret = -ENOMEM;
		dev_err_probe(&data->client->dev, ret, "channel info alloc failed\n");
		return ret;
	}

	for (i = 0; i < HX9031AS_CH_NUM; i++) {
		snprintf(data->chs_info[i].name,
			sizeof(data->chs_info[i].name),
			"hx9031as_ch%d",
			i);
		dev_dbg(&data->client->dev,
			"name of ch_%d:\"%s\"\n",
			i,
			data->chs_info[i].name);
		data->chs_info[i].used = false;
		data->chs_info[i].enabled = false;
		if ((data->channel_used_flag >> i) & 0x1) {
			data->chs_info[i].used = true;
			data->chs_info[i].state = 0;
		}
	}

	dev_info(&data->client->dev,
		"name=%s, addr=0x%02X, irq=%d\n",
		client->name,
		client->addr,
		client->irq);

	data->regmap = devm_regmap_init_i2c(client, &hx9031as_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err_probe(&data->client->dev, ret, "regmap init failed\n");
		return ret;
	}

	ret = devm_regulator_get_enable(&data->client->dev, "vdd");
	if (ret) {
		dev_err_probe(&data->client->dev, ret, "regulator get failed\n");
		return ret;
	}

	usleep_range(1000, 1100);

	ret = hx9031as_get_id(data);
	if (ret) {
		dev_err_probe(&data->client->dev, ret, "id check failed\n");
		return ret;
	}

	indio_dev->channels = hx9031as_channels;
	indio_dev->num_channels = ARRAY_SIZE(hx9031as_channels);
	indio_dev->info = &hx9031as_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = "hx9031as";
	i2c_set_clientdata(client, indio_dev);

	ret = hx9031as_init_device(indio_dev);
	if (ret) {
		dev_err_probe(&data->client->dev, ret, "device init failed\n");
		return ret;
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(dev,
						client->irq,
						hx9031as_irq_handler,
						hx9031as_irq_thread_handler,
						IRQF_ONESHOT,
						"hx9031as_event",
						indio_dev);
		if (ret) {
			dev_err_probe(&data->client->dev, ret, "irq request failed\n");
			return ret;
		}

		data->trig = devm_iio_trigger_alloc(dev,
						"%s-dev%d",
						indio_dev->name,
						iio_device_id(indio_dev));
		if (!data->trig) {
			ret = -ENOMEM;
			dev_err_probe(&data->client->dev, ret, "iio trigger alloc failed\n");
			return ret;
		}

		data->trig->dev.parent = dev;
		data->trig->ops = &hx9031as_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);

		ret = devm_iio_trigger_register(dev, data->trig);
		if (ret) {
			dev_err_probe(&data->client->dev, ret, "iio trigger register failed\n");
			return ret;
		}
	}

	ret = devm_iio_triggered_buffer_setup(dev,
					indio_dev,
					iio_pollfunc_store_time,
					hx9031as_trigger_handler,
					&hx9031as_buffer_setup_ops);
	if (ret) {
		dev_err_probe(&data->client->dev, ret, "iio triggered buffer setup failed\n");
		return ret;
	}

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err_probe(&data->client->dev, ret, "iio device register failed\n");
		return ret;
	}

	return ret;
}

static int hx9031as_suspend(struct device *dev)
{
	struct hx9031as_data *data = iio_priv(dev_get_drvdata(dev));

	disable_irq_nosync(data->client->irq);
	return 0;
}

static int hx9031as_resume(struct device *dev)
{
	struct hx9031as_data *data = iio_priv(dev_get_drvdata(dev));

	enable_irq(data->client->irq);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(hx9031as_pm_ops, hx9031as_suspend, hx9031as_resume);

static const struct acpi_device_id hx9031as_acpi_match[] = {
	{ .id = "TYHX9031", .driver_data = HX9031AS_CHIP_ID },
	{}
};
MODULE_DEVICE_TABLE(acpi, hx9031as_acpi_match);

static const struct of_device_id hx9031as_of_match[] = {
	{ .compatible = "tyhx,hx9031as", (void *)HX9031AS_CHIP_ID },
	{}
};
MODULE_DEVICE_TABLE(of, hx9031as_of_match);

static const struct i2c_device_id hx9031as_id[] = {
	{ .name = "hx9031as", .driver_data = HX9031AS_CHIP_ID },
	{}
};
MODULE_DEVICE_TABLE(i2c, hx9031as_id);

static struct i2c_driver hx9031as_driver = {
	.driver = {
		.name = "hx9031as",
		.acpi_match_table = hx9031as_acpi_match,
		.of_match_table = hx9031as_of_match,
		.pm = &hx9031as_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe_new = hx9031as_probe,
	.id_table = hx9031as_id,
};
module_i2c_driver(hx9031as_driver);

MODULE_AUTHOR("Yasin Lee <yasin.lee.x@gmail.com>");
MODULE_DESCRIPTION("Driver for TYHX HX9031AS/HX9023S SAR sensor");
MODULE_LICENSE("GPL");
