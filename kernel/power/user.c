// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/kernel/power/user.c
 *
 * This file provides the user space interface for software suspend/resume.
 *
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 */

#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/freezer.h>

#include <linux/uaccess.h>
#include <linux/blkdev.h>

#include "power.h"

static bool need_wait;

static struct snapshot_data {
	struct snapshot_handle handle;
	int swap;
	int mode;
	bool frozen;
	bool ready;
	bool platform_support;
	bool free_bitmaps;
	struct block_device *bdev;
	dev_t dev;
} snapshot_state;

int is_hibernate_resume_dev(dev_t dev)
{
	return hibernation_available() && snapshot_state.dev == dev;
}

static int snapshot_open(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;
	int error;

	if (!hibernation_available())
		return -EPERM;

	lock_system_sleep();

	if (!hibernate_acquire()) {
		error = -EBUSY;
		goto Unlock;
	}

	if ((filp->f_flags & O_ACCMODE) == O_RDWR) {
		hibernate_release();
		error = -ENOSYS;
		goto Unlock;
	}
	nonseekable_open(inode, filp);
	data = &snapshot_state;
	filp->private_data = data;
	memset(&data->handle, 0, sizeof(struct snapshot_handle));
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY) {
		/* Hibernating.  The image device should be accessible. */
		data->swap = swap_type_of(swsusp_resume_device, 0);
		data->mode = O_RDONLY;
		data->free_bitmaps = false;
		error = pm_notifier_call_chain_robust(PM_HIBERNATION_PREPARE, PM_POST_HIBERNATION);
	} else {
		/*
		 * Resuming.  We may need to wait for the image device to
		 * appear.
		 */
		need_wait = true;

		data->swap = -1;
		data->mode = O_WRONLY;
		error = pm_notifier_call_chain_robust(PM_RESTORE_PREPARE, PM_POST_RESTORE);
		if (!error) {
			error = create_basic_memory_bitmaps();
			data->free_bitmaps = !error;
		}
	}
	if (error)
		hibernate_release();

	data->frozen = false;
	data->ready = false;
	data->platform_support = false;

 Unlock:
	unlock_system_sleep();

	return error;
}

static int snapshot_release(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;

	lock_system_sleep();

	swsusp_free();
	data = filp->private_data;
	if (!data->bdev) {
		data->dev = 0;
		free_all_swap_pages(data->swap);
	}

	if (data->frozen) {
		pm_restore_gfp_mask();
		free_basic_memory_bitmaps();
		thaw_processes();
	} else if (data->free_bitmaps) {
		free_basic_memory_bitmaps();
	}
	pm_notifier_call_chain(data->mode == O_RDONLY ?
			PM_POST_HIBERNATION : PM_POST_RESTORE);
	hibernate_release();

	unlock_system_sleep();

	return 0;
}

static ssize_t snapshot_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;
	loff_t pg_offp = *offp & ~PAGE_MASK;

	lock_system_sleep();

	data = filp->private_data;
	if (!data->ready) {
		res = -ENODATA;
		goto Unlock;
	}
	if (!pg_offp) { /* on page boundary? */
		res = snapshot_read_next(&data->handle);
		if (res <= 0)
			goto Unlock;
	} else {
		res = PAGE_SIZE - pg_offp;
	}

	res = simple_read_from_buffer(buf, count, &pg_offp,
			data_of(data->handle), res);
	if (res > 0)
		*offp += res;

 Unlock:
	unlock_system_sleep();

	return res;
}

static ssize_t snapshot_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;
	loff_t pg_offp = *offp & ~PAGE_MASK;

	if (need_wait) {
		wait_for_device_probe();
		need_wait = false;
	}

	lock_system_sleep();

	data = filp->private_data;

	if (!pg_offp) {
		res = snapshot_write_next(&data->handle);
		if (res <= 0)
			goto unlock;
	} else {
		res = PAGE_SIZE - pg_offp;
	}

	if (!data_of(data->handle)) {
		res = -EINVAL;
		goto unlock;
	}

	res = simple_write_to_buffer(data_of(data->handle), res, &pg_offp,
			buf, count);
	if (res > 0)
		*offp += res;
unlock:
	unlock_system_sleep();

	return res;
}

struct compat_resume_swap_area {
	compat_loff_t offset;
	u32 dev;
} __packed;

