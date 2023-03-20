// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/component.h>

#include <drm/i915_pxp_tee_interface.h>
#include <drm/i915_component.h>

#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_tee_interface.h"

static inline struct intel_pxp *i915_dev_to_pxp(struct device *i915_kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);

	return &to_gt(i915)->pxp;
}

static int intel_pxp_tee_io_message(struct intel_pxp *pxp,
				    void *msg_in, u32 msg_in_size,
				    void *msg_out, u32 msg_out_max_size,
				    u32 *msg_out_rcv_size)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct i915_pxp_component *pxp_component = pxp->pxp_component;
	int ret = 0;
	u8 vtag = 1;

	mutex_lock(&pxp->tee_mutex);

	/*
	 * The binding of the component is asynchronous from i915 probe, so we
	 * can't be sure it has happened.
	 */
	if (!pxp_component) {
		ret = -ENODEV;
		goto unlock;
	}

	/* The vtag is stored in the first byte of the output buffer */
	memcpy(&vtag, msg_out, sizeof(vtag));

	if (pxp->last_tee_msg_interrupted) {
		/* read and drop data from the previous iteration */
		ret = pxp_component->ops->recv(pxp_component->tee_dev, msg_out,
					msg_out_max_size, vtag);
		if (ret == -EINTR)
			goto unlock;

		pxp->last_tee_msg_interrupted = false;
	}

	ret = pxp_component->ops->send(pxp_component->tee_dev, msg_in, msg_in_size, vtag);
	if (ret) {
		/* flag on next msg to drop interrupted msg */
		if (ret == -EINTR)
			pxp->last_tee_msg_interrupted = true;

		drm_err(&i915->drm, "Failed to send PXP TEE message\n");
		goto unlock;
	}

	ret = pxp_component->ops->recv(pxp_component->tee_dev, msg_out, msg_out_max_size, vtag);
	if (ret < 0) {
		/* flag on next msg to drop interrupted msg */
		if (ret == -EINTR)
			pxp->last_tee_msg_interrupted = true;

		drm_err(&i915->drm, "Failed to receive PXP TEE message\n");
		goto unlock;
	}

	if (ret > msg_out_max_size) {
		drm_err(&i915->drm,
			"Failed to receive PXP TEE message due to unexpected output size\n");
		ret = -ENOSPC;
		goto unlock;
	}

	if (msg_out_rcv_size)
		*msg_out_rcv_size = ret;

	ret = 0;
unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

/**
 * i915_pxp_tee_component_bind - bind function to pass the function pointers to pxp_tee
 * @i915_kdev: pointer to i915 kernel device
 * @tee_kdev: pointer to tee kernel device
 * @data: pointer to pxp_tee_master containing the function pointers
 *
 * This bind function is called during the system boot or resume from system sleep.
 *
 * Return: return 0 if successful.
 */
static int i915_pxp_tee_component_bind(struct device *i915_kdev,
				       struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);
	intel_wakeref_t wakeref;

	if (!HAS_HECI_PXP(i915)) {
		pxp->dev_link = device_link_add(i915_kdev, tee_kdev, DL_FLAG_STATELESS);
		if (drm_WARN_ON(&i915->drm, !pxp->dev_link))
			return -ENODEV;
	}

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = data;
	pxp->pxp_component->tee_dev = tee_kdev;
	mutex_unlock(&pxp->tee_mutex);

	/* if we are suspended, the HW will be re-initialized on resume */
	wakeref = intel_runtime_pm_get_if_in_use(&i915->runtime_pm);
	if (!wakeref)
		return 0;

	/* the component is required to fully start the PXP HW */
	intel_pxp_init_hw(pxp);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return 0;
}

static void i915_pxp_tee_component_unbind(struct device *i915_kdev,
					  struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);
	intel_wakeref_t wakeref;

	with_intel_runtime_pm_if_in_use(&i915->runtime_pm, wakeref)
		intel_pxp_fini_hw(pxp);

	mutex_lock(&pxp->tee_mutex);
	pxp->pxp_component = NULL;
	mutex_unlock(&pxp->tee_mutex);

	if (pxp->dev_link) {
		device_link_del(pxp->dev_link);
		pxp->dev_link = NULL;
	}
}

static const struct component_ops i915_pxp_tee_component_ops = {
	.bind   = i915_pxp_tee_component_bind,
	.unbind = i915_pxp_tee_component_unbind,
};

int intel_pxp_tee_component_init(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct drm_i915_private *i915 = gt->i915;

	ret = component_add_typed(i915->drm.dev, &i915_pxp_tee_component_ops,
				  I915_COMPONENT_PXP);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to add PXP component (%d)\n", ret);
		return ret;
	}

	pxp->pxp_component_added = true;

	return 0;
}

