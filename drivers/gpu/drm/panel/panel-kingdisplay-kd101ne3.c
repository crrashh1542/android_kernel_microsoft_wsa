// SPDX-License-Identifier: GPL-2.0
/*
 * Panels based on the JD9365DA display controller.
 * Author: Zhaoxiong Lv <lvzhaoxiong@huaqin.corp-partner.google.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
	bool discharge_on_disable;
	bool lp11_before_reset;
};

struct kingdisplay_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	const struct panel_desc *desc;

	enum drm_panel_orientation orientation;
	struct regulator *pp3300;
	struct gpio_desc *enable_gpio;
};

enum dsi_cmd_type {
	INIT_DCS_CMD,
	DELAY_CMD,
};

struct panel_init_cmd {
	enum dsi_cmd_type type;
	size_t len;
	const char *data;
};

#define _INIT_DCS_CMD(...) { \
	.type = INIT_DCS_CMD, \
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

#define _INIT_DELAY_CMD(...) { \
	.type = DELAY_CMD,\
	.len = sizeof((char[]){__VA_ARGS__}), \
	.data = (char[]){__VA_ARGS__} }

static const struct panel_init_cmd kingdisplay_kd101ne3_init_cmd[] = {
	_INIT_DELAY_CMD(50),
	_INIT_DCS_CMD(0xE0, 0x00),
	_INIT_DCS_CMD(0xE1, 0x93),
	_INIT_DCS_CMD(0xE2, 0x65),
	_INIT_DCS_CMD(0xE3, 0xF8),
	_INIT_DCS_CMD(0x80, 0x03),
	_INIT_DCS_CMD(0xE0, 0x01),
	_INIT_DCS_CMD(0x0C, 0x74),
	_INIT_DCS_CMD(0x17, 0x00),
	_INIT_DCS_CMD(0x18, 0xC7),
	_INIT_DCS_CMD(0x19, 0x01),
	_INIT_DCS_CMD(0x1A, 0x00),
	_INIT_DCS_CMD(0x1B, 0xC7),
	_INIT_DCS_CMD(0x1C, 0x01),
	_INIT_DCS_CMD(0x24, 0xFE),
	_INIT_DCS_CMD(0x37, 0x19),
	_INIT_DCS_CMD(0x35, 0x28),
	_INIT_DCS_CMD(0x38, 0x05),
	_INIT_DCS_CMD(0x39, 0x08),
	_INIT_DCS_CMD(0x3A, 0x12),
	_INIT_DCS_CMD(0x3C, 0x7E),
	_INIT_DCS_CMD(0x3D, 0xFF),
	_INIT_DCS_CMD(0x3E, 0xFF),
	_INIT_DCS_CMD(0x3F, 0x7F),
	_INIT_DCS_CMD(0x40, 0x06),
	_INIT_DCS_CMD(0x41, 0xA0),
	_INIT_DCS_CMD(0x43, 0x1E),
	_INIT_DCS_CMD(0x44, 0x0B),
	_INIT_DCS_CMD(0x55, 0x02),
	_INIT_DCS_CMD(0x57, 0x6A),
	_INIT_DCS_CMD(0x59, 0x0A),
	_INIT_DCS_CMD(0x5A, 0x2E),
	_INIT_DCS_CMD(0x5B, 0x1A),
	_INIT_DCS_CMD(0x5C, 0x15),
	_INIT_DCS_CMD(0x5D, 0x7F),
	_INIT_DCS_CMD(0x5E, 0x61),
	_INIT_DCS_CMD(0x5F, 0x50),
	_INIT_DCS_CMD(0x60, 0x43),
	_INIT_DCS_CMD(0x61, 0x3F),
	_INIT_DCS_CMD(0x62, 0x32),
	_INIT_DCS_CMD(0x63, 0x35),
	_INIT_DCS_CMD(0x64, 0x1F),
	_INIT_DCS_CMD(0x65, 0x38),
	_INIT_DCS_CMD(0x66, 0x36),
	_INIT_DCS_CMD(0x67, 0x36),
	_INIT_DCS_CMD(0x68, 0x54),
	_INIT_DCS_CMD(0x69, 0x42),
	_INIT_DCS_CMD(0x6A, 0x48),
	_INIT_DCS_CMD(0x6B, 0x39),
	_INIT_DCS_CMD(0x6C, 0x34),
	_INIT_DCS_CMD(0x6D, 0x26),
	_INIT_DCS_CMD(0x6E, 0x14),
	_INIT_DCS_CMD(0x6F, 0x02),
	_INIT_DCS_CMD(0x70, 0x7F),
	_INIT_DCS_CMD(0x71, 0x61),
	_INIT_DCS_CMD(0x72, 0x50),
	_INIT_DCS_CMD(0x73, 0x43),
	_INIT_DCS_CMD(0x74, 0x3F),
	_INIT_DCS_CMD(0x75, 0x32),
	_INIT_DCS_CMD(0x76, 0x35),
	_INIT_DCS_CMD(0x77, 0x1F),
	_INIT_DCS_CMD(0x78, 0x38),
	_INIT_DCS_CMD(0x79, 0x36),
	_INIT_DCS_CMD(0x7A, 0x36),
	_INIT_DCS_CMD(0x7B, 0x54),
	_INIT_DCS_CMD(0x7C, 0x42),
	_INIT_DCS_CMD(0x7D, 0x48),
	_INIT_DCS_CMD(0x7E, 0x39),
	_INIT_DCS_CMD(0x7F, 0x34),
	_INIT_DCS_CMD(0x80, 0x26),
	_INIT_DCS_CMD(0x81, 0x14),
	_INIT_DCS_CMD(0x82, 0x02),
	_INIT_DCS_CMD(0xE0, 0x02),
	_INIT_DCS_CMD(0x00, 0x52),
	_INIT_DCS_CMD(0x01, 0x5F),
	_INIT_DCS_CMD(0x02, 0x5F),
	_INIT_DCS_CMD(0x03, 0x50),
	_INIT_DCS_CMD(0x04, 0x77),
	_INIT_DCS_CMD(0x05, 0x57),
	_INIT_DCS_CMD(0x06, 0x5F),
	_INIT_DCS_CMD(0x07, 0x4E),
	_INIT_DCS_CMD(0x08, 0x4C),
	_INIT_DCS_CMD(0x09, 0x5F),
	_INIT_DCS_CMD(0x0A, 0x4A),
	_INIT_DCS_CMD(0x0B, 0x48),
	_INIT_DCS_CMD(0x0C, 0x5F),
	_INIT_DCS_CMD(0x0D, 0x46),
	_INIT_DCS_CMD(0x0E, 0x44),
	_INIT_DCS_CMD(0x0F, 0x40),
	_INIT_DCS_CMD(0x10, 0x5F),
	_INIT_DCS_CMD(0x11, 0x5F),
	_INIT_DCS_CMD(0x12, 0x5F),
	_INIT_DCS_CMD(0x13, 0x5F),
	_INIT_DCS_CMD(0x14, 0x5F),
	_INIT_DCS_CMD(0x15, 0x5F),
	_INIT_DCS_CMD(0x16, 0x53),
	_INIT_DCS_CMD(0x17, 0x5F),
	_INIT_DCS_CMD(0x18, 0x5F),
	_INIT_DCS_CMD(0x19, 0x51),
	_INIT_DCS_CMD(0x1A, 0x77),
	_INIT_DCS_CMD(0x1B, 0x57),
	_INIT_DCS_CMD(0x1C, 0x5F),
	_INIT_DCS_CMD(0x1D, 0x4F),
	_INIT_DCS_CMD(0x1E, 0x4D),
	_INIT_DCS_CMD(0x1F, 0x5F),
	_INIT_DCS_CMD(0x20, 0x4B),
	_INIT_DCS_CMD(0x21, 0x49),
	_INIT_DCS_CMD(0x22, 0x5F),
	_INIT_DCS_CMD(0x23, 0x47),
	_INIT_DCS_CMD(0x24, 0x45),
	_INIT_DCS_CMD(0x25, 0x41),
	_INIT_DCS_CMD(0x26, 0x5F),
	_INIT_DCS_CMD(0x27, 0x5F),
	_INIT_DCS_CMD(0x28, 0x5F),
	_INIT_DCS_CMD(0x29, 0x5F),
	_INIT_DCS_CMD(0x2A, 0x5F),
	_INIT_DCS_CMD(0x2B, 0x5F),
	_INIT_DCS_CMD(0x2C, 0x13),
	_INIT_DCS_CMD(0x2D, 0x1F),
	_INIT_DCS_CMD(0x2E, 0x1F),
	_INIT_DCS_CMD(0x2F, 0x01),
	_INIT_DCS_CMD(0x30, 0x17),
	_INIT_DCS_CMD(0x31, 0x17),
	_INIT_DCS_CMD(0x32, 0x1F),
	_INIT_DCS_CMD(0x33, 0x0D),
	_INIT_DCS_CMD(0x34, 0x0F),
	_INIT_DCS_CMD(0x35, 0x1F),
	_INIT_DCS_CMD(0x36, 0x05),
	_INIT_DCS_CMD(0x37, 0x07),
	_INIT_DCS_CMD(0x38, 0x1F),
	_INIT_DCS_CMD(0x39, 0x09),
	_INIT_DCS_CMD(0x3A, 0x0B),
	_INIT_DCS_CMD(0x3B, 0x11),
	_INIT_DCS_CMD(0x3C, 0x1F),
	_INIT_DCS_CMD(0x3D, 0x1F),
	_INIT_DCS_CMD(0x3E, 0x1F),
	_INIT_DCS_CMD(0x3F, 0x1F),
	_INIT_DCS_CMD(0x40, 0x1F),
	_INIT_DCS_CMD(0x41, 0x1F),
	_INIT_DCS_CMD(0x42, 0x12),
	_INIT_DCS_CMD(0x43, 0x1F),
	_INIT_DCS_CMD(0x44, 0x1F),
	_INIT_DCS_CMD(0x45, 0x00),
	_INIT_DCS_CMD(0x46, 0x17),
	_INIT_DCS_CMD(0x47, 0x17),
	_INIT_DCS_CMD(0x48, 0x1F),
	_INIT_DCS_CMD(0x49, 0x0C),
	_INIT_DCS_CMD(0x4A, 0x0E),
	_INIT_DCS_CMD(0x4B, 0x1F),
	_INIT_DCS_CMD(0x4C, 0x04),
	_INIT_DCS_CMD(0x4D, 0x06),
	_INIT_DCS_CMD(0x4E, 0x1F),
	_INIT_DCS_CMD(0x4F, 0x08),
	_INIT_DCS_CMD(0x50, 0x0A),
	_INIT_DCS_CMD(0x51, 0x10),
	_INIT_DCS_CMD(0x52, 0x1F),
	_INIT_DCS_CMD(0x53, 0x1F),
	_INIT_DCS_CMD(0x54, 0x1F),
	_INIT_DCS_CMD(0x55, 0x1F),
	_INIT_DCS_CMD(0x56, 0x1F),
	_INIT_DCS_CMD(0x57, 0x1F),
	_INIT_DCS_CMD(0x58, 0x40),
	_INIT_DCS_CMD(0x5B, 0x10),
	_INIT_DCS_CMD(0x5C, 0x06),
	_INIT_DCS_CMD(0x5D, 0x40),
	_INIT_DCS_CMD(0x5E, 0x00),
	_INIT_DCS_CMD(0x5F, 0x00),
	_INIT_DCS_CMD(0x60, 0x40),
	_INIT_DCS_CMD(0x61, 0x03),
	_INIT_DCS_CMD(0x62, 0x04),
	_INIT_DCS_CMD(0x63, 0x6C),
	_INIT_DCS_CMD(0x64, 0x6C),
	_INIT_DCS_CMD(0x65, 0x75),
	_INIT_DCS_CMD(0x66, 0x08),
	_INIT_DCS_CMD(0x67, 0xB4),
	_INIT_DCS_CMD(0x68, 0x08),
	_INIT_DCS_CMD(0x69, 0x6C),
	_INIT_DCS_CMD(0x6A, 0x6C),
	_INIT_DCS_CMD(0x6B, 0x0C),
	_INIT_DCS_CMD(0x6D, 0x00),
	_INIT_DCS_CMD(0x6E, 0x00),
	_INIT_DCS_CMD(0x6F, 0x88),
	_INIT_DCS_CMD(0x75, 0xBB),
	_INIT_DCS_CMD(0x76, 0x00),
	_INIT_DCS_CMD(0x77, 0x05),
	_INIT_DCS_CMD(0x78, 0x2A),
	_INIT_DCS_CMD(0xE0, 0x04),
	_INIT_DCS_CMD(0x00, 0x0E),
	_INIT_DCS_CMD(0x02, 0xB3),
	_INIT_DCS_CMD(0x09, 0x61),
	_INIT_DCS_CMD(0x0E, 0x48),

	_INIT_DCS_CMD(0xE0, 0x00),
	_INIT_DCS_CMD(0X11),
	/* T6: 120ms */
	_INIT_DELAY_CMD(120),
	_INIT_DCS_CMD(0X29),
	_INIT_DELAY_CMD(20),
	{},
};

