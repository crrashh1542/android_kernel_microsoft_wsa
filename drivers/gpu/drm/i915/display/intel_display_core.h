/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_CORE_H__
#define __INTEL_DISPLAY_CORE_H__

#include <linux/types.h>
#include "intel_dpll_mgr.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_cdclk_funcs;
struct intel_color_funcs;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dpll_funcs;
struct intel_dpll_mgr;
struct intel_fdi_funcs;
struct intel_hotplug_funcs;
struct intel_initial_plane_config;

struct intel_display_funcs {
	/*
	 * Returns the active state of the crtc, and if the crtc is active,
	 * fills out the pipe-config with the hw state.
	 */
	bool (*get_pipe_config)(struct intel_crtc *,
				struct intel_crtc_state *);
	void (*get_initial_plane_config)(struct intel_crtc *,
					 struct intel_initial_plane_config *);
	void (*crtc_enable)(struct intel_atomic_state *state,
			    struct intel_crtc *crtc);
	void (*crtc_disable)(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
	void (*commit_modeset_enables)(struct intel_atomic_state *state);
};

/* functions used for watermark calcs for display. */
struct intel_wm_funcs {
	/* update_wm is for legacy wm management */
	void (*update_wm)(struct drm_i915_private *dev_priv);
	int (*compute_pipe_wm)(struct intel_atomic_state *state,
			       struct intel_crtc *crtc);
	int (*compute_intermediate_wm)(struct intel_atomic_state *state,
				       struct intel_crtc *crtc);
	void (*initial_watermarks)(struct intel_atomic_state *state,
				   struct intel_crtc *crtc);
	void (*atomic_update_watermarks)(struct intel_atomic_state *state,
					 struct intel_crtc *crtc);
	void (*optimize_watermarks)(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
	int (*compute_global_watermarks)(struct intel_atomic_state *state);
};

/*
 * dpll and cdclk state is protected by connection_mutex dpll.lock serializes
 * intel_{prepare,enable,disable}_shared_dpll.  Must be global rather than per
 * dpll, because on some platforms plls share registers.
 */
struct intel_dpll {
	struct mutex lock;

	int num_shared_dpll;
	struct intel_shared_dpll shared_dplls[I915_NUM_PLLS];
	const struct intel_dpll_mgr *mgr;

	struct {
		int nssc;
		int ssc;
	} ref_clks;
};

struct intel_display {
	/* Display functions */
	struct {
		/* Top level crtc-ish functions */
		const struct intel_display_funcs *display;

		/* Display CDCLK functions */
		const struct intel_cdclk_funcs *cdclk;

		/* Display pll funcs */
		const struct intel_dpll_funcs *dpll;

		/* irq display functions */
		const struct intel_hotplug_funcs *hotplug;

		/* pm display functions */
		const struct intel_wm_funcs *wm;

		/* fdi display functions */
		const struct intel_fdi_funcs *fdi;

		/* Display internal color functions */
		const struct intel_color_funcs *color;
	} funcs;
	struct intel_dpll dpll;
};

#endif /* __INTEL_DISPLAY_CORE_H__ */
