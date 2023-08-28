/*
 * Linux Security Module for Chromium OS
 *
 * Copyright 2011 Google Inc. All Rights Reserved
 *
 * Authors:
 *      Stephan Uphoff  <ups@google.com>
 *      Kees Cook       <keescook@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "Chromium OS LSM: " fmt

#include <asm/syscall.h>
#include <linux/audit.h>
#include <linux/binfmts.h>
#include <linux/cred.h>
#include <linux/device-mapper.h>
#include <linux/fs.h>
#include <linux/fs_parser.h>
#include <linux/fs_struct.h>
#include <linux/lsm_hooks.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>	/* for nameidata_get_total_link_count */
#include <linux/path.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <linux/sched.h>	/* current and other task related stuff */
#include <linux/security.h>
#include <linux/shmem_fs.h>
#include <uapi/linux/mount.h>

#include "inode_mark.h"
#include "utils.h"

#if defined(CONFIG_SECURITY_CHROMIUMOS_NO_UNPRIVILEGED_UNSAFE_MOUNTS) || \
	defined(CONFIG_SECURITY_CHROMIUMOS_NO_SYMLINK_MOUNT)
static void report(const char *origin, const struct path *path, char *operation)
{
	char *alloced = NULL, *cmdline;
	char *pathname; /* Pointer to either static string or "alloced". */

	if (!path)
		pathname = "<unknown>";
	else {
		/* We will allow 11 spaces for ' (deleted)' to be appended */
		alloced = pathname = kmalloc(PATH_MAX+11, GFP_KERNEL);
		if (!pathname)
			pathname = "<no_memory>";
		else {
			pathname = d_path(path, pathname, PATH_MAX+11);
			if (IS_ERR(pathname))
				pathname = "<too_long>";
			else {
				pathname = printable(pathname, PATH_MAX+11);
				kfree(alloced);
				alloced = pathname;
			}
		}
	}

	cmdline = printable_cmdline(current);

	pr_notice("%s %s obj=%s pid=%d cmdline=%s\n", origin,
		  operation, pathname, task_pid_nr(current), cmdline);

	kfree(cmdline);
	kfree(alloced);
}
#endif

static int chromiumos_security_sb_mount(const char *dev_name,
					const struct path *path,
					const char *type, unsigned long flags,
					void *data)
{
#ifdef CONFIG_SECURITY_CHROMIUMOS_NO_SYMLINK_MOUNT
	if (!(path->link_count & PATH_LINK_COUNT_VALID)) {
		WARN(1, "No link count available");
		return -ELOOP;
	} else if (path->link_count & ~PATH_LINK_COUNT_VALID) {
		report("sb_mount", path, "Mount path with symlinks prohibited");
		pr_notice("sb_mount dev=%s type=%s flags=%#lx\n",
			  dev_name, type, flags);
		return -ELOOP;
	}
#endif

#ifdef CONFIG_SECURITY_CHROMIUMOS_NO_UNPRIVILEGED_UNSAFE_MOUNTS
	if ((!(flags & (MS_BIND | MS_MOVE | MS_SHARED | MS_PRIVATE | MS_SLAVE |
			MS_UNBINDABLE)) ||
	     ((flags & MS_REMOUNT) && (flags & MS_BIND))) &&
	    !capable(CAP_SYS_ADMIN)) {
		int required_mnt_flags = MNT_NOEXEC | MNT_NOSUID | MNT_NODEV;

		if (flags & MS_REMOUNT) {
			/*
			 * If this is a remount, we only require that the
			 * requested flags are a superset of the original mount
			 * flags. In addition, using nosymfollow is not
			 * initially required, but remount is not allowed to
			 * remove it.
			 */
			required_mnt_flags |= MNT_NOSYMFOLLOW;
			required_mnt_flags &= path->mnt->mnt_flags;
		}
		/*
		 * The three flags we are interested in disallowing in
		 * unprivileged user namespaces (MS_NOEXEC, MS_NOSUID, MS_NODEV)
		 * cannot be modified when doing a bind-mount. The kernel
		 * attempts to dispatch calls to do_mount() within
		 * fs/namespace.c in the following order:
		 *
		 * * If the MS_REMOUNT flag is present, it calls do_remount().
		 *   When MS_BIND is also present, it only allows to modify the
		 *   per-mount flags, which are copied into
		 *   |required_mnt_flags|.  Otherwise it bails in the absence of
		 *   the CAP_SYS_ADMIN in the init ns.
		 * * If the MS_BIND flag is present, the only other flag checked
		 *   is MS_REC.
		 * * If any of the mount propagation flags are present
		 *   (MS_SHARED, MS_PRIVATE, MS_SLAVE, MS_UNBINDABLE),
		 *   flags_to_propagation_type() filters out any additional
		 *   flags.
		 * * If MS_MOVE flag is present, all other flags are ignored.
		 */
		if ((required_mnt_flags & MNT_NOEXEC) && !(flags & MS_NOEXEC)) {
			report("sb_mount", path,
			       "Mounting a filesystem with 'exec' flag requires CAP_SYS_ADMIN in init ns");
			pr_notice("sb_mount dev=%s type=%s flags=%#lx\n",
				  dev_name, type, flags);
			return -EPERM;
		}
		if ((required_mnt_flags & MNT_NOSUID) && !(flags & MS_NOSUID)) {
			report("sb_mount", path,
			       "Mounting a filesystem with 'suid' flag requires CAP_SYS_ADMIN in init ns");
			pr_notice("sb_mount dev=%s type=%s flags=%#lx\n",
				  dev_name, type, flags);
			return -EPERM;
		}
		if ((required_mnt_flags & MNT_NODEV) && !(flags & MS_NODEV) &&
		    strcmp(type, "devpts")) {
			report("sb_mount", path,
			       "Mounting a filesystem with 'dev' flag requires CAP_SYS_ADMIN in init ns");
			pr_notice("sb_mount dev=%s type=%s flags=%#lx\n",
				  dev_name, type, flags);
			return -EPERM;
		}
	}
#endif

	return 0;
}