static inline struct kingdisplay_panel *to_kingdisplay_panel(struct drm_panel *panel)
{
	return container_of(panel, struct kingdisplay_panel, base);
}

static int kingdisplay_panel_init_dcs_cmd(struct kingdisplay_panel *kingdisplay)
{
	struct mipi_dsi_device *dsi = kingdisplay->dsi;
	struct drm_panel *panel = &kingdisplay->base;
	int i, err = 0;

	if (kingdisplay->desc->init_cmds) {
		const struct panel_init_cmd *init_cmds = kingdisplay->desc->init_cmds;

		for (i = 0; init_cmds[i].len != 0; i++) {
			const struct panel_init_cmd *cmd = &init_cmds[i];

			switch (cmd->type) {
			case DELAY_CMD:
				msleep(cmd->data[0]);
				err = 0;
				break;

			case INIT_DCS_CMD:
				err = mipi_dsi_dcs_write(dsi, cmd->data[0],
							 cmd->len <= 1 ? NULL :
							 &cmd->data[1],
							 cmd->len - 1);
				break;

			default:
				err = -EINVAL;
			}

			if (err < 0) {
				dev_err(panel->dev,
					"failed to write command %u\n", i);
				return err;
			}
		}
	}
	return 0;
}

static int kingdisplay_panel_enter_sleep_mode(struct kingdisplay_panel *kingdisplay)
{
	struct mipi_dsi_device *dsi = kingdisplay->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	usleep_range(1000, 2000);
	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	return 0;
}