static int snapshot_set_swap_area(struct snapshot_data *data,
		void __user *argp)
{
	sector_t offset;
	dev_t swdev;

	if (swsusp_swap_in_use())
		return -EPERM;

	if (data->bdev)
		return -EBUSY;

	if (in_compat_syscall()) {
		struct compat_resume_swap_area swap_area;

		if (copy_from_user(&swap_area, argp, sizeof(swap_area)))
			return -EFAULT;
		swdev = new_decode_dev(swap_area.dev);
		offset = swap_area.offset;
	} else {
		struct resume_swap_area swap_area;

		if (copy_from_user(&swap_area, argp, sizeof(swap_area)))
			return -EFAULT;
		swdev = new_decode_dev(swap_area.dev);
		offset = swap_area.offset;
	}

	/*
	 * User space encodes device types as two-byte values,
	 * so we need to recode them
	 */
	data->swap = swap_type_of(swdev, offset);
	if (data->swap < 0)
		return swdev ? -ENODEV : -EINVAL;
	data->dev = swdev;
	return 0;
}

static int snapshot_set_block_device(struct snapshot_data *data, __u32 device)
{
	dev_t dev;
	struct block_device *bdev;
	int res;

	if (swsusp_swap_in_use())
		return -EPERM;

	if (data->swap > 0 || data->bdev)
		return -EBUSY;

	dev = new_decode_dev(device);
	bdev = blkdev_get_by_dev(dev,
			FMODE_WRITE | FMODE_READ | FMODE_EXCL, &snapshot_state);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	res = set_blocksize(bdev, PAGE_SIZE);
	if (res < 0) {
		blkdev_put(bdev, FMODE_WRITE | FMODE_READ | FMODE_EXCL);
		return res;
	}

	data->dev = dev;
	data->bdev = bdev;
	pr_info("snapshot block device set to %02x:%02x: %ld blocks", MAJOR(dev), MINOR(dev),
		i_size_read(bdev->bd_inode) >> PAGE_SHIFT);
	return 0;
}

static int snapshot_release_block_device(struct snapshot_data *data) {
	if (swsusp_swap_in_use())
		return -EPERM;

	if (!data->dev || !data->bdev)
		return -ENODEV;

	blkdev_put(data->bdev, FMODE_WRITE | FMODE_READ | FMODE_EXCL);
	data->dev = 0;
	data->bdev = NULL;

	return 0;
}

struct snapshot_bio_batch {
	atomic_t		count;
	wait_queue_head_t	wait;
	atomic_t 		status;
	struct blk_plug		plug;
	struct block_device    *bdev;
};

static void snapshot_bio_batch_init(struct snapshot_bio_batch *batch, struct block_device *bdev)
{
	atomic_set(&batch->count, 0);
	init_waitqueue_head(&batch->wait);
	atomic_set(&batch->status, BLK_STS_OK);
	blk_start_plug(&batch->plug);
	batch->bdev = bdev;
}

static void snapshot_bio_batch_finish(struct snapshot_bio_batch *batch)
{
	blk_finish_plug(&batch->plug);
}

static void snapshot_bio_end_io(struct bio *bio)
{
	struct snapshot_bio_batch *batch = bio->bi_private;
	struct page *page = bio_first_page_all(bio);

	if (bio->bi_status) {
		pr_alert_ratelimited("snapshot io error sector %Lu block error %d\n",
			 (unsigned long long)bio->bi_iter.bi_sector,
			 bio->bi_status);
		atomic_cmpxchg(&batch->status, BLK_STS_OK, bio->bi_status);
	} else if (bio_data_dir(bio) == READ)
		flush_icache_range((unsigned long)page_address(page),
				   (unsigned long)page_address(page) + PAGE_SIZE);

	if (atomic_dec_and_test(&batch->count))
		wake_up(&batch->wait);

	bio_put(bio);
}

static int snapshot_bio_batch_wait(struct snapshot_bio_batch *batch)
{
	wait_event(batch->wait, atomic_read(&batch->count) == 0);
	return blk_status_to_errno(atomic_read(&batch->status));
}

static int snapshot_submit_io(int op, int op_flags, sector_t sector,
		struct snapshot_bio_batch *batch, void *data)
{
	struct bio *bio = bio_alloc(GFP_NOIO | __GFP_HIGH, 1);
	if (!bio)
		return -ENOMEM;

	if (data && !bio_add_page(bio, virt_to_page(data), PAGE_SIZE, 0)) {
		pr_err("Adding page to bio failed at %llu\n",
		       (unsigned long long)bio->bi_iter.bi_sector);
		bio_put(bio);
		return -EFAULT;
	}

	bio_set_dev(bio, batch->bdev);
	bio->bi_iter.bi_sector = sector;
	bio->bi_opf = op | op_flags;
	bio->bi_end_io = snapshot_bio_end_io;
	bio->bi_private = batch;
	atomic_inc(&batch->count);
	submit_bio(bio);

	if (bio->bi_opf & REQ_SYNC)
		return snapshot_bio_batch_wait(batch);

