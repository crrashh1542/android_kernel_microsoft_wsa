// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic gpio mux bridge driver
 *
 * Copyright 2016 Google LLC
 */

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

struct gpio_display_mux {
	struct device *dev;

	struct gpio_desc *gpiod_detect;
	int detect_irq;
	int cur_next;

	struct drm_bridge bridge;

	struct drm_bridge *next[2];
};

static inline struct gpio_display_mux *bridge_to_gpio_display_mux(
		struct drm_bridge *bridge)
{
	return container_of(bridge, struct gpio_display_mux, bridge);
}

static irqreturn_t gpio_display_mux_det_threaded_handler(int unused, void *data)
{
	struct gpio_display_mux *mux = data;
	int active = gpiod_get_value(mux->gpiod_detect);

	if (active < 0) {
		dev_err(mux->dev, "Failed to get detect GPIO\n");
		return IRQ_HANDLED;
	}

	dev_dbg(mux->dev, "Interrupt %d!\n", active);
	mux->cur_next = active;

	if (mux->bridge.dev)
		drm_kms_helper_hotplug_event(mux->bridge.dev);

	return IRQ_HANDLED;
}

static bool gpio_display_mux_mode_fixup(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct gpio_display_mux *mux = bridge_to_gpio_display_mux(bridge);
	struct drm_bridge *next;

	next = mux->next[mux->cur_next];

	/* Assume that we have a most one bridge in both downstreams */
	if (next && next->funcs->mode_fixup)
		return next->funcs->mode_fixup(next, mode, adjusted_mode);

	return true;
}

static const struct drm_bridge_funcs gpio_display_mux_bridge_funcs = {
	.mode_fixup = gpio_display_mux_mode_fixup,
};

static int gpio_display_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_display_mux *mux;
	struct device_node *port, *ep, *remote;
	int ret;
	u32 reg;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	platform_set_drvdata(pdev, mux);
	mux->dev = &pdev->dev;

	mux->bridge.of_node = dev->of_node;

	mux->gpiod_detect = devm_gpiod_get(dev, "detect", GPIOD_IN);
	if (IS_ERR(mux->gpiod_detect))
		return PTR_ERR(mux->gpiod_detect);

	mux->detect_irq = gpiod_to_irq(mux->gpiod_detect);
	if (mux->detect_irq < 0) {
		dev_err(dev, "Failed to get output irq %d\n",
			mux->detect_irq);
		return -ENODEV;
	}

	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (!port) {
		dev_err(dev, "Missing output port node\n");
		return -EINVAL;
	}

	for_each_child_of_node(port, ep) {
		if (!ep->name || (of_node_cmp(ep->name, "endpoint") != 0)) {
			of_node_put(ep);
			continue;
		}

		if (of_property_read_u32(ep, "reg", &reg) < 0 ||
		    reg >= ARRAY_SIZE(mux->next)) {
			dev_err(dev,
				"Missing/invalid reg property for endpoint %s\n",
				ep->full_name);
			of_node_put(ep);
			of_node_put(port);
			return -EINVAL;
		}

		remote = of_graph_get_remote_port_parent(ep);
		if (!remote) {
			dev_err(dev,
				"Missing connector/bridge node for endpoint %s\n",
				ep->full_name);
			of_node_put(ep);
			of_node_put(port);
			return -EINVAL;
		}

		mux->next[reg] = of_drm_find_bridge(remote);
		if (!mux->next[reg]) {
			dev_err(dev, "Waiting for external bridge %s\n",
				remote->name);
			of_node_put(ep);
			of_node_put(remote);
			of_node_put(port);
			return -EPROBE_DEFER;
		}

		of_node_put(remote);
	}
	of_node_put(port);

	mux->bridge.funcs = &gpio_display_mux_bridge_funcs;
	mux->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	drm_bridge_add(&mux->bridge);

	ret = devm_request_threaded_irq(dev, mux->detect_irq, NULL,
					gpio_display_mux_det_threaded_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"gpio-display-mux-det", mux);
	if (ret) {
		dev_err(dev, "Failed to request MUX_DET threaded irq\n");
		goto err_bridge_remove;
	}

	return 0;

err_bridge_remove:
	drm_bridge_remove(&mux->bridge);

	return ret;
}

static int gpio_display_mux_remove(struct platform_device *pdev)
{
	struct gpio_display_mux *mux = platform_get_drvdata(pdev);

	disable_irq(mux->detect_irq);
	drm_bridge_remove(&mux->bridge);

	return 0;
}

static const struct of_device_id gpio_display_mux_match[] = {
	{ .compatible = "gpio-display-mux", },
	{},
};

struct platform_driver gpio_display_mux_driver = {
	.probe = gpio_display_mux_probe,
	.remove = gpio_display_mux_remove,
	.driver = {
		.name = "gpio-display-mux",
		.of_match_table = gpio_display_mux_match,
	},
};

module_platform_driver(gpio_display_mux_driver);

MODULE_DESCRIPTION("GPIO-controlled display mux");
MODULE_AUTHOR("Nicolas Boichat <drinkcat@chromium.org>");
MODULE_LICENSE("GPL");
