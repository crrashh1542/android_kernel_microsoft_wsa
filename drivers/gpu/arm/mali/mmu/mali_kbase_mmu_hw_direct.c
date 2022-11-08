// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <device/mali_kbase_device.h>
#include <linux/bitops.h>
#include <mali_kbase.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_mem.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <tl/mali_kbase_tracepoints.h>
#include <linux/delay.h>

/**
 * lock_region() - Generate lockaddr to lock memory region in MMU
 *
 * @gpu_props: GPU properties for finding the MMU lock region size.
 * @lockaddr:  Address and size of memory region to lock.
 * @op_param:  Pointer to a struct containing the starting page frame number of
 *             the region to lock, the number of pages to lock and page table
 *             levels to skip when flushing (if supported).
 *
 * The lockaddr value is a combination of the starting address and
 * the size of the region that encompasses all the memory pages to lock.
 *
 * The size is expressed as a logarithm: it is represented in a way
 * that is compatible with the HW specification and it also determines
 * how many of the lowest bits of the address are cleared.
 *
 * Return: 0 if success, or an error code on failure.
 */
static int lock_region(struct kbase_gpu_props const *gpu_props, u64 *lockaddr,
		       const struct kbase_mmu_hw_op_param *op_param)
{

	const u64 lockaddr_base = op_param->vpfn << PAGE_SHIFT;
	const u64 lockaddr_end =
		((op_param->vpfn + op_param->nr) << PAGE_SHIFT) - 1;
	u64 lockaddr_size_log2;

	if (op_param->nr == 0)
		return -EINVAL;

	/* The size is expressed as a logarithm and should take into account
	 * the possibility that some pages might spill into the next region.
	 */

	lockaddr_size_log2 = fls64(lockaddr_base ^ lockaddr_end);

	if (lockaddr_size_log2 > KBASE_LOCK_REGION_MAX_SIZE_LOG2)
		return -EINVAL;

	/* The lowest bits are cleared and then set to size - 1 to represent
	 * the size in a way that is compatible with the HW specification.
	 */
	*lockaddr = lockaddr_base & ~((1ull << lockaddr_size_log2) - 1);
	*lockaddr |= lockaddr_size_log2 - 1;

	return 0;
}

static int wait_ready(struct kbase_device *kbdev,
		unsigned int as_nr)
{
	u32 max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;

	/* Wait for the MMU status to indicate there is no active command. */
	while (--max_loops &&
	       kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS)) &
		       AS_STATUS_AS_ACTIVE) {
		;
	}

	if (WARN_ON_ONCE(max_loops == 0)) {
		dev_err(kbdev->dev,
			"AS_ACTIVE bit stuck for as %u, might be caused by slow/unstable GPU clock or possible faulty FPGA connector",
			as_nr);
		return -1;
	}

	return 0;
}

static int write_cmd(struct kbase_device *kbdev, int as_nr, u32 cmd)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(kbdev, as_nr);
	if (status == 0)
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_COMMAND), cmd);
	else {
		dev_err(kbdev->dev,
			"Wait for AS_ACTIVE bit failed for as %u, before sending MMU command %u",
			as_nr, cmd);
	}

	return status;
}

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as)
{
	struct kbase_mmu_setup *current_setup = &as->current_setup;
	u64 transcfg = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	transcfg = current_setup->transcfg;

	/* Set flag AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK
	 * Clear PTW_MEMATTR bits
	 */
	transcfg &= ~AS_TRANSCFG_PTW_MEMATTR_MASK;
	/* Enable correct PTW_MEMATTR bits */
	transcfg |= AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK;
	/* Ensure page-tables reads use read-allocate cache-policy in
	 * the L2
	 */
	transcfg |= AS_TRANSCFG_R_ALLOCATE;

	if (kbdev->system_coherency != COHERENCY_NONE) {
		/* Set flag AS_TRANSCFG_PTW_SH_OS (outer shareable)
		 * Clear PTW_SH bits
		 */
		transcfg = (transcfg & ~AS_TRANSCFG_PTW_SH_MASK);
		/* Enable correct PTW_SH bits */
		transcfg = (transcfg | AS_TRANSCFG_PTW_SH_OS);
	}

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_LO),
			transcfg);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_HI),
			(transcfg >> 32) & 0xFFFFFFFFUL);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_LO),
			current_setup->transtab & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_HI),
			(current_setup->transtab >> 32) & 0xFFFFFFFFUL);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_LO),
			current_setup->memattr & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_HI),
			(current_setup->memattr >> 32) & 0xFFFFFFFFUL);

	KBASE_TLSTREAM_TL_ATTRIB_AS_CONFIG(kbdev, as,
			current_setup->transtab,
			current_setup->memattr,
			transcfg);

	write_cmd(kbdev, as->number, AS_COMMAND_UPDATE);
