#ifndef _LINUX_RAMFS_H
#define _LINUX_CHIFFREFS_H

#define CHIFFREFS_MAGIC             0x858458f8

struct inode *chiffrefs_get_inode(struct super_block *sb, const struct inode *dir,
	 umode_t mode, dev_t dev);
extern struct dentry *chiffrefs_mount(struct file_system_type *fs_type,
	 int flags, const char *dev_name, void *data);

#ifdef CONFIG_MMU
static inline int
chiffrefs_nommu_expand_for_mapping(struct inode *inode, size_t newsize)
{
	return 0;
}
#else
extern int chiffrefs_nommu_expand_for_mapping(struct inode *inode, size_t newsize);
#endif

extern const struct file_operations chiffrefs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
extern int __init init_chiffrefs_fs(void);

int chiffrefs_fill_super(struct super_block *sb, void *data, int silent);

#endif
