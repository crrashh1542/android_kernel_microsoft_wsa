// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/psp-sev.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <asm/amd_nb.h>
#include <asm/msr.h>

#include "i2c-designware-core.h"

#define PSP_CMD_TIMEOUT_US	(500 * USEC_PER_MSEC)

#define PSP_I2C_RESERVATION_TIME_MS 100

#define PSP_I2C_REQ_BUS_CMD		0x64
#define PSP_I2C_REQ_RETRY_CNT		400
#define PSP_I2C_REQ_RETRY_DELAY_US	(25 * USEC_PER_MSEC)
#define PSP_I2C_REQ_STS_OK		0x0
#define PSP_I2C_REQ_STS_BUS_BUSY	0x1
#define PSP_I2C_REQ_STS_INV_PARAM	0x3

#define PSP_MBOX_FIELDS_STS		GENMASK(15, 0)
#define PSP_MBOX_FIELDS_CMD		GENMASK(23, 16)
#define PSP_MBOX_FIELDS_RESERVED	GENMASK(29, 24)
#define PSP_MBOX_FIELDS_RECOVERY	BIT(30)
#define PSP_MBOX_FIELDS_READY		BIT(31)

#define PSP_MBOX_CMD_OFFSET		0x3810570
#define PSP_MBOX_BUFFER_L_OFFSET	0x3810574
#define PSP_MBOX_BUFFER_H_OFFSET	0x3810578

struct psp_req_buffer_hdr {
	u32 total_size;
	u32 status;
};

enum psp_i2c_req_type {
	PSP_I2C_REQ_ACQUIRE,
	PSP_I2C_REQ_RELEASE,
	PSP_I2C_REQ_MAX
};

struct psp_i2c_req {
	struct psp_req_buffer_hdr hdr;
	enum psp_i2c_req_type type;
};

static DEFINE_MUTEX(psp_i2c_access_mutex);
static unsigned long psp_i2c_sem_acquired;
static u32 psp_i2c_access_count;
static bool psp_i2c_mbox_fail;
static struct device *psp_i2c_dev;

/*
 * Implementation of PSP-x86 i2c-arbitration mailbox introduced for AMD Cezanne
 * family of SoCs.
 */

static int psp_mbox_probe(void)
{
	/*
	 * Explicitly initialize system management network interface here, since
	 * usual init happens only after PCI subsystem is ready. This is too late
	 * for I2C controller driver which may be executed earlier.
	 */
	return amd_cache_northbridges();
}

static int psp_smn_write(u32 smn_addr, u32 value)
{
	return amd_smn_write(0, smn_addr, value);
}

static int psp_smn_read(u32 smn_addr, u32 *value)
{
	return amd_smn_read(0, smn_addr, value);
}

/* Recovery field should be equal 0 to start sending commands */
static int psp_check_mbox_recovery(void)
{
	u32 tmp;
	int status;

	status = psp_smn_read(PSP_MBOX_CMD_OFFSET, &tmp);
	if (status)
		return status;

	return FIELD_GET(PSP_MBOX_FIELDS_RECOVERY, tmp);
}

static int psp_wait_cmd(void)
{
	u32 tmp, expected;
	int ret, status;

	/* Expect mbox_cmd to be cleared and ready bit to be set by PSP */
	expected = FIELD_PREP(PSP_MBOX_FIELDS_READY, 1);

	/*
	 * Check for readiness of PSP mailbox in a tight loop in order to
	 * process further as soon as command was consumed.
	 */
	ret = read_poll_timeout(psp_smn_read, status,
				(status < 0) || (tmp == expected), 0,
				PSP_CMD_TIMEOUT_US, 0, PSP_MBOX_CMD_OFFSET,
				&tmp);
	if (status)
		ret = status;

	return ret;
}

/* Status equal to 0 means that PSP succeed processing command */
static int psp_check_mbox_sts(void)
{
	u32 cmd_reg;
	int status;

	status = psp_smn_read(PSP_MBOX_CMD_OFFSET, &cmd_reg);
	if (status)
		return status;

	return FIELD_GET(PSP_MBOX_FIELDS_STS, cmd_reg);
}

