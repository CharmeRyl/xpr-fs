#include <linux/time.h>
#include <linux/buffer_head.h>
#include "xpr.h"

int xpr_sync_file(struct file *file,struct dentry *dentry,int datasync){
	struct inode *inode =dentry->d_inode;
	int err,ret;
	ret=sync_mapping_buffers(inode->i_mapping);
	if(!(inode->i_state&I_DIRTY))
		return ret;
	if(datasync && !(inode->i_state&I_DIRTY_DATASYNC))
		return ret;
	err=xpr_sync_inode(inode);
	if(ret==0)
		ret=err;
	return ret;
}

const struct file_operations xpr_file_operations ={
	.llseek		=generic_file_llseek,
	.read		=do_sync_read,
	.write		=do_sync_write,
	.aio_read	=generic_file_aio_read,
	.aio_write	=generic_file_aio_write,
	.mmap		=generic_file_mmap,
	.open		=generic_file_open,
	.fsync		=xpr_sync_file,
};

const struct inode_operations xpr_file_inode_operations ={
	.truncate	=xpr_truncate,
};