static int kingdisplay_panel_disable(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int ret;

	ret = kingdisplay_panel_enter_sleep_mode(kingdisplay);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	msleep(100);

	return 0;
}

static int kingdisplay_panel_unprepare(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int err;

	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 0);

	/* T15: 2ms */
	usleep_range(1000, 2000);

	err = regulator_disable(kingdisplay->pp3300);
	if (err < 0)
		return err;

	return 0;
}

static int kingdisplay_panel_prepare(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int ret;

	gpiod_set_value(kingdisplay->enable_gpio, 0);

	ret = regulator_enable(kingdisplay->pp3300);
	if (ret < 0)
		return ret;

	/* T1: 5ms */
	usleep_range(5000, 6000);

	if (kingdisplay->desc->lp11_before_reset) {
		mipi_dsi_dcs_nop(kingdisplay->dsi);
		usleep_range(1000, 2000);
	}

	/* T2: 10ms, T1 + T2 > 5ms*/
	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 1);

	ret = kingdisplay_panel_init_dcs_cmd(kingdisplay);
	if (ret < 0) {
		dev_err(panel->dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	return 0;

poweroff:
	regulator_disable(kingdisplay->pp3300);
		/* T6: 2ms */
	usleep_range(1000, 2000);
	gpiod_set_value(kingdisplay->enable_gpio, 0);

	return ret;
}

static int kingdisplay_panel_enable(struct drm_panel *panel)
{
	msleep(130);
	return 0;
}

static const struct drm_display_mode kingdisplay_kd101ne3_40ti_default_mode = {
	.clock = 70595,
	.hdisplay = 800,
	.hsync_start = 800 + 30,
	.hsync_end = 800 + 30 + 30,
	.htotal = 800 + 30 + 30 + 30,
	.vdisplay = 1280,
	.vsync_start = 1280 + 30,
	.vsync_end = 1280 + 30 + 4,
	.vtotal = 1280 + 30 + 4 + 8,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc kingdisplay_kd101ne3_40ti_desc = {
	.modes = &kingdisplay_kd101ne3_40ti_default_mode,
	.bpc = 8,
	.size = {
		.width_mm = 135,
		.height_mm = 216,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init_cmds = kingdisplay_kd101ne3_init_cmd,
	.lp11_before_reset = true,
};

static int kingdisplay_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	const struct drm_display_mode *m = kingdisplay->desc->modes;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = kingdisplay->desc->size.width_mm;
	connector->display_info.height_mm = kingdisplay->desc->size.height_mm;
	connector->display_info.bpc = kingdisplay->desc->bpc;
	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, kingdisplay->orientation);

	return 1;
}

static enum drm_panel_orientation kingdisplay_panel_get_orientation(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);

	return kingdisplay->orientation;
}

