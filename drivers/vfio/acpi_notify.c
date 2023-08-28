// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO ACPI notification propagation
 *
 * Author: Grzegorz Jaszczyk <jaz@semihalf.com>
 */
#include <linux/file.h>
#include <linux/semaphore.h>
#include <linux/vfio_acpi_notify.h>

#define NOTIFICATION_QUEUE_SIZE 20

struct acpi_eventfd_ctx {
	struct eventfd_ctx	*acpi_notify_trigger;
	struct file		*acpi_notify_trigger_file;
	struct semaphore	notification_sem;
	struct work_struct	acpi_notification_work;
	wait_queue_entry_t	wait;
	poll_table		pt;
	struct vfio_acpi_notification *acpi_notify;
};

struct vfio_acpi_notification {
	struct acpi_eventfd_ctx		*acpi_eventfd;
	struct list_head		notification_list;
	struct mutex			notification_list_lock;
	int				notification_queue_count;
};

struct notification_queue {
	int notification_val;
	struct list_head node;
};

static int vfio_eventfd_wakeup(wait_queue_entry_t *wait, unsigned int mode,
			       int sync, void *key)
{
	struct acpi_eventfd_ctx *acpi_eventfdctx =
		container_of(wait, struct acpi_eventfd_ctx, wait);
	__poll_t flags = key_to_poll(key);

	/*
	 * eventfd_read signalize EPOLLOUT at the end of its function - this
	 * means previous eventfd with its notification value was consumed so
	 * the next notification can be signalized now if pending - schedule
	 * proper work.
	 */
	if (flags & EPOLLOUT) {
		up(&acpi_eventfdctx->notification_sem);

		schedule_work(&acpi_eventfdctx->acpi_notification_work);
	}

	/*
	 * Even if the eventfd is closed lets still queue notifications so they
	 * can be replicated when new eventfd is registered (see "Allow eventfd
	 * to be swapped").
	 *
	 * Below will be reached only in case user closes eventfd and then
	 * trigger eventfd swap (or vice-versa).
	 */
	if (flags & EPOLLHUP) {
		/*
		 * eventfd_release after signalling EPOLLHUP calls eventfd_ctx_put
		 * so no need to do it here.
		 */

		kfree(acpi_eventfdctx);
	}

	return 0;
}

static void vfio_ptable_queue_proc(struct file *file,
				   wait_queue_head_t *wqh, poll_table *pt)
{
	struct acpi_eventfd_ctx *acpi_eventfdctx =
		container_of(pt, struct acpi_eventfd_ctx, pt);

	add_wait_queue(wqh, &acpi_eventfdctx->wait);
}

static struct notification_queue *
acpi_notification_dequeue(struct vfio_acpi_notification *acpi_notify)
{
	struct notification_queue *oldest_entry;

	lockdep_assert_held(&acpi_notify->notification_list_lock);

	oldest_entry = list_first_entry(&acpi_notify->notification_list,
					struct notification_queue,
					node);
	list_del(&oldest_entry->node);
	acpi_notify->notification_queue_count--;

	return oldest_entry;
}

static void acpi_notification_work_fn(struct work_struct *work)
{
	struct acpi_eventfd_ctx *acpi_eventfdctx;
	struct vfio_acpi_notification *acpi_notify;
	struct notification_queue *entry;

	acpi_eventfdctx = container_of(work, struct acpi_eventfd_ctx,
				       acpi_notification_work);

	acpi_notify = acpi_eventfdctx->acpi_notify;

	mutex_lock(&acpi_notify->notification_list_lock);
	if (list_empty(&acpi_notify->notification_list))
		goto out;

	/*
	 * If the previous eventfd was not yet consumed by user-space lets hold
	 * on and exit. The notification function will be rescheduled when
	 * signaling eventfd will be possible (when the EPOLLOUT will be
	 * signalized and unlocks notify_events or when eventfd will be swapped).
	 */
	if (down_trylock(&acpi_eventfdctx->notification_sem))
		goto out;

	entry = acpi_notification_dequeue(acpi_notify);

	mutex_unlock(&acpi_notify->notification_list_lock);

	eventfd_signal(acpi_eventfdctx->acpi_notify_trigger, entry->notification_val);

	kfree(entry);

	return;
out:
	mutex_unlock(&acpi_notify->notification_list_lock);
}

static void
vfio_acpi_notify_cleanup(struct vfio_acpi_notification **acpi_notify_ptr,
			 struct acpi_device *adev)
{
	struct vfio_acpi_notification *acpi_notify = *acpi_notify_ptr;
	struct acpi_eventfd_ctx *acpi_eventfd = acpi_notify->acpi_eventfd;
	struct notification_queue *entry, *entry_tmp;
	u64 cnt;

	eventfd_ctx_remove_wait_queue(acpi_eventfd->acpi_notify_trigger,
				      &acpi_eventfd->wait, &cnt);

	flush_work(&acpi_eventfd->acpi_notification_work);

	mutex_lock(&acpi_notify->notification_list_lock);
	list_for_each_entry_safe(entry, entry_tmp,
				 &acpi_notify->notification_list,
				 node) {
		list_del(&entry->node);
		kfree(entry);
	}
	mutex_unlock(&acpi_notify->notification_list_lock);

	eventfd_ctx_put(acpi_eventfd->acpi_notify_trigger);

	/*
	 * fput releases references to eventfd file but it will not trigger the
	 * vfio_eventfd_wakeup with EPOLLHUP since eventfd_ctx_remove_wait_queue
	 * was already called and removed wq entry (wait) from the eventfd
	 * wq head.
	 */
	fput(acpi_eventfd->acpi_notify_trigger_file);

