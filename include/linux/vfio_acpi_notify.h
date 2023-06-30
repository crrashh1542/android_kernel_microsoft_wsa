/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VFIO ACPI notification replication
 *
 * Author: Grzegorz Jaszczyk <jaz@semihalf.com>
 */
#include <linux/acpi.h>
#include <linux/eventfd.h>
#include <linux/poll.h>

#ifndef VFIO_ACPI_NOTIFY_H
#define VFIO_ACPI_NOTIFY_H

struct vfio_acpi_notification;

#if IS_ENABLED(CONFIG_ACPI)
void vfio_acpi_notify(struct acpi_device *adev, u32 event, void *data);
int vfio_register_acpi_notify_handler(struct vfio_acpi_notification **acpi_notify,
				      struct acpi_device *adev, int32_t fd);
void vfio_remove_acpi_notify(struct vfio_acpi_notification **acpi_notify,
			     struct acpi_device *adev);
#else
static inline void vfio_acpi_notify(struct acpi_device *adev, u32 event, void *data) {}
static inline int
vfio_register_acpi_notify_handler(struct vfio_acpi_notification **acpi_notify,
				  struct acpi_device *adev, int32_t fd)
{
	return -ENODEV;
}

static inline void
vfio_remove_acpi_notify(struct vfio_acpi_notification **acpi_notify,
			struct acpi_device *adev) {}
#endif /* CONFIG_ACPI */

#endif /* VFIO_ACPI_NOTIFY_H */
