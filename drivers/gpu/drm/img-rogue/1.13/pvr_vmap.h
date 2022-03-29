/*************************************************************************/ /*!
@File
@Title          Utility functions for virtual memory mapping
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Strictly Confidential.
*/ /**************************************************************************/

#ifndef PVR_VMAP_H
#define PVR_VMAP_H

#include <linux/version.h>
#include <linux/vmalloc.h>

static inline void *pvr_vmap(struct page **pages,
			     unsigned int count,
			     __maybe_unused unsigned long flags,
			     pgprot_t prot)
{
#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	return vmap(pages, count, flags, prot);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	return vm_map_ram(pages, count, -1, prot);
#else
	if (pgprot_val(prot) == pgprot_val(PAGE_KERNEL))
		return vm_map_ram(pages, count, -1);
	else
		return vmap(pages, count, flags, prot);
#endif /* !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS) */
}

static inline void pvr_vunmap(void *pages,
			      __maybe_unused unsigned int count,
			      __maybe_unused pgprot_t prot)
{
#if !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS)
	vunmap(pages);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	vm_unmap_ram(pages, count);
#else
	if (pgprot_val(prot) == pgprot_val(PAGE_KERNEL))
		vm_unmap_ram(pages, count);
	else
		vunmap(pages);
#endif /* !defined(CONFIG_64BIT) || defined(PVRSRV_FORCE_SLOWER_VMAP_ON_64BIT_BUILDS) */
}

#endif /* PVR_VMAP_H */