/*
 * NOTE: The WARN() calls will emit a warning in cases of blocked symlink
 * traversal attempts. These will show up in kernel warning reports
 * collected by the crash reporter, so we have some insight on spurious
 * failures that need addressing.
 */
static int chromiumos_security_inode_follow_link(struct dentry *dentry,
						 struct inode *inode, bool rcu)
{
	static char accessed_path[PATH_MAX];
	enum chromiumos_inode_security_policy policy;

	policy = chromiumos_get_inode_security_policy(
		dentry, inode,
		CHROMIUMOS_SYMLINK_TRAVERSAL);

	WARN(policy == CHROMIUMOS_INODE_POLICY_BLOCK,
	     "Blocked symlink traversal for path %x:%x:%s (see https://goo.gl/8xICW6 for context and rationale)\n",
	     MAJOR(dentry->d_sb->s_dev), MINOR(dentry->d_sb->s_dev),
	     dentry_path(dentry, accessed_path, PATH_MAX));

	return policy == CHROMIUMOS_INODE_POLICY_BLOCK ? -EACCES : 0;
}

#define DM_LOCKED_PREFIX "dm_locked-"
static bool chromiumos_locked_down_dm_device(dev_t dev)
{
	bool ret = false;
	struct mapped_device *md;
	char dm_uuid[DM_UUID_LEN]; /* 129 bytes */

	md = dm_get_md(dev);
	if (!md)
		return false;

	if (!dm_copy_name_and_uuid(md, NULL, dm_uuid) &&
			str_has_prefix(dm_uuid, DM_LOCKED_PREFIX))
			ret = true;

	dm_put(md);
	return ret;
}

static int chromiumos_security_file_open(struct file *file)
{
	static char accessed_path[PATH_MAX];
	enum chromiumos_inode_security_policy policy;
	struct dentry *dentry = file->f_path.dentry;

	/* if it's a dm block device that's locked down return -EPERM */
	if (S_ISBLK(file->f_inode->i_mode) &&
			chromiumos_locked_down_dm_device(file->f_inode->i_rdev))
		return -EPERM;

	/* Returns 0 if file is not a FIFO */
	if (!S_ISFIFO(file->f_inode->i_mode))
		return 0;

	policy = chromiumos_get_inode_security_policy(
		dentry, dentry->d_inode,
		CHROMIUMOS_FIFO_ACCESS);

	/*
	 * Emit a warning in cases of blocked fifo access attempts. These will
	 * show up in kernel warning reports collected by the crash reporter,
	 * so we have some insight on spurious failures that need addressing.
	 */
	WARN(policy == CHROMIUMOS_INODE_POLICY_BLOCK,
	     "Blocked fifo access for path %x:%x:%s\n (see https://goo.gl/8xICW6 for context and rationale)\n",
	     MAJOR(dentry->d_sb->s_dev), MINOR(dentry->d_sb->s_dev),
	     dentry_path(dentry, accessed_path, PATH_MAX));

	return policy == CHROMIUMOS_INODE_POLICY_BLOCK ? -EACCES : 0;
}

