/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include "chiffrefs.h"
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/splice.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "internal.h"


#define CHIFFREFS_DEFAULT_MODE	0755

extern ssize_t generic_file_splice_read(struct file *, loff_t *,
					struct pipe_inode_info *, size_t, unsigned int);
extern ssize_t default_file_splice_read(struct file *, loff_t *,
					struct pipe_inode_info *, size_t, unsigned int);
extern ssize_t iter_file_splice_write(struct pipe_inode_info *,
				      struct file *, loff_t *, size_t, unsigned int);
extern ssize_t generic_splice_sendpage(struct pipe_inode_info *pipe,
				       struct file *out, loff_t *, size_t len, unsigned int flags);
extern long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
			     loff_t *opos, size_t len, unsigned int flags);


extern ssize_t iter_file_splice_write(struct pipe_inode_info *,
				      struct file *, loff_t *, size_t, unsigned int);

/* extern ssize_t new_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos); */
/* extern int __set_page_dirty_no_writeback(struct page *page); */
static const struct super_operations chiffrefs_ops;
static const struct inode_operations chiffrefs_dir_inode_operations;

static const struct address_space_operations chiffrefs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_no_writeback,
};

static struct backing_dev_info chiffrefs_backing_dev_info = {
	.name		= "chiffrefs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK |
			  BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
			  BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

int __set_page_dirty_no_writeback(struct page *page)
{
  if (!PageDirty(page))
    return !TestSetPageDirty(page);
  return 0;
}
  
struct inode *chiffrefs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &chiffrefs_aops;
		inode->i_mapping->backing_dev_info = &chiffrefs_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &chiffrefs_file_inode_operations;
			inode->i_fop = &chiffrefs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &chiffrefs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
chiffrefs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = chiffrefs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int chiffrefs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = chiffrefs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int chiffrefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return chiffrefs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int chiffrefs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = chiffrefs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}

static const struct inode_operations chiffrefs_dir_inode_operations = {
	.create		= chiffrefs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= chiffrefs_symlink,
	.mkdir		= chiffrefs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= chiffrefs_mknod,
	.rename		= simple_rename,
};

static const struct super_operations chiffrefs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= generic_show_options,
};

struct chiffrefs_mount_opts {
	umode_t mode;
};

enum {
	Opt_mode,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct chiffrefs_fs_info {
	struct chiffrefs_mount_opts mount_opts;
};

static int chiffrefs_parse_options(char *data, struct chiffrefs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	char *p;

	opts->mode = CHIFFREFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally chiffrefs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		}
	}

	return 0;
}

int chiffrefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct chiffrefs_fs_info *fsi;
	struct inode *inode;
	int err;

	save_mount_options(sb, data);

	fsi = kzalloc(sizeof(struct chiffrefs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi)
		return -ENOMEM;

	err = chiffrefs_parse_options(data, &fsi->mount_opts);
	if (err)
		return err;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= CHIFFREFS_MAGIC;
	sb->s_op		= &chiffrefs_ops;
	sb->s_time_gran		= 1;

	inode = chiffrefs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

struct dentry *chiffrefs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, chiffrefs_fill_super);
}

static void chiffrefs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type chiffrefs_fs_type = {
	.name		= "chiffrefs",
	.mount		= chiffrefs_mount,
	.kill_sb	= chiffrefs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init init_chiffrefs_fs(void)
{
	static unsigned long once;
	int err;

	if (test_and_set_bit(0, &once))
		return 0;

	err = bdi_init(&chiffrefs_backing_dev_info);
	if (err)
		return err;

	err = register_filesystem(&chiffrefs_fs_type);
	if (err)
		bdi_destroy(&chiffrefs_backing_dev_info);

	return err;
}
fs_initcall(init_chiffrefs_fs);