static const struct drm_panel_funcs kingdisplay_panel_funcs = {
	.disable = kingdisplay_panel_disable,
	.unprepare = kingdisplay_panel_unprepare,
	.prepare = kingdisplay_panel_prepare,
	.enable = kingdisplay_panel_enable,
	.get_modes = kingdisplay_panel_get_modes,
	.get_orientation = kingdisplay_panel_get_orientation,
};

static int kingdisplay_panel_add(struct kingdisplay_panel *kingdisplay)
{
	struct device *dev = &kingdisplay->dsi->dev;
	int err;

	kingdisplay->pp3300 = devm_regulator_get(dev, "pp3300");
	if (IS_ERR(kingdisplay->pp3300))
		return PTR_ERR(kingdisplay->pp3300);


	kingdisplay->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(kingdisplay->enable_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(kingdisplay->enable_gpio));
		return PTR_ERR(kingdisplay->enable_gpio);
	}

	gpiod_set_value(kingdisplay->enable_gpio, 0);

	drm_panel_init(&kingdisplay->base, dev, &kingdisplay_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	err = of_drm_get_panel_orientation(dev->of_node, &kingdisplay->orientation);
	if (err < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	err = drm_panel_of_backlight(&kingdisplay->base);
	if (err)
		return err;

	kingdisplay->base.funcs = &kingdisplay_panel_funcs;
	kingdisplay->base.dev = &kingdisplay->dsi->dev;

	drm_panel_add(&kingdisplay->base);

	return 0;
}

static int kingdisplay_panel_probe(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay;
	int ret;
	const struct panel_desc *desc;

	kingdisplay = devm_kzalloc(&dsi->dev, sizeof(*kingdisplay), GFP_KERNEL);
	if (!kingdisplay)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	kingdisplay->desc = desc;
	kingdisplay->dsi = dsi;
	ret = kingdisplay_panel_add(kingdisplay);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, kingdisplay);

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&kingdisplay->base);

	return ret;
}

