/* file-mmu.c: chiffrefs MMU-based file operations
 *
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


#include <linux/mm.h>
#include "chiffrefs.h"

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/log2.h>
#include <linux/mount.h>
#include <linux/magic.h>
#include <linux/pipe_fs_i.h>
#include <linux/uio.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/audit.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/aio.h>

#include <linux/fs.h>
#include <linux/mm.h>

#include "internal.h"

#define DIFF 10

ssize_t new_chiffre_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
  struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
  struct kiocb kiocb;
  struct iov_iter iter;
  ssize_t ret;

  char *t;
  for (t = (char*)buf; t < ppos; t++)
    *t += DIFF;
  init_sync_kiocb(&kiocb, filp);
  kiocb.ki_pos = *ppos;
  kiocb.ki_nbytes = len;
  iov_iter_init(&iter, WRITE, &iov, 1, len);
  
  ret = filp->f_op->write_iter(&kiocb, &iter);
  if (-EIOCBQUEUED == ret)
    ret = wait_on_sync_kiocb(&kiocb);
  *ppos = kiocb.ki_pos;
  return ret;
}


const struct file_operations chiffrefs_file_operations = {
	.read		= new_sync_read,
	//	.read_iter	= generic_file_read_iter,
	.write		= new_sync_write,
	//.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
};

const struct inode_operations chiffrefs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};
