#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/log2.h>
#include <asm/uaccess.h>
#include "xpr.h"

static void xpr_sync_super(struct super_block *sb,
			struct xpr_super_block *xpr_sb);


static void xpr_put_super(struct super_block *sb){

	struct xpr_sb_info *sbi=XPR_SB(sb);

	brelse(sbi->s_sbh);
	sb->s_fs_info=NULL;
	kfree(sbi);

}	

static struct kmem_cache *xpr_inode_cachep;

static struct inode *xpr_alloc_inode(struct super_block *sb){
	struct xpr_inode_info *xpr_ino_i;

	xpr_ino_i=(struct xpr_inode_info *)kmem_cache_alloc(xpr_inode_cachep,GFP_KERNEL);
	if(!xpr_ino_i)
		return NULL;
	
	return &xpr_ino_i->vfs_inode;

}

static void destroy_inodecache(void){
         kmem_cache_destroy(xpr_inode_cachep);
}

static void init_once(void *foo){
	struct xpr_inode_info *xpr_ino_i=(struct xpr_inode_info *)foo;
	inode_init_once(&xpr_ino_i->vfs_inode);
}

static int init_inodecache(void){
	xpr_inode_cachep=kmem_cache_create("xpr_inode_cache",sizeof(struct xpr_inode_info),0,(SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD),init_once);
	if(xpr_inode_cachep==NULL)
		return -ENOMEM;
	return 0;
}

static void xpr_destroy_inode(struct inode *inode){
	kmem_cache_free(xpr_inode_cachep,XPR_INO_I(inode));
}

static int xpr_write_inode(struct inode *inode,int wait){
	brelse(xpr_update_inode(inode));
	return 0;
}

static void xpr_delete_inode(struct inode *inode){
	truncate_inode_pages(&inode->i_data,0);
	XPR_INO_I(inode)->i_dtime=get_seconds();
	inode->i_size=0;
	xpr_truncate(inode);
	xpr_free_inode(inode);
}

static int xpr_statfs(struct dentry *dentry,struct kstatfs *buf){
	struct xpr_sb_info * sbi=XPR_SB(dentry->d_sb);
	struct xpr_super_block *xpr_sb=sbi->s_xs;
	buf->f_type=dentry->d_sb->s_magic;
	buf->f_bsize=dentry->d_sb->s_blocksize;
	buf->f_blocks=(xpr_sb->s_blk_cnt-xpr_sb->s_fdblk);
	buf->f_bfree=xpr_count_free_blocks(sbi);
	buf->f_bavail=buf->f_bfree;
	buf->f_ffree=xpr_count_free_inodes(sbi);
	buf->f_namelen=XPR_NAME_LEN;
	return 0;
}


static void xpr_write_super(struct super_block *sb){
	struct xpr_super_block *xpr_sb;
	lock_kernel();
	xpr_sb=XPR_SB(sb)->s_xs;
	xpr_sb->s_fblk_cnt=cpu_to_le32(xpr_count_free_blocks(sb));
	xpr_sb->s_fino_cnt=cpu_to_le32(xpr_count_free_inodes(sb));
	xpr_sb->s_mtime=cpu_to_le32(get_seconds());
	xpr_sb->s_wtime = cpu_to_le32(get_seconds());
	mark_buffer_dirty(XPR_SB(sb)->s_sbh);
	sync_dirty_buffer(XPR_SB(sb)->s_sbh);
	sb->s_dirt=0;	
	unlock_kernel();
}

static const struct super_operations xpr_sops={
	.alloc_inode	=xpr_alloc_inode,
	.destroy_inode	=xpr_destroy_inode,
	.write_inode	=xpr_write_inode,
	.delete_inode	=xpr_delete_inode,
	.write_super    =xpr_write_super,
	.put_super	=xpr_put_super,
	.statfs		=xpr_statfs,
};


static int xpr_fill_super(struct super_block *sb,void *data,int silent){

	struct buffer_head *bh;
	struct xpr_super_block *xpr_sb;	struct xpr_sb_info *sbi;
	struct inode *root;
	
	unsigned long sb_block=1;

	long ret=-EINVAL;
	
	int blocksize=XPR_BLOCK_SIZE;

	sbi=kzalloc(sizeof(struct xpr_sb_info),GFP_KERNEL);
	if(!sbi)
		return -ENOMEM;

	if(!sb_set_blocksize(sb,XPR_BLOCK_SIZE))
		goto blk_err;
	if(!(bh=sb_bread(sb,sb_block))){
		printk("XPR-fs:unable to read superblock\n");
		goto sbi_err;
	}

	xpr_sb=(struct xpr_super_block *)(bh->b_data);
	sbi->s_sbh=bh;
	sbi->s_xs=xpr_sb;
	sb->s_fs_info=sbi;
	sb->s_magic=xpr_sb->s_magic;

	if(sb->s_magic !=XPR_MAGIC)
		goto no_xpr_err;

	blocksize=XPR_BLOCK_SIZE;

	sb->s_op=&xpr_sops;
	
	root=xpr_iget(sb,XPR_ROOT_INO);
	if(IS_ERR(root)){
		ret=PTR_ERR(root);
		printk(KERN_ERR "XPR-fs: can't find root inode\n");
		goto mount_err;	
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		printk(KERN_ERR "isdir?%d,root->i_blocks=%d,root->i_size=%d\n",S_ISDIR(root->i_mode) , root->i_blocks, root->i_size);
		printk(KERN_ERR "XPR-fs: corrupt root inode\n");
		goto mount_err;
	}

	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		printk(KERN_ERR "XPR: get root inode failed\n");
		ret = -ENOMEM;
		goto mount_err;
	}
	
	return 0;
no_xpr_err:
	printk("VFS: No xpr filesystem on %s.\n",sb->s_id);
mount_err:
	brelse(bh);
blk_err:
	printk("XPRFS:Blocksize too small\n");
sbi_err:
	sb->s_fs_info=NULL;
	kfree(sbi);
	return ret;
}
/*
static void xpr_sync_super(struct super_block *sb,struct xpr_super_block *xpr_sb){
	xpr_sb->s_fblk_cnt = cpu_to_le32(xpr_count_free_blocks(sb));
	xpr_sb->s_fino_cnt = cpu_to_le32(xpr_count_free_inodes(sb));
	xpr_sb->s_wtime = cpu_to_le32(get_seconds());
	mark_buffer_dirty(XPR_SB(sb)->s_sbh);
	sync_dirty_buffer(XPR_SB(sb)->s_sbh);
	sb->s_dirt = 0;
}
*/

static int xpr_get_sb(struct file_system_type *fs_type,
int flags,const char *dev_name,void *data,struct vfsmount *mnt){
	return get_sb_bdev(fs_type,flags,dev_name,data,xpr_fill_super,mnt);
}



static struct file_system_type xpr_fs_type ={
	.owner		=THIS_MODULE,
	.name		="xprfs",
	.get_sb		=xpr_get_sb,
	.kill_sb	=kill_block_super,
	.fs_flags	=FS_REQUIRES_DEV,	
};


static int __init init_xpr_fs(void){
	int err=init_inodecache();
	if(err)
		return err;
	err=register_filesystem(&xpr_fs_type);
	if(err)
		goto out;
	return 0;
out:
	destroy_inodecache();
	return err;
}


static void __exit exit_xpr_fs(void){
	unregister_filesystem(&xpr_fs_type);
	destroy_inodecache();
}

module_init(init_xpr_fs)
module_exit(exit_xpr_fs)
