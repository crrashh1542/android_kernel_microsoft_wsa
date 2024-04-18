// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <drm/drm_privacy_screen_machine.h>
#include <drm/drm_privacy_screen_driver.h>
#include <linux/dmi.h>

#ifdef CONFIG_X86
static struct drm_privacy_screen_lookup arch_lookup;

struct arch_init_data {
	struct drm_privacy_screen_lookup lookup;
	bool (*detect)(void);
};

#if IS_ENABLED(CONFIG_THINKPAD_ACPI)
static acpi_status __init acpi_set_handle(acpi_handle handle, u32 level,
					  void *context, void **return_value)
{
	*(acpi_handle *)return_value = handle;
	return AE_CTRL_TERMINATE;
}

static bool __init detect_thinkpad_privacy_screen(void)
{
	union acpi_object obj = { .type = ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { .count = 1, .pointer = &obj, };
	acpi_handle ec_handle = NULL;
	unsigned long long output;
	acpi_status status;

	if (acpi_disabled)
		return false;

	/* Get embedded-controller handle */
	status = acpi_get_devices("PNP0C09", acpi_set_handle, NULL, &ec_handle);
	if (ACPI_FAILURE(status) || !ec_handle)
		return false;

	/* And call the privacy-screen get-status method */
	status = acpi_evaluate_integer(ec_handle, "HKEY.GSSS", &args, &output);
	if (ACPI_FAILURE(status))
		return false;

	return (output & 0x10000) ? true : false;
}
#endif

#if IS_ENABLED(CONFIG_CHROMEOS_PRIVACY_SCREEN)
/* DRALLION FUNCTIONS START HERE */
#ifdef CONFIG_ACPI

#define PRIV_SCRN_DSM_REVID		1	/* DSM version */
#define PRIV_SCRN_DSM_FN_GET_STATUS	1	/* Get privacy screen status */
#define PRIV_SCRN_DSM_FN_ENABLE		2	/* Enable privacy screen */
#define PRIV_SCRN_DSM_FN_DISABLE	3	/* Disable privacy screen */
#define DRALLION_PRIVACY_SCREEN_ADDR 0x80010400u /* Lookup address for privacy screen on drallion*/

static const guid_t chromeos_privacy_screen_dsm_guid =
	GUID_INIT(0xc7033113, 0x8720, 0x4ceb, 0x90, 0x90, 0x9d, 0x52, 0xb3,
		  0xe5, 0x2d, 0x73);

struct device *privacy_screen_dev;

static void chromeos_privacy_screen_get_hw_state(
	struct drm_privacy_screen *drm_privacy_screen)
{
	union acpi_object *obj;
	acpi_handle handle;
	struct device *privacy_screen =
		drm_privacy_screen_get_drvdata(drm_privacy_screen);

	handle = acpi_device_handle(to_acpi_device(privacy_screen));
	obj = acpi_evaluate_dsm(handle,
				&chromeos_privacy_screen_dsm_guid,
				PRIV_SCRN_DSM_REVID,
				PRIV_SCRN_DSM_FN_GET_STATUS,
				NULL);
	if (!obj) {
		dev_err(privacy_screen,
			"_DSM failed to get privacy-screen state\n");
		return;
	}

	if (obj->type != ACPI_TYPE_INTEGER)
		dev_err(privacy_screen,
			"Bad _DSM to get privacy-screen state\n");
	else if (obj->integer.value == 1) {
		drm_privacy_screen->sw_state = PRIVACY_SCREEN_ENABLED;
		drm_privacy_screen->hw_state = PRIVACY_SCREEN_ENABLED;

	} else {
		drm_privacy_screen->sw_state = PRIVACY_SCREEN_DISABLED;
		drm_privacy_screen->hw_state = PRIVACY_SCREEN_DISABLED;
	}
	ACPI_FREE(obj);
}
static int chromeos_privacy_screen_set_sw_state(
	struct drm_privacy_screen *drm_privacy_screen,
	enum drm_privacy_screen_status state)
{
	union acpi_object *obj = NULL;
	acpi_handle handle;
	struct device *privacy_screen =
		drm_privacy_screen_get_drvdata(drm_privacy_screen);

	handle = acpi_device_handle(to_acpi_device(privacy_screen));

	if (state == PRIVACY_SCREEN_DISABLED) {
		obj = acpi_evaluate_dsm(
			handle, &chromeos_privacy_screen_dsm_guid,
			PRIV_SCRN_DSM_REVID,
			PRIV_SCRN_DSM_FN_DISABLE,
			NULL);
	} else if (state == PRIVACY_SCREEN_ENABLED) {
		obj = acpi_evaluate_dsm(
			handle,
			&chromeos_privacy_screen_dsm_guid,
			PRIV_SCRN_DSM_REVID,
			PRIV_SCRN_DSM_FN_ENABLE,
			NULL);
	} else {
		dev_err(privacy_screen,
			"Bad attempt to set privacy-screen status to %u\n",
			state);
		return -EINVAL;
	}

	if (!obj) {
		dev_err(privacy_screen,
			"_DSM failed to set privacy-screen state\n");
		return -EIO;
	}

	drm_privacy_screen->sw_state = state;
	drm_privacy_screen->hw_state = state;
	ACPI_FREE(obj);
	return 0;
}

static const struct drm_privacy_screen_ops chromeos_privacy_screen_ops = {
	.get_hw_state = chromeos_privacy_screen_get_hw_state,
	.set_sw_state = chromeos_privacy_screen_set_sw_state,
};

static int match_privacy_screen(struct device *dev, void *data)
{
	struct acpi_device *adev = to_acpi_device(dev);

	u64 addr = acpi_device_adr(adev);

	if (addr == DRALLION_PRIVACY_SCREEN_ADDR) {
		privacy_screen_dev = dev;
		return 1;
	}

	return 0;
}

bool privacy_screen_present(struct device *privacy_screen)
{
	acpi_handle handle = acpi_device_handle(to_acpi_device(privacy_screen));

	if (!handle)
		return false;

	if (!acpi_check_dsm(handle, &chromeos_privacy_screen_dsm_guid, 1,
			    BIT(PRIV_SCRN_DSM_FN_GET_STATUS) |
				BIT(PRIV_SCRN_DSM_FN_ENABLE) |
				BIT(PRIV_SCRN_DSM_FN_DISABLE)))
		return false;

	return true;
}

static bool drm_drallion_privacy_screen_register(struct device *dev)
{

	struct acpi_device *adev = to_acpi_device(dev);

	struct drm_privacy_screen *drm_privacy_screen;

	if (strcmp(dev_name(&adev->dev), "device:05") != 0) {
		dev_err(&adev->dev, "Unexpected device name for privacy screen: %s\n",
				dev_name(&adev->dev));

		return false;
	}

	drm_privacy_screen = drm_privacy_screen_register(
		&adev->dev, &chromeos_privacy_screen_ops, &adev->dev);

	if (IS_ERR(drm_privacy_screen)) {
		dev_err(&adev->dev, "Error registering privacy-screen: %d\n",
				PTR_ERR(drm_privacy_screen));
		return false;
	}

	adev->driver_data = drm_privacy_screen;
	dev_info(&adev->dev, "registered privacy-screen '%s'\n",
		 dev_name(&drm_privacy_screen->dev));

	return true;
}

static bool __init detect_drallion_privacy_screen(void)
{

	if (!dmi_match(DMI_PRODUCT_NAME, "Drallion"))
		return false;

	if (acpi_dev_present("GOOG0010", NULL, -1))
		return false;

	/* For drallion device GOOG0010 will not find privacy screen */
	if (!acpi_bus_for_each_dev(&match_privacy_screen, NULL))
		return false;

	if (!privacy_screen_dev)
		return false;

	if (!privacy_screen_present(privacy_screen_dev))
		return false;

	return drm_drallion_privacy_screen_register(privacy_screen_dev);
}

#else
static bool __init detect_drallion_privacy_screen(void)
{
	return false;
}
#endif

/* DRALLION FUNCTIONS END HERE */

static bool __init detect_chromeos_privacy_screen(void)
{
	return acpi_dev_present("GOOG0010", NULL, -1);
}
#endif

static const struct arch_init_data arch_init_data[] __initconst = {
#if IS_ENABLED(CONFIG_THINKPAD_ACPI)
	{
		.lookup = {
			.dev_id = NULL,
			.con_id = NULL,
			.provider = "privacy_screen-thinkpad_acpi",
		},
		.detect = detect_thinkpad_privacy_screen,
	},
#endif
#if IS_ENABLED(CONFIG_CHROMEOS_PRIVACY_SCREEN)
	{
		.lookup = {
			.dev_id = NULL,
			.con_id = NULL,
			.provider = "privacy_screen-GOOG0010:00",
		},
		.detect = detect_chromeos_privacy_screen,
	},
		{
		.lookup = {
			.dev_id = NULL,
			.con_id = NULL,
			.provider = "privacy_screen-device:05",
		},
		.detect = detect_drallion_privacy_screen,
	},
#endif
};

void __init drm_privacy_screen_lookup_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(arch_init_data); i++) {
		if (!arch_init_data[i].detect())
			continue;

		pr_info("Found '%s' privacy-screen provider\n",
			arch_init_data[i].lookup.provider);

		/* Make a copy because arch_init_data is __initconst */
		arch_lookup = arch_init_data[i].lookup;
		drm_privacy_screen_lookup_add(&arch_lookup);
		break;
	}
}

void drm_privacy_screen_lookup_exit(void)
{
	if (arch_lookup.provider)
		drm_privacy_screen_lookup_remove(&arch_lookup);
}
#endif /* ifdef CONFIG_X86 */
