#include <linux/fs.h>
#include <linux/types.h>
#include "xpr_fs.h"

struct xpr_inode_info{
	__le32 i_blk_start;
	__le32 i_blk_end;
	__le32 i_blocks;
	__le32 i_reserved;
	__u32 i_flags;
	__u32 i_dtime;
	struct mutex truncate_mutex;
	struct inode vfs_inode;
};

static inline struct xpr_inode_info *XPR_INO_I(struct inode *inode){
	return container_of(inode,struct xpr_inode_info,vfs_inode);
}


static inline struct xpr_sb_info * XPR_SB(struct super_block *sb){
	return sb->s_fs_info;
}

extern struct inode *xpr_iget(struct super_block *, unsigned long);
extern struct xpr_inode * xpr_raw_inode(struct super_block *, ino_t, struct buffer_head **);

extern struct inode *xpr_new_inode(struct inode *dir,int *err);
extern void xpr_free_inode(struct inode * inode);
extern unsigned long xpr_count_free_inodes(struct super_block *sb);

//extern void xpr_free_block(struct inode *inode, unsigned long block);
extern unsigned long xpr_count_free_blocks(struct super_block *sb);

extern int __xpr_write_begin(struct file *file,struct address_space *mapping,loff_t pos,unsigned len,unsigned flags,struct page *pagep,void **fsdata);
extern void xpr_truncate(struct inode *);
extern int xpr_sync_inode(struct inode *);
extern struct buffer_head *xpr_update_inode(struct inode *inode);
extern void xpr_set_inode(struct inode *, dev_t);
extern int xpr_get_block(struct inode *inode,sector_t iblock,struct buffer_head *bh_result,int create);

//extern unsigned xpr_blocks(loff_t, struct super_block *);
extern inline void xpr_put_page(struct page * page);
extern struct xpr_dentry *xpr_find_entry(struct dentry*, struct page**);
extern int xpr_add_link(struct dentry*, struct inode*);
extern int xpr_delete_entry(struct xpr_dentry*, struct page*);
extern int xpr_make_empty(struct inode*, struct inode*);
extern int xpr_empty_dir(struct inode*);
extern void xpr_set_link(struct xpr_dentry *, struct page *,struct inode *);
extern struct xpr_dentry *xpr_dotdot(struct inode*, struct page**);
extern int xpr_sync_file(struct file *, struct dentry *, int);


/*
 * Inodes and files operations
 */
 
/* namei.c */
extern const struct inode_operations xpr_dir_inode_operations;

/* inode.c */
extern const struct address_space_operations xpr_aops;

/* dir.c */
extern const struct file_operations xpr_dir_operations;

/* file.c */
extern const struct inode_operations xpr_file_inode_operations;
extern const struct file_operations xpr_file_operations;