static int chromiumos_sb_eat_lsm_opts(char *options, void **mnt_opts)
{
	char *from = options, *to = options;
	bool found = false;
	bool first = true;

	while (1) {
		char *next = strchr(from, ',');
		int len;

		if (next)
			len = next - from;
		else
			len = strlen(from);

		/*
		 * Remove the option so that filesystems won't see it.
		 * do_mount() has already forced the MS_NOSYMFOLLOW flag on
		 * if it found this option, so no other action is needed.
		 */
		if (len == strlen("nosymfollow") && !strncmp(from, "nosymfollow", len)) {
			found = true;
		} else {
			if (!first) {   /* copy with preceding comma */
				from--;
				len++;
			}
			if (to != from)
				memmove(to, from, len);
			to += len;
			first = false;
		}
		if (!next)
			break;
		from += len + 1;
	}
	*to = '\0';

	if (found)
		pr_notice("nosymfollow option should be changed to MS_NOSYMFOLLOW flag.");

	return 0;
}

static int chromiumos_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	struct file *file = bprm->file;

	if (shmem_file(file)) {
		char *cmdline = printable_cmdline(current);

		audit_log(
			audit_context(),
			GFP_ATOMIC,
			AUDIT_AVC,
			"ChromeOS LSM: memfd execution attempt, cmd=%s, pid=%d",
			cmdline ? cmdline : "(null)",
			task_pid_nr(current));
		kfree(cmdline);

		pr_notice_ratelimited("memfd execution blocked\n");
		return -EACCES;
	}
	return 0;
}

static int chromiumos_locked_down(enum lockdown_reason what)
{
	if (what == LOCKDOWN_BPF_WRITE_USER) {
		pr_notice_ratelimited("BPF_WRITE_USER blocked\n");
		return -EACCES;
	}

	return 0;
}

/*
 * This specific function will prevent mknod of 3 specific device mapper devices.
 * If an attempt is made to mknod hiberimage, hiberintegrity, or hiberimage_integrity it will
 * fail with a -EPERM.
 *
 * When device mapper first creates a device using dmsetup the node created is a dm-N node, this
 * happens before a table has been made live. Once the table has been made live a symbolic link is
 * created in /dev/mapper/DM_NAME pointing to the dm-N node that was previously created.
 * This method specifically queries the name of the dm device, that is, it's a no-op if the device
 * mapper device has no table (and thus no name). Once a table has been established if the name of the
 * device is one of the three restricted ones any future mknod will be rejected with -EPERM.
 *
 * The typical flow would be: establish the dm-crypt/dm-integrity hibernate volumes. Once they are
 * created they are opened by the kernel using the /dev/snapshot set-device ioctl. When the kernel
 * has it opened it will then be unlinked from the file system and once it has been unlink since
 * we're blocking mknod there will be no way to recreate the node.
 *
 */
static int chromiumos_security_dm_mknod(struct dentry *dentry, umode_t mode, dev_t dev)
{
	/* if it's a dm block device that's locked down, return -EPERM */
	if (S_ISBLK(mode) && chromiumos_locked_down_dm_device(dev))
		return -EPERM;

	return 0;
}

static int chromiumos_security_path_mknod(const struct path *const dir,
			   struct dentry *const dentry, const umode_t mode,
			   const unsigned int dev)
{
	return chromiumos_security_dm_mknod(dentry, mode, new_decode_dev(dev));
}

static int chromiumos_security_inode_mknod(struct inode *dir, struct dentry *dentry,
		umode_t mode, dev_t dev)
{
	return chromiumos_security_dm_mknod(dentry, mode, dev);
}


static struct security_hook_list chromiumos_security_hooks[] = {
	LSM_HOOK_INIT(sb_mount, chromiumos_security_sb_mount),
	LSM_HOOK_INIT(inode_follow_link, chromiumos_security_inode_follow_link),
	LSM_HOOK_INIT(file_open, chromiumos_security_file_open),
	LSM_HOOK_INIT(sb_eat_lsm_opts, chromiumos_sb_eat_lsm_opts),
	LSM_HOOK_INIT(bprm_creds_for_exec, chromiumos_bprm_creds_for_exec),
	LSM_HOOK_INIT(locked_down, chromiumos_locked_down),
	LSM_HOOK_INIT(path_mknod, chromiumos_security_path_mknod),
	LSM_HOOK_INIT(inode_mknod, chromiumos_security_inode_mknod),
};

static int __init chromiumos_security_init(void)
{
	security_add_hooks(chromiumos_security_hooks,
			   ARRAY_SIZE(chromiumos_security_hooks), "chromiumos");

	pr_info("enabled");

	return 0;
}
DEFINE_LSM(chromiumos) = {
	.name = "chromiumos",
	.init = chromiumos_security_init
};
