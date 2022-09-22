// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/skbuff.h>
#include <linux/wwan.h>

#include "t7xx_port.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"

#define T7XX_TRC_SUB_BUFF_SIZE		131072
#define T7XX_TRC_N_SUB_BUFF		32
#define T7XX_TRC_FILE_PERM              0600

static struct dentry *t7xx_trace_create_buf_file_handler(const char *filename,
							 struct dentry *parent,
							 umode_t mode,
							 struct rchan_buf *buf,
							 int *is_global)
{
	*is_global = 1;
	return debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
}

static int t7xx_trace_remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static int t7xx_trace_subbuf_start_handler(struct rchan_buf *buf, void *subbuf,
					   void *prev_subbuf,
					   size_t prev_padding)
{
	if (relay_buf_full(buf)) {
		pr_err_ratelimited("Relay_buf full dropping traces");
		return 0;
	}

	return 1;
}

static struct rchan_callbacks relay_callbacks = {
	.subbuf_start = t7xx_trace_subbuf_start_handler,
	.create_buf_file = t7xx_trace_create_buf_file_handler,
	.remove_buf_file = t7xx_trace_remove_buf_file_handler,
};

static ssize_t t7xx_port_trace_write(struct file *file, const char __user *buf,
				size_t len, loff_t *ppos)
{
	struct t7xx_port *port = file->private_data;
	size_t actual_len, alloc_size, txq_mtu;
	const struct t7xx_port_conf *port_conf;
	enum md_state md_state;
	struct sk_buff *skb;
	int ret;

	port_conf = port->port_conf;
	md_state = t7xx_fsm_get_md_state(port->t7xx_dev->md->fsm_ctl);
	if (md_state == MD_STATE_WAITING_FOR_HS1 || md_state == MD_STATE_WAITING_FOR_HS2) {
		dev_warn(port->dev, "port: %s ch: %d, write fail when md_state: %d\n",
				port_conf->name, port_conf->tx_ch, md_state);
		return -ENODEV;
	}
	txq_mtu = t7xx_get_port_mtu(port);
	alloc_size = min_t(size_t, txq_mtu, len + sizeof(struct ccci_header));
	actual_len = alloc_size - sizeof(struct ccci_header);
	skb = t7xx_port_alloc_skb(alloc_size);
	if (!skb) {
		ret = -ENOMEM;
		goto err_out;
	}

	ret = copy_from_user(skb_put(skb, actual_len), buf, actual_len);
	if (ret) {
		ret = -EFAULT;
		goto err_out;
	}

	ret = t7xx_port_send_skb(port, skb, 0, 0);
	if (ret)
		goto err_out;

	return actual_len;

err_out:
	dev_err(port->dev, "write error done on %s, size: %zu, ret: %d\n",
			port_conf->name, actual_len, ret);
	dev_kfree_skb(skb);
	return ret;
}

static const struct file_operations t7xx_trace_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = t7xx_port_trace_write,
};

static void t7xx_trace_port_uninit(struct t7xx_port *port)
{
	struct rchan *relaych = port->relaych;

	if (!relaych)
		return;

	relay_close(relaych);
	debugfs_remove_recursive(port->debugfs_dir);
	wwan_put_debugfs_dir(port->debugfs_wwan_dir);
}

static int t7xx_trace_port_recv_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	struct rchan *relaych = port->relaych;

	if (!relaych)
		return -EINVAL;

	relay_write(relaych, skb->data, skb->len);
	dev_kfree_skb(skb);
	return 0;
}

static void t7xx_port_trace_md_state_notify(struct t7xx_port *port, unsigned int state)
{
	struct rchan *relaych;

	struct dentry *ctrl_file;

	if (state != MD_STATE_READY || port->relaych)
		return;

	port->debugfs_wwan_dir = wwan_get_debugfs_dir(port->dev);
	if (IS_ERR(port->debugfs_wwan_dir))
		port->debugfs_wwan_dir = NULL;

	port->debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, port->debugfs_wwan_dir);
	if (IS_ERR_OR_NULL(port->debugfs_dir)) {
		dev_err(port->dev, "Unable to create debugfs for trace");
		return;
	}

	ctrl_file = devm_kzalloc(port->dev, sizeof(*ctrl_file), GFP_KERNEL);
	if (!ctrl_file)
		goto err_rm_debugfs_dir;

	ctrl_file = debugfs_create_file("mdlog_ctrl",
			T7XX_TRC_FILE_PERM,
			port->debugfs_dir,
			port,
			&t7xx_trace_fops);

	if (!ctrl_file)
		goto err_rm_trace;

	relaych = devm_kzalloc(port->dev, sizeof(*relaych), GFP_KERNEL);
	if (!relaych)
		goto err_rm_debugfs_dir;

	relaych = relay_open("relay_ch",
			     port->debugfs_dir,
			     T7XX_TRC_SUB_BUFF_SIZE,
			     T7XX_TRC_N_SUB_BUFF,
			     &relay_callbacks, NULL);
	if (!relaych)
		goto err_rm_trace;

	port->relaych = relaych;
	port->ctrl_file = ctrl_file;
	return;

err_rm_trace:
	kfree(relaych);
	kfree(ctrl_file);

err_rm_debugfs_dir:
	debugfs_remove_recursive(port->debugfs_dir);
	wwan_put_debugfs_dir(port->debugfs_wwan_dir);
	dev_err(port->dev, "Unable to create trace port %s", port->port_conf->name);
}

struct port_ops t7xx_trace_port_ops = {
	.recv_skb = t7xx_trace_port_recv_skb,
	.uninit = t7xx_trace_port_uninit,
	.md_state_notify = t7xx_port_trace_md_state_notify,
};