	return 0;
}

static int snapshot_rw_block_device(int direction, struct snapshot_data *data) {
	struct snapshot_bio_batch batch;
	ktime_t start = ktime_get();
	sector_t sector = 0;
	int res;

	BUG_ON(direction != WRITE && direction != READ);
	snapshot_bio_batch_init(&batch, data->bdev);

	while (atomic_read(&batch.status) == BLK_STS_OK) {
		/* When writing we're reading from the snapshot, vice-versa. */
		if (direction == WRITE)
			res = snapshot_read_next(&data->handle);
		else
			res = snapshot_write_next(&data->handle);

		if (res <= 0)
			break;

		res = snapshot_submit_io(direction == WRITE ? REQ_OP_WRITE : REQ_OP_READ,
				REQ_IDLE | (data->handle.sync_read ? REQ_SYNC : 0), sector,
				&batch, data_of(data->handle));
		if (res)
			break;

		sector += PAGE_SECTORS;
	}

	snapshot_bio_batch_wait(&batch);
	snapshot_bio_batch_finish(&batch);

	if (!res)
		res = blk_status_to_errno(atomic_read(&batch.status));

	/* Issue a final flush to make sure everything is committed to disk */
	if (!res && direction == WRITE)
		res = snapshot_submit_io(REQ_OP_WRITE, REQ_FUA | REQ_SYNC | REQ_PREFLUSH,
				sector, &batch, NULL);

	if (!res)
		swsusp_show_speed(start, ktime_get(), sector >> PAGE_SECTORS_SHIFT, "transferred image via ioctl");

	return res;
}

static int snapshot_transfer_block_device(struct snapshot_data *data)
{
	if (swsusp_swap_in_use())
		return -EPERM;

	if (data->swap > 0)
		return -EBUSY;

	if (!data->bdev)
		return -ENODEV;

	if (data->mode == O_RDONLY) {
		if (!data->ready)
			return -ENODATA;

		return snapshot_rw_block_device(WRITE, data);
	} else if (data->mode == O_WRONLY) {
		if (snapshot_image_loaded(&data->handle))
			return -EBUSY;
		return snapshot_rw_block_device(READ, data);
	}

	return -EINVAL;
}