#if MALI_USE_CSF
	/* Wait for UPDATE command to complete */
	wait_ready(kbdev, as->number);
#endif
}

/* Helper function to program the LOCKADDR register before LOCK/UNLOCK command
 * is issued.
 */
static int mmu_hw_set_lock_addr(struct kbase_device *kbdev, int as_nr,
				u64 *lock_addr,
				const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = lock_region(&kbdev->gpu_props, lock_addr, op_param);

	if (!ret) {
		/* Set the region that needs to be updated */
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_LOCKADDR_LO),
				*lock_addr & 0xFFFFFFFFUL);
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_LOCKADDR_HI),
				(*lock_addr >> 32) & 0xFFFFFFFFUL);
	}
	return ret;
}

/**
 * mmu_hw_do_lock_no_wait - Issue LOCK command to the MMU and return without
 *                          waiting for it's completion.
 *
 * @kbdev:      Kbase device to issue the MMU operation on.
 * @as:         Address space to issue the MMU operation on.
 * @lock_addr:  Address of memory region locked for this operation.
 * @op_param:   Pointer to a struct containing information about the MMU operation.
 *
 * Return: 0 if issuing the command was successful, otherwise an error code.
 */
static int mmu_hw_do_lock_no_wait(struct kbase_device *kbdev,
				  struct kbase_as *as, u64 *lock_addr,
				  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, lock_addr, op_param);

	if (!ret)
		write_cmd(kbdev, as->number, AS_COMMAND_LOCK);

	return ret;
}

int kbase_mmu_hw_do_unlock_no_addr(struct kbase_device *kbdev,
				   struct kbase_as *as,
				   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK);

	/* Wait for UNLOCK command to complete */
	if (!ret)
		ret = wait_ready(kbdev, as->number);

	return ret;
}

int kbase_mmu_hw_do_unlock(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;
	u64 lock_addr = 0x0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, &lock_addr, op_param);

	if (!ret)
		ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, op_param);

	return ret;
}

static int mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param,
			   bool hwaccess_locked)
{
	int ret;
	u64 lock_addr = 0x0;
	u32 mmu_cmd = AS_COMMAND_FLUSH_MEM;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	/* MMU operations can be either FLUSH_PT or FLUSH_MEM, anything else at
	 * this point would be unexpected.
	 */
	if (op_param->op != KBASE_MMU_OP_FLUSH_PT &&
	    op_param->op != KBASE_MMU_OP_FLUSH_MEM) {
		dev_err(kbdev->dev, "Unexpected flush operation received");
		return -EINVAL;
	}

	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	if (op_param->op == KBASE_MMU_OP_FLUSH_PT)
		mmu_cmd = AS_COMMAND_FLUSH_PT;

	/* Lock the region that needs to be updated */
	ret = mmu_hw_do_lock_no_wait(kbdev, as, &lock_addr, op_param);
	if (ret)
		return ret;

	write_cmd(kbdev, as->number, mmu_cmd);

	/* Wait for the command to complete */
	ret = wait_ready(kbdev, as->number);

	return ret;
}

int kbase_mmu_hw_do_flush_locked(struct kbase_device *kbdev,
				 struct kbase_as *as,
				 const struct kbase_mmu_hw_op_param *op_param)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return mmu_hw_do_flush(kbdev, as, op_param, true);
}

int kbase_mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param)
{
	return mmu_hw_do_flush(kbdev, as, op_param, false);
}

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 pf_bf_mask;

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	/* Clear the page (and bus fault IRQ as well in case one occurred) */
	pf_bf_mask = MMU_PAGE_FAULT(as->number);
#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		pf_bf_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), pf_bf_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 irq_mask;

	/* Enable the page fault IRQ
	 * (and bus fault IRQ as well in case one occurred)
	 */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	irq_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK)) |
			MMU_PAGE_FAULT(as->number);

#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		irq_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), irq_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}