static void kingdisplay_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&kingdisplay->base);
	drm_panel_unprepare(&kingdisplay->base);
}

static void kingdisplay_panel_remove(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay = mipi_dsi_get_drvdata(dsi);
	int ret;

	kingdisplay_panel_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	if (kingdisplay->base.dev)
		drm_panel_remove(&kingdisplay->base);
}

static const struct of_device_id kingdisplay_of_match[] = {
	{ .compatible = "kingdisplay,kd101ne3-40ti",
	  .data = &kingdisplay_kd101ne3_40ti_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, kingdisplay_of_match);

static struct mipi_dsi_driver kingdisplay_panel_driver = {
	.driver = {
		.name = "panel-kingdisplay-kd101ne3",
		.of_match_table = kingdisplay_of_match,
	},
	.probe = kingdisplay_panel_probe,
	.remove = kingdisplay_panel_remove,
	.shutdown = kingdisplay_panel_shutdown,
};
module_mipi_dsi_driver(kingdisplay_panel_driver);

MODULE_AUTHOR("Zhaoxiong Lv <lvzhaoxiong@huaqin.corp-partner.google.com>");
MODULE_DESCRIPTION("kingdisplay kd101ne3-40ti 800x1280 video mode panel driver");
MODULE_LICENSE("GPL v2");