static long snapshot_ioctl(struct file *filp, unsigned int cmd,
							unsigned long arg)
{
	int error = 0;
	struct snapshot_data *data;
	loff_t size;
	sector_t offset;

	if (need_wait) {
		wait_for_device_probe();
		need_wait = false;
	}

	if (_IOC_TYPE(cmd) != SNAPSHOT_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > SNAPSHOT_IOC_MAXNR &&
		cmd != SNAPSHOT_SET_BLOCK_DEVICE &&
		cmd != SNAPSHOT_RELEASE_BLOCK_DEVICE &&
		cmd != SNAPSHOT_XFER_BLOCK_DEVICE)
		return -ENOTTY;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!mutex_trylock(&system_transition_mutex))
		return -EBUSY;

	lock_device_hotplug();
	data = filp->private_data;

	switch (cmd) {

	case SNAPSHOT_FREEZE:
		if (data->frozen)
			break;

		ksys_sync_helper();

		error = freeze_processes();
		if (error)
			break;

		error = create_basic_memory_bitmaps();
		if (error)
			thaw_processes();
		else
			data->frozen = true;

		break;

	case SNAPSHOT_UNFREEZE:
		if (!data->frozen || data->ready)
			break;
		pm_restore_gfp_mask();
		free_basic_memory_bitmaps();
		data->free_bitmaps = false;
		thaw_processes();
		data->frozen = false;
		break;

	case SNAPSHOT_CREATE_IMAGE:
		if (!hibernation_available() ||
			data->mode != O_RDONLY || !data->frozen  || data->ready) {
			error = -EPERM;
			break;
		}
		pm_restore_gfp_mask();
		error = hibernation_snapshot(data->platform_support);
		if (!error) {
			error = put_user(in_suspend, (int __user *)arg);
			data->ready = !freezer_test_done && !error;
			freezer_test_done = false;
		}
		break;

	case SNAPSHOT_ATOMIC_RESTORE:
		snapshot_write_finalize(&data->handle);
		if (data->mode != O_WRONLY || !data->frozen ||
		    !snapshot_image_loaded(&data->handle)) {
			error = -EPERM;
			break;
		}
		error = hibernation_restore(data->platform_support);
		break;

	case SNAPSHOT_FREE:
		swsusp_free();
		memset(&data->handle, 0, sizeof(struct snapshot_handle));
		data->ready = false;
		/*
		 * It is necessary to thaw kernel threads here, because
		 * SNAPSHOT_CREATE_IMAGE may be invoked directly after
		 * SNAPSHOT_FREE.  In that case, if kernel threads were not
		 * thawed, the preallocation of memory carried out by
		 * hibernation_snapshot() might run into problems (i.e. it
		 * might fail or even deadlock).
		 */
		thaw_kernel_threads();
		break;

	case SNAPSHOT_PREF_IMAGE_SIZE:
		image_size = arg;
		break;

	case SNAPSHOT_GET_IMAGE_SIZE:
		if (!data->ready && !(data->mode == O_WRONLY &&
					snapshot_image_loaded(&data->handle))) {
			error = -ENODATA;
			break;
		}
		size = snapshot_get_image_size();
		size <<= PAGE_SHIFT;
		error = put_user(size, (loff_t __user *)arg);
		break;

	case SNAPSHOT_AVAIL_SWAP_SIZE:
		if (data->bdev) {
			error = -ENODEV;
			break;
		}
		size = count_swap_pages(data->swap, 1);
		size <<= PAGE_SHIFT;
		error = put_user(size, (loff_t __user *)arg);
		break;

	case SNAPSHOT_ALLOC_SWAP_PAGE:
		if (data->swap < 0 || data->swap >= MAX_SWAPFILES ||
				data->bdev) {
			error = -ENODEV;
			break;
		}
		offset = alloc_swapdev_block(data->swap);
		if (offset) {
			offset <<= PAGE_SHIFT;
			error = put_user(offset, (loff_t __user *)arg);
		} else {
			error = -ENOSPC;
		}
		break;

	case SNAPSHOT_FREE_SWAP_PAGES:
		if (data->swap < 0 || data->swap >= MAX_SWAPFILES ||
				data->bdev) {
			error = -ENODEV;
			break;
		}

		free_all_swap_pages(data->swap);
		break;

	case SNAPSHOT_S2RAM:
		if (!data->frozen) {
			error = -EPERM;
			break;
		}
		/*
		 * Tasks are frozen and the notifiers have been called with
		 * PM_HIBERNATION_PREPARE
		 */
		error = suspend_devices_and_enter(PM_SUSPEND_MEM);
		data->ready = false;
		break;

	case SNAPSHOT_PLATFORM_SUPPORT:
		data->platform_support = !!arg;
		break;

	case SNAPSHOT_POWER_OFF:
		if (data->platform_support)
			error = hibernation_platform_enter();
		break;

	case SNAPSHOT_SET_SWAP_AREA:
		error = snapshot_set_swap_area(data, (void __user *)arg);
		break;

	case SNAPSHOT_SET_BLOCK_DEVICE:
		error = snapshot_set_block_device(data, (__u32)arg);
		break;

	case SNAPSHOT_RELEASE_BLOCK_DEVICE:
		snapshot_release_block_device(data);
		break;

	case SNAPSHOT_XFER_BLOCK_DEVICE:
		error = snapshot_transfer_block_device(data);
		break;
	default:
		error = -ENOTTY;

	}

	unlock_device_hotplug();
	mutex_unlock(&system_transition_mutex);

	return error;
}

#ifdef CONFIG_COMPAT
static long
snapshot_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	BUILD_BUG_ON(sizeof(loff_t) != sizeof(compat_loff_t));

	switch (cmd) {
	case SNAPSHOT_GET_IMAGE_SIZE:
	case SNAPSHOT_AVAIL_SWAP_SIZE:
	case SNAPSHOT_ALLOC_SWAP_PAGE:
	case SNAPSHOT_CREATE_IMAGE:
	case SNAPSHOT_SET_SWAP_AREA:
	case SNAPSHOT_SET_BLOCK_DEVICE:
	case SNAPSHOT_RELEASE_BLOCK_DEVICE:
	case SNAPSHOT_XFER_BLOCK_DEVICE:
		return snapshot_ioctl(file, cmd,
				      (unsigned long) compat_ptr(arg));
	default:
		return snapshot_ioctl(file, cmd, arg);
	}
}
#endif /* CONFIG_COMPAT */

static const struct file_operations snapshot_fops = {
	.open = snapshot_open,
	.release = snapshot_release,
	.read = snapshot_read,
	.write = snapshot_write,
	.llseek = no_llseek,
	.unlocked_ioctl = snapshot_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = snapshot_compat_ioctl,
#endif
};

static struct miscdevice snapshot_device = {
	.minor = SNAPSHOT_MINOR,
	.name = "snapshot",
	.fops = &snapshot_fops,
};

static int __init snapshot_device_init(void)
{
	return misc_register(&snapshot_device);
};

device_initcall(snapshot_device_init);
