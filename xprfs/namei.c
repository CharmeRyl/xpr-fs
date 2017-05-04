#include <linux/pagemap.h>
#include "xpr.h"

static int add_nondir(struct dentry *dentry,struct inode *inode){
	
	int err=xpr_add_link(dentry,inode);
	if(!err){
		d_instantiate(dentry,inode);
		return 0;	
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}



static int xpr_link(struct dentry *old_dentry,struct inode *dir,struct dentry *dentry){
	struct xpr_super_block *xpr_sb=XPR_SB(dir->i_sb)->s_xs;
	struct inode *inode=old_dentry->d_inode;
	if(inode->i_nlink>xpr_sb->s_link_max)
		return -EMLINK;
	inode->i_mtime=CURRENT_TIME_SEC;
	inode_inc_link_count(inode);
	atomic_inc(&inode->i_count);
	return add_nondir(dentry,inode);
}

static int xpr_unlink(struct inode *dir,struct dentry *dentry){
	int err=-ENOENT;
	struct inode *inode=dentry->d_inode;
	struct page *page;
	struct xpr_dentry *de;
	de=xpr_find_entry(dentry,&page);
	if(!de)
		goto end_unlink;
	err=xpr_delete_entry(de,page);
	if(err)
		goto end_unlink;
	inode->i_ctime=dir->i_ctime;
	inode_dec_link_count(inode);
end_unlink:
	return err;
}

static struct dentry *xpr_lookup(struct inode *dir,struct dentry *dentry,struct nameidata *nd){
	struct inode *inode=NULL;
	ino_t ino=0;
	dentry->d_op=dir->i_sb->s_root->d_op;
	if(dentry->d_name.len>XPR_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
	struct page *page;
	
	struct xpr_dentry *de=xpr_find_entry(dentry,&page);
	if(de){
		ino=de->ino;
		xpr_put_page(page);	
	}
	if(ino){
		inode=xpr_iget(dir->i_sb,ino);
		if(IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry,inode);
	return NULL;
}

static int xpr_mknod(struct inode *dir,struct dentry *dentry,int mode,dev_t rdev){
	int error;
	struct inode *inode;
	if(!old_valid_dev(rdev))
		return -EINVAL;
	 
	inode=xpr_new_inode(dir,&error);
	if(inode){
		inode->i_mode=mode;
		xpr_set_inode(inode,rdev);
		
		mark_inode_dirty(inode);
	
		error=add_nondir(dentry,inode);
		
	}
	return error;
}

static int xpr_create(struct inode *dir,struct dentry *dentry,int mode,struct nameidata *nd){
	return xpr_mknod(dir,dentry,mode,0);
}

static int xpr_mkdir(struct inode *dir,struct dentry *dentry,int mode){
	struct inode *inode;
	printk("xpr_mkdir in 1\n");
	struct xpr_super_block *xpr_sb=XPR_SB(dir->i_sb)->s_xs;
	printk("xpr_mkdir in 2\n");
	int err=-EMLINK;
	if(dir->i_nlink>=(xpr_sb->s_link_max))
		goto out;
	printk("xpr_mkdir in 3\n");
	inode_inc_link_count(dir);
	inode=xpr_new_inode(dir,&err);
	if(!inode)
		goto out_dir;
	inode->i_mode=S_IFDIR|mode;
	if(dir->i_mode&S_ISGID)
		inode->i_mode|=S_ISGID;
	//设置该inode的操作
	xpr_set_inode(inode,0);
	inode_inc_link_count(inode);
	
	err=xpr_make_empty(inode,dir);
	if(err)
		goto out_fail;
	err=xpr_add_link(dentry,inode);
	if(err)
		goto out_fail;
	d_instantiate(dentry,inode);
out:	
	return err;
out_fail:
	//需要减少两次，因为在alloc_inode中已经设置过一次inode->i_nlink=1
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	iput(inode);
out_dir:
	inode_dec_link_count(dir);
	goto out;
}

static int xpr_rmdir(struct inode *dir,struct dentry *dentry){
	struct inode *inode=dentry->d_inode;
	int err=-ENOTEMPTY;
	if(xpr_empty_dir(inode)){
		err=xpr_unlink(dir,dentry);
		if(err){
			inode_dec_link_count(dir);
			inode_dec_link_count(dir);
		}
	}
	return err;
}

static int xpr_rename(struct inode * old_dir, struct dentry *old_dentry,
struct inode * new_dir, struct dentry *new_dentry)
{
	struct xpr_super_block * xpr_sb = XPR_SB(old_dir->i_sb)->s_xs;
	struct inode * old_inode = old_dentry->d_inode;
	struct inode * new_inode = new_dentry->d_inode;
	struct page * dir_page = NULL;
	struct xpr_dentry * dir_de = NULL;
	struct page * old_page;
	struct xpr_dentry * old_de;
	int err = -ENOENT;

	old_de = xpr_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = xpr_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page * new_page;
		struct xpr_dentry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !xpr_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = xpr_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		inode_inc_link_count(old_inode);
		xpr_set_link(new_de, new_page, old_inode);
		new_inode->i_ctime = CURRENT_TIME_SEC;
		if (dir_de)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else {
		if (dir_de) {
			err = -EMLINK;
			if (new_dir->i_nlink >= xpr_sb->s_link_max)
				goto out_dir;
		}
		inode_inc_link_count(old_inode);
		err = xpr_add_link(new_dentry, old_inode);
		if (err) {
			inode_dec_link_count(old_inode);
			goto out_dir;
		}
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	xpr_delete_entry(old_de, old_page);
	inode_dec_link_count(old_inode);

	if (dir_de) {
		xpr_set_link(dir_de, dir_page, new_dir);
		inode_dec_link_count(old_dir);
	}
	return 0;

out_dir:
	if (dir_de) {
		kunmap(dir_page);
		page_cache_release(dir_page);
	}
out_old:
	kunmap(old_page);
	page_cache_release(old_page);
out:
	return err;
}

static int xpr_symlink(struct inode *dir,struct dentry *dentry,const char* symname){
	int err= -ENAMETOOLONG;
	int lon=strlen(symname)+1;
	struct inode *inode;

	if(lon>dir->i_sb->s_blocksize)
		goto out;
	inode=xpr_new_inode(dir,&err);
	if(!inode)
		goto out;
	inode->i_mode=S_IFLNK|0777;
	xpr_set_inode(inode,0);
	err=page_symlink(inode,symname,lon);
	if(err)
		goto out_fail;
	err=add_nondir(dentry,inode);
out:
	return err;
out_fail:
	inode_dec_link_count(inode);
	iput(inode);
	goto out;
}

const struct inode_operations xpr_dir_inode_operations={
	.create		=xpr_create,
	.lookup		=xpr_lookup,
	.link		=xpr_link,
	.unlink		=xpr_unlink,
	.symlink	=xpr_symlink,
	.mkdir		=xpr_mkdir,
	.rmdir		=xpr_rmdir,
	.mknod		=xpr_mknod,
	.rename		=xpr_rename,
};