static int psp_wr_mbox_buffer(phys_addr_t buf)
{
	u32 buf_addr_h = upper_32_bits(buf);
	u32 buf_addr_l = lower_32_bits(buf);
	int status;

	status = psp_smn_write(PSP_MBOX_BUFFER_H_OFFSET, buf_addr_h);
	if (status)
		return status;

	status = psp_smn_write(PSP_MBOX_BUFFER_L_OFFSET, buf_addr_l);
	if (status)
		return status;

	return 0;
}

static int psp_send_cmd(struct psp_i2c_req *req)
{
	phys_addr_t req_addr;
	u32 cmd_reg;

	if (psp_check_mbox_recovery())
		return -EIO;

	if (psp_wait_cmd())
		return -EBUSY;

	/*
	 * Fill mailbox with address of command-response buffer, which will be
	 * used for sending i2c requests as well as reading status returned by
	 * PSP. Use physical address of buffer, since PSP will map this region.
	 */
	req_addr = __psp_pa((void *)req);
	if (psp_wr_mbox_buffer(req_addr))
		return -EIO;

	/* Write command register to trigger processing */
	cmd_reg = FIELD_PREP(PSP_MBOX_FIELDS_CMD, PSP_I2C_REQ_BUS_CMD);
	if (psp_smn_write(PSP_MBOX_CMD_OFFSET, cmd_reg))
		return -EIO;

	if (psp_wait_cmd())
		return -ETIMEDOUT;

	if (psp_check_mbox_sts())
		return -EIO;

	return 0;
}

/* Helper to verify status returned by PSP */
static int check_i2c_req_sts(struct psp_i2c_req *req)
{
	u32 status;

	/* Status field in command-response buffer is updated by PSP */
	status = READ_ONCE(req->hdr.status);

	switch (status) {
	case PSP_I2C_REQ_STS_OK:
		return 0;
	case PSP_I2C_REQ_STS_BUS_BUSY:
		return -EBUSY;
	case PSP_I2C_REQ_STS_INV_PARAM:
	default:
		return -EIO;
	}
}

static int psp_send_check_i2c_req(struct psp_i2c_req *req)
{
	/*
	 * Errors in x86-PSP i2c-arbitration protocol may occur at two levels:
	 * 1. mailbox communication - PSP is not operational or some IO errors
	 * with basic communication had happened;
	 * 2. i2c-requests - PSP refuses to grant i2c arbitration to x86 for too
	 * long.
	 * In order to distinguish between these two in error handling code, all
	 * errors on the first level (returned by psp_send_cmd) are shadowed by
	 * -EIO.
	 */
	if (psp_send_cmd(req))
		return -EIO;

	return check_i2c_req_sts(req);
}

static int psp_send_i2c_req(enum psp_i2c_req_type i2c_req_type)
{
	struct psp_i2c_req *req;
	unsigned long start;
	int status, ret;

	/* Allocate command-response buffer */
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->hdr.total_size = sizeof(*req);
	req->type = i2c_req_type;

	start = jiffies;
	ret = read_poll_timeout(psp_send_check_i2c_req, status,
				(status != -EBUSY),
				PSP_I2C_REQ_RETRY_DELAY_US,
				PSP_I2C_REQ_RETRY_CNT * PSP_I2C_REQ_RETRY_DELAY_US,
				0, req);
	if (ret) {
		dev_err(psp_i2c_dev, "Timed out waiting for PSP to %s I2C bus\n",
			(i2c_req_type == PSP_I2C_REQ_ACQUIRE) ?
			"release" : "acquire");
		goto cleanup;
	}

	ret = status;
	if (ret) {
		dev_err(psp_i2c_dev, "PSP communication error\n");
		goto cleanup;
	}

	dev_dbg(psp_i2c_dev, "Request accepted by PSP after %ums\n",
		jiffies_to_msecs(jiffies - start));

cleanup:
	if (ret) {
		dev_err(psp_i2c_dev, "Assume i2c bus is for exclusive host usage\n");
		psp_i2c_mbox_fail = true;
	}

	kfree(req);
	return ret;
}

static void release_bus(void)
{
	int status;

	if (!psp_i2c_sem_acquired)
		return;

	status = psp_send_i2c_req(PSP_I2C_REQ_RELEASE);
	if (status)
		return;

	dev_dbg(psp_i2c_dev, "PSP semaphore held for %ums\n",
		jiffies_to_msecs(jiffies - psp_i2c_sem_acquired));

	psp_i2c_sem_acquired = 0;
}