void intel_pxp_tee_component_fini(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;

	if (!pxp->pxp_component_added)
		return;

	component_del(i915->drm.dev, &i915_pxp_tee_component_ops);
	pxp->pxp_component_added = false;
}

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct pxp_tee_create_arb_in msg_in = {0};
	struct pxp_tee_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_TEE_APIVER;
	msg_in.header.command_id = PXP_TEE_ARB_CMDID;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP_TEE_ARB_PROTECTION_MODE;
	msg_in.session_id = arb_session_id;

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	if (ret)
		drm_err(&i915->drm, "Failed to send tee msg ret=[%d]\n", ret);
	else if (msg_out.header.status != 0x0)
		drm_warn(&i915->drm, "PXP firmware failed arb session init request ret=[0x%08x]\n",
			 msg_out.header.status);

	return ret;
}

static void intel_pxp_tee_end_one_fw_session(struct intel_pxp *pxp, u32 session_id, bool is_alive)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct pxp_inv_stream_key_in msg_in = {0};
	struct pxp_inv_stream_key_out msg_out = {0};
	int ret, trials = 0;

try_again:
	memset(&msg_in, 0, sizeof(msg_in));
	memset(&msg_out, 0, sizeof(msg_out));
	msg_in.header.api_version = PXP_TEE_APIVER;
	msg_in.header.command_id = PXP_TEE_INVALIDATE_STREAM_KEY;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	msg_in.header.extdata = FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_VALID, 1);
	msg_in.header.extdata |= FIELD_PREP(PXP_CMDHDR_EXTDATA_APP_TYPE, 0);
	msg_in.header.extdata |= FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_ID, session_id);

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	/* Cleanup coherency between GT and Firmware is critical, so try again if it fails */
	if ((ret || msg_out.header.status != 0x0) && ++trials < 3)
		goto try_again;

	if (ret)
		drm_err(&i915->drm, "Failed to send tee msg for inv-stream-key-%d, ret=[%d]\n",
			session_id, ret);
	else if (msg_out.header.status != 0x0 && is_alive)
		drm_warn(&i915->drm, "PXP firmware failed inv-stream-key-%d with status 0x%08x\n",
			 session_id, msg_out.header.status);
}

void intel_pxp_tee_end_all_fw_sessions(struct intel_pxp *pxp, u32 sessions_mask)
{
	int n;

	for (n = 0; n < INTEL_PXP_MAX_HWDRM_SESSIONS; ++n) {
		intel_pxp_tee_end_one_fw_session(pxp, n, (sessions_mask & 0x1) ? true : false);
		sessions_mask = (sessions_mask >> 1);
	}
}

int intel_pxp_tee_ioctl_io_message(struct intel_pxp *pxp,
				   struct downstream_drm_i915_pxp_tee_io_message_params *params)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	void *msg_in = NULL;
	void *msg_out = NULL;
	int ret = 0;

	if (!params->msg_in || !params->msg_out ||
	    params->msg_out_buf_size == 0 || params->msg_in_size == 0)
		return -EINVAL;

	msg_in = kzalloc(params->msg_in_size, GFP_KERNEL);
	if (!msg_in)
		return -ENOMEM;

	msg_out = kzalloc(params->msg_out_buf_size, GFP_KERNEL);
	if (!msg_out) {
		ret = -ENOMEM;
		goto end;
	}

	if (copy_from_user(msg_in, u64_to_user_ptr(params->msg_in), params->msg_in_size)) {
		drm_dbg(&i915->drm, "Failed to copy_from_user for TEE input message\n");
		ret = -EFAULT;
		goto end;
	}

	if (copy_from_user(msg_out, u64_to_user_ptr(params->msg_out), params->msg_out_buf_size)) {
		drm_dbg(&i915->drm, "Failed to copy_from_user for TEE vtag output message\n");
	}

	ret = intel_pxp_tee_io_message(pxp,
				       msg_in, params->msg_in_size,
				       msg_out, params->msg_out_buf_size,
				       &params->msg_out_ret_size);
	if (ret) {
		drm_dbg(&i915->drm, "Failed to send/receive user TEE message\n");
		goto end;
	}

	if (copy_to_user(u64_to_user_ptr(params->msg_out), msg_out, params->msg_out_ret_size)) {
		drm_dbg(&i915->drm, "Failed copy_to_user for TEE output message\n");
		ret = -EFAULT;
		goto end;
	}

end:
	kfree(msg_in);
	kfree(msg_out);
	return ret;
}