	kfree(acpi_notify->acpi_eventfd);
	kfree(acpi_notify);

	*acpi_notify_ptr = NULL;
}

static void vfio_acpi_notify_handler(acpi_handle handle, u32 event, void *data)
{
	struct vfio_acpi_notification *acpi_notify = (struct vfio_acpi_notification *)data;
	struct notification_queue *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return;

	entry->notification_val = event;
	INIT_LIST_HEAD(&entry->node);

	mutex_lock(&acpi_notify->notification_list_lock);
	if (acpi_notify->notification_queue_count > NOTIFICATION_QUEUE_SIZE) {
		struct notification_queue *oldest_entry =
			acpi_notification_dequeue(acpi_notify);

		if (printk_ratelimit())
			acpi_handle_warn(handle,
					 "dropping notification value %d\n",
					 oldest_entry->notification_val);

		kfree(oldest_entry);
	}
	list_add_tail(&entry->node, &acpi_notify->notification_list);
	acpi_notify->notification_queue_count++;
	mutex_unlock(&acpi_notify->notification_list_lock);

	schedule_work(&acpi_notify->acpi_eventfd->acpi_notification_work);
}

void vfio_acpi_notify(struct acpi_device *adev, u32 event, void *data)
{
	acpi_handle handle = adev->handle;

	vfio_acpi_notify_handler(handle, event, data);
}
EXPORT_SYMBOL_GPL(vfio_acpi_notify);

void vfio_remove_acpi_notify(struct vfio_acpi_notification **acpi_notify_ptr,
			     struct acpi_device *adev)
{
	struct vfio_acpi_notification *acpi_notify = *acpi_notify_ptr;

	if (!acpi_notify)
		return;

	acpi_remove_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
				   vfio_acpi_notify_handler);

	vfio_acpi_notify_cleanup(acpi_notify_ptr, adev);
}
EXPORT_SYMBOL_GPL(vfio_remove_acpi_notify);

static void vfio_acpi_eventfd_init(struct vfio_acpi_notification *acpi_notify,
				   struct eventfd_ctx *efdctx, int32_t fd)
{
	struct acpi_eventfd_ctx *acpi_eventfd;

	acpi_eventfd = kzalloc(sizeof(struct acpi_eventfd_ctx), GFP_KERNEL_ACCOUNT);

	INIT_WORK(&acpi_eventfd->acpi_notification_work, acpi_notification_work_fn);
	acpi_eventfd->acpi_notify_trigger = efdctx;

	sema_init(&acpi_eventfd->notification_sem, 1);

	/*
	 * Install custom wake-up handler to be notified whenever underlying
	 * eventfd is consumed by the user-space.
	 */
	init_waitqueue_func_entry(&acpi_eventfd->wait, vfio_eventfd_wakeup);
	init_poll_funcptr(&acpi_eventfd->pt, vfio_ptable_queue_proc);

	acpi_eventfd->acpi_notify_trigger_file = eventfd_fget(fd);
	vfs_poll(acpi_eventfd->acpi_notify_trigger_file, &acpi_eventfd->pt);

	acpi_eventfd->acpi_notify = acpi_notify;
	acpi_notify->acpi_eventfd = acpi_eventfd;
}

int vfio_register_acpi_notify_handler(struct vfio_acpi_notification **acpi_notify_ptr,
				      struct acpi_device *adev, int32_t fd)
{
	struct vfio_acpi_notification *acpi_notify = *acpi_notify_ptr;
	struct eventfd_ctx *efdctx;
	acpi_status status;

	if (fd < -1)
		return -EINVAL;
	else if (fd == -1) {
		vfio_remove_acpi_notify(acpi_notify_ptr, adev);
		return 0;
	}

	efdctx = eventfd_ctx_fdget(fd);
	if (IS_ERR(efdctx))
		return PTR_ERR(efdctx);

	/* Allow eventfd to be swapped */
	if (acpi_notify) {
		struct file *trigger_file_before_swap =
			acpi_notify->acpi_eventfd->acpi_notify_trigger_file;

		/*
		 * Lets allocate new acpi_eventfd_ctx, the previous is
		 * alive until eventfd is closed.
		 */
		vfio_acpi_eventfd_init(acpi_notify, efdctx, fd);

		/*
		 * The ACPI notifications could arrive and be queued during
		 * eventfd swap, retrigger the worker after notification
		 * replication unlocking.
		 */
		schedule_work(&acpi_notify->acpi_eventfd->acpi_notification_work);

		/*
		 * If the last reference to acpi_notify_trigger_file was
		 * released, the EPOLLHUP will be handled (but not immediately
		 * since fput is async).
		 */
		fput(trigger_file_before_swap);

		return 0;
	}

	acpi_notify = kzalloc(sizeof(*acpi_notify), GFP_KERNEL_ACCOUNT);
	if (!acpi_notify)
		return -ENOMEM;

	*acpi_notify_ptr = acpi_notify;
	mutex_init(&acpi_notify->notification_list_lock);
	INIT_LIST_HEAD(&acpi_notify->notification_list);

	vfio_acpi_eventfd_init(acpi_notify, efdctx, fd);

	status = acpi_install_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
				vfio_acpi_notify_handler, (void *)acpi_notify);
	if (ACPI_FAILURE(status)) {
		dev_err(&adev->dev, "Failed to install notify handler: %s",
			acpi_format_exception(status));

		vfio_acpi_notify_cleanup(acpi_notify_ptr, adev);

		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_register_acpi_notify_handler);