static void psp_release_i2c_bus_deferred(struct work_struct *work)
{
	mutex_lock(&psp_i2c_access_mutex);

	/*
	 * If there is any pending transaction, cannot release the bus here.
	 * psp_release_i2c_bus will take care of this later.
	 */
	if (psp_i2c_access_count)
		goto cleanup;

	release_bus();

cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
}
static DECLARE_DELAYED_WORK(release_queue, psp_release_i2c_bus_deferred);

static int psp_acquire_i2c_bus(void)
{
	int status;

	mutex_lock(&psp_i2c_access_mutex);

	/* Return early if mailbox malfunctioned */
	if (psp_i2c_mbox_fail)
		goto cleanup;

	psp_i2c_access_count++;

	/*
	 * No need to request bus arbitration once we are inside semaphore
	 * reservation period.
	 */
	if (psp_i2c_sem_acquired)
		goto cleanup;

	status = psp_send_i2c_req(PSP_I2C_REQ_ACQUIRE);
	if (status)
		goto cleanup;

	psp_i2c_sem_acquired = jiffies;

	schedule_delayed_work(&release_queue,
			      msecs_to_jiffies(PSP_I2C_RESERVATION_TIME_MS));

	/*
	 * In case of errors with PSP arbitrator psp_i2c_mbox_fail variable is
	 * set above. As a consequence consecutive calls to acquire will bypass
	 * communication with PSP. At any case i2c bus is granted to the caller,
	 * thus always return success.
	 */
cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
	return 0;
}

static void psp_release_i2c_bus(void)
{
	mutex_lock(&psp_i2c_access_mutex);

	/* Return early if mailbox was malfunctional */
	if (psp_i2c_mbox_fail)
		goto cleanup;

	/*
	 * If we are last owner of PSP semaphore, need to release aribtration
	 * via mailbox.
	 */
	psp_i2c_access_count--;
	if (psp_i2c_access_count)
		goto cleanup;

	/*
	 * Send a release command to PSP if the semaphore reservation timeout
	 * elapsed but x86 still owns the controller.
	 */
	if (!delayed_work_pending(&release_queue))
		release_bus();

cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
}

/*
 * Locking methods are based on the default implementation from
 * drivers/i2c/i2c-core-base.c, but with psp acquire and release operations
 * added. With this in place we can ensure that i2c clients on the bus shared
 * with psp are able to lock HW access to the bus for arbitrary number of
 * operations - that is e.g. write-wait-read.
 */
static void i2c_adapter_dw_psp_lock_bus(struct i2c_adapter *adapter,
					unsigned int flags)
{
	psp_acquire_i2c_bus();
	rt_mutex_lock_nested(&adapter->bus_lock, i2c_adapter_depth(adapter));
}

static int i2c_adapter_dw_psp_trylock_bus(struct i2c_adapter *adapter,
					  unsigned int flags)
{
	int ret;

	ret = rt_mutex_trylock(&adapter->bus_lock);
	if (ret)
		return ret;

	psp_acquire_i2c_bus();

	return ret;
}

static void i2c_adapter_dw_psp_unlock_bus(struct i2c_adapter *adapter,
					  unsigned int flags)
{
	psp_release_i2c_bus();
	rt_mutex_unlock(&adapter->bus_lock);
}

static const struct i2c_lock_operations i2c_dw_psp_lock_ops = {
	.lock_bus = i2c_adapter_dw_psp_lock_bus,
	.trylock_bus = i2c_adapter_dw_psp_trylock_bus,
	.unlock_bus = i2c_adapter_dw_psp_unlock_bus,
};

int i2c_dw_amdpsp_probe_lock_support(struct dw_i2c_dev *dev)
{
	int ret;

	if (!dev)
		return -ENODEV;

	if (!(dev->flags & ARBITRATION_SEMAPHORE))
		return -ENODEV;

	/* Allow to bind only one instance of a driver */
	if (psp_i2c_dev)
		return -EEXIST;

	psp_i2c_dev = dev->dev;

	ret = psp_mbox_probe();
	if (ret)
		return ret;

	dev_info(psp_i2c_dev, "I2C bus managed by AMD PSP\n");

	/*
	 * Install global locking callbacks for adapter as well as internal i2c
	 * controller locks.
	 */
	dev->adapter.lock_ops = &i2c_dw_psp_lock_ops;
	dev->acquire_lock = psp_acquire_i2c_bus;
	dev->release_lock = psp_release_i2c_bus;

	return 0;
}
