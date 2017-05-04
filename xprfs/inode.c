#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/random.h>
#include "xpr.h"

MODULE_AUTHOR("Jiang Jiaqiu");
MODULE_DESCRIPTION("Experimental(XPR) Filesystem, not study&test only!");
MODULE_LICENSE("GPL");


void xpr_free_inode(struct inode *inode){
	struct super_block *sb=inode->i_sb;
	struct xpr_super_block *xpr_sb=XPR_SB(inode->i_sb)->s_xs;
	struct buffer_head *bh;
	unsigned long ino;
	ino=inode->i_ino;
	struct xpr_inode *raw_inode;
	if(ino<1||ino>xpr_sb->s_ino_cnt){
		printk("xpr_free_inode: inode 0 or nonexistent inode\n");
		return;
	}
	raw_inode=xpr_raw_inode(sb,ino,&bh);
	if(raw_inode){
		raw_inode->i_nlinks=0;
		raw_inode->i_mode=0;
	}
	if(bh){
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	clear_inode(inode);
}

static const struct inode_operations xpr_symlink_inode_operations={
	.readlink	=generic_readlink,
	.follow_link	=page_follow_link_light,
	.put_link	=page_put_link,
};


void xpr_set_inode(struct inode *inode,dev_t rdev){
	if(S_ISREG(inode->i_mode)){
		inode->i_op=&xpr_file_inode_operations;
		inode->i_fop=&xpr_file_operations;
		inode->i_mapping->a_ops=&xpr_aops;
	}else if(S_ISDIR(inode->i_mode)){
		inode->i_op=&xpr_dir_inode_operations;
		inode->i_fop=&xpr_dir_operations;
		inode->i_mapping->a_ops=&xpr_aops;
	}else if(S_ISLNK(inode->i_mode)){
		inode->i_op=&xpr_symlink_inode_operations;
		inode->i_mapping->a_ops=&xpr_aops;
	}else
		init_special_inode(inode,inode->i_mode,rdev);
}

/*
只需要返回设置了ino的inode
*/
struct inode *xpr_new_inode(struct inode *dir,int *err){

	struct super_block *sb;
	struct buffer_head *bh;
	ino_t ino=0;
	int block;
	struct inode *inode;

	struct xpr_inode_info *xpr_ino_i;

	struct xpr_inode *raw_inode;
	char *p;
	
	sb=dir->i_sb;
	inode=new_inode(sb);
	if(!inode)
		return ERR_PTR(-ENOMEM);
	xpr_ino_i=XPR_INO_I(inode);
	
	
	inode->i_uid=current->cred->fsuid;
	inode->i_gid=(dir->i_mode&S_ISGID)?dir->i_gid:current->cred->fsgid;
	inode->i_mtime=inode->i_atime=inode->i_ctime=CURRENT_TIME_SEC;
	
	block=2;
	struct xpr_inode *prev=NULL;
	while(bh=sb_bread(sb,block)){
		p=bh->b_data;
		while(p<=(bh->b_data+XPR_BLOCK_SIZE-XPR_INODE_SIZE)){
			raw_inode=(struct xpr_inode *)p;
			ino++;
			if(!raw_inode->i_nlinks&&!raw_inode->i_blk_start){
				if(!prev->i_reserved)
					prev->i_reserved=XPR_BLOCK_RESERVED;
				prev->i_blocks=prev->i_blk_end-prev->i_blk_start+1;
			
				mark_buffer_dirty(bh);
				goto find;
			}
			p+=XPR_INODE_SIZE;
			
			prev=raw_inode;
		}
		brelse(bh);
		if(block>XPR_INODE_BLOCK_COUNT(sb))
			break;
		block++;	
	}
	
	iput(inode);
	brelse(bh);
	*err=-ENOSPC;
	return NULL;
find:
/*内存中的inode->i_ino可能有相同的，但因为inode所属设备不同，所以没有影响
inode->i_ino就是文件在i节点块中的节点号
*/
	inode->i_ino=ino;
	
	raw_inode->i_blk_start=prev->i_blk_end+prev->i_reserved+1;
	xpr_ino_i->i_reserved=raw_inode->i_reserved;
	xpr_ino_i->i_blk_start=xpr_ino_i->i_blk_end=raw_inode->i_blk_start;
	raw_inode->i_blk_end=raw_inode->i_blk_start;
	brelse(bh);
	insert_inode_hash(inode);	
	mark_inode_dirty(inode);
	*err=0;
	return inode;
}

unsigned long xpr_count_free_inodes(struct super_block *sb){
	
	struct buffer_head *bh;
	struct xpr_inode *xpr;
	char *p;
	 
	unsigned long block=2; 
	unsigned long count=0;
	
	while(bh=sb_bread(sb,block)){
		p=bh->b_data;
		while(p<=(bh->b_data+XPR_BLOCK_SIZE-XPR_INODE_SIZE)){
			xpr=(struct xpr_inode *)p;
			if(xpr->i_nlinks)
				count++;
			p+=XPR_INODE_SIZE;
		}
		brelse(bh);
		if(block>XPR_INODE_BLOCK_COUNT(sb))
			break;
		block++;	
	}
	
	return XPR_SB(sb)->s_xs->s_ino_cnt-count;
}

unsigned long xpr_count_free_blocks(struct super_block *sb){

	struct xpr_super_block *xpr_sb;
	char *p;
	int block=2;
	xpr_sb=XPR_SB(sb)->s_xs;
	unsigned long used=0;
	struct buffer_head *bh;
	
	struct xpr_inode * xpr;
	while(bh=sb_bread(sb,block)){
		p=bh->b_data;
		while(p<=(bh->b_data+XPR_BLOCK_SIZE-XPR_INODE_SIZE)){
			xpr=(struct xpr_inode *)p;
			if(!xpr->i_blocks)
				used=xpr->i_blk_end;
				
		}
		brelse(bh);
	}
	return XPR_BLOCKS(sb)-used;
}


/*
删除,在ialloc.c中实现
从磁盘读取xpr_inode,返回xpr_inode
*/

struct xpr_inode * xpr_raw_inode(struct super_block *sb,ino_t ino,struct buffer_head **p){
	int block=0;
	struct buffer_head *bh;
	struct xpr_sb_info *sbi=XPR_SB(sb);
	struct xpr_inode *xpr=NULL;
	if(!ino||ino>sbi->s_xs->s_ino_cnt){
		printk("Bad inode number on dev %s: %ld is out of range\n",sb->s_id,(long)ino);
		return NULL;
	}
	ino--;

	block=(2 + ino/XPR_INODES_PER_BLOCK);

	if(!(bh=sb_bread(sb,block))){
		printk("Unable to read inode block\n");
		return NULL;
	}
	xpr=(struct xpr_inode *)((char *)(bh->b_data));
	*p=bh;
	return (struct xpr_inode *)((char *)(bh->b_data+ino*sizeof(struct xpr_dentry)));
}


/*
从磁盘上读取xpr_inode，填充inode结构
*/
struct inode *xpr_iget(struct super_block *sb,unsigned long ino){

	struct xpr_inode_info *xpr_ino_i;
	struct buffer_head *bh;
	struct xpr_inode *raw_inode;
	
	long ret=-EIO;
	 

	struct inode *inode=iget_locked(sb,ino);

	if(!inode)
		return ERR_PTR(-ENOMEM);
	if(!(inode->i_state &I_NEW)){
		
		return inode;
	}
	xpr_ino_i=XPR_INO_I(inode);

	raw_inode=xpr_raw_inode(inode->i_sb,ino,&bh);
	if(!raw_inode){
		iput(inode);
		return NULL;
	}
	
	inode->i_mode=raw_inode->i_mode;
	inode->i_uid=(uid_t)raw_inode->i_uid;
	inode->i_gid=(gid_t)raw_inode->i_gid;
	inode->i_size=raw_inode->i_size;
	inode->i_nlink=raw_inode->i_nlinks;
	inode->i_rdev=raw_inode->i_dev;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = raw_inode->i_ctime;
        inode->i_mtime.tv_nsec = 0;
        inode->i_atime.tv_nsec = 0;
        inode->i_ctime.tv_nsec = 0;

	xpr_ino_i->i_dtime=raw_inode->i_dtime;

	if(inode->i_nlink==0 && (inode->i_mode==0||xpr_ino_i->i_dtime)){
		
		brelse(bh);
		ret=-ESTALE;
		goto bad_inode;
	}

	inode->i_blocks=raw_inode->i_blocks;
	xpr_ino_i->i_dtime=0;
	xpr_ino_i->i_blk_start=raw_inode->i_blk_start;
	xpr_ino_i->i_blk_end=raw_inode->i_blk_end;
	xpr_ino_i->i_blocks=raw_inode->i_blocks;
	xpr_ino_i->i_reserved=raw_inode->i_reserved;

	//dev_t rdev=0所以不支持设备，mknod也就无用了
	//修改xpr_inode,添加i_dev;现在支持设备
	xpr_set_inode(inode,inode->i_rdev);
	brelse(bh);
	unlock_new_inode(inode);
	
	return inode;
bad_inode:
	iget_failed(inode);
	return ERR_PTR(ret);
}
/*
同步磁盘上的xpr_inode信息，用内存中的inode填充buffer_head，然后标识为脏
*/
struct buffer_head *xpr_update_inode(struct inode *inode){
		
	struct xpr_inode_info *xpr_ino_i=XPR_INO_I(inode);
	struct super_block *sb=inode->i_sb;
	ino_t ino=inode->i_ino;
	uid_t uid=inode->i_uid;
	gid_t gid=inode->i_gid;
	struct buffer_head *bh;
	struct xpr_inode *raw_inode =xpr_raw_inode(sb,ino,&bh);

	if(!raw_inode)
		return NULL;
	
	raw_inode->i_mode=inode->i_mode;
	raw_inode->i_uid=uid;
	raw_inode->i_gid=gid;
	raw_inode->i_nlinks=inode->i_nlink;
	raw_inode->i_size=inode->i_size;
	raw_inode->i_atime=inode->i_atime.tv_sec;
	raw_inode->i_mtime=inode->i_mtime.tv_sec;
	raw_inode->i_ctime=inode->i_ctime.tv_sec;
	//raw_inode->i_dtime=inode->i_dtime.tv_sec;
	
	if(S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_dev=old_encode_dev(inode->i_rdev);
	else{
		raw_inode->i_blk_start=xpr_ino_i->i_blk_start;
		raw_inode->i_blk_end=xpr_ino_i->i_blk_end;
		raw_inode->i_blocks=xpr_ino_i->i_blocks;
		raw_inode->i_reserved=xpr_ino_i->i_reserved;
			
	}
	mark_buffer_dirty(bh);
	return bh;
}

void xpr_truncate(struct inode *inode){
	
	if(!(S_ISREG(inode->i_mode)||S_ISDIR(inode->i_mode)||S_ISLNK(inode->i_mode)))
		return;
	
	struct xpr_inode_info *xpr_ino_i=XPR_INO_I(inode);
	block_truncate_page(inode->i_mapping,inode->i_size,xpr_get_block);

	xpr_ino_i->i_reserved+=xpr_ino_i->i_blk_end-xpr_ino_i->i_blk_start+1;
	xpr_ino_i->i_blk_end=xpr_ino_i->i_blk_start;
	//xpr_ino_i->i_blocks=-1;
	/*
	块数等于0，说明文件在最后
	块数不等于0，说明文件已经封顶
	块数等于－1，说明文件已经被清空
	开始块和结束块＝0，说明该处空
	*/
	inode->i_mtime=inode->i_ctime=CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	
}

int xpr_sync_inode(struct inode *inode){
	int ret=0;
	struct buffer_head *bh;
	bh=xpr_update_inode(inode);
	if(bh && buffer_dirty(bh)){
		sync_dirty_buffer(bh);
		if(buffer_req(bh)&&!buffer_uptodate(bh)){
			printk("IO error syncing xpr inode\n");
			ret=-1;
		}
	}else if(!bh)
		ret=-1;
	brelse(bh);
	return ret;
}


int xpr_get_block(struct inode *inode,sector_t iblock,struct buffer_head *bh,int create){

	int err=-EIO;
	 
	struct xpr_inode_info *xpr_ino_i=XPR_INO_I(inode);
	
	if(iblock>(xpr_ino_i->i_blocks+xpr_ino_i->i_reserved)){
		printk("XPR-fs:function xpr_get_blocks block error");
		return err;
	}
	
	xprblk_t block=xpr_ino_i->i_blk_start+iblock;
	if(block<=xpr_ino_i->i_blk_end){
		map_bh(bh,inode->i_sb,le32_to_cpu(block));
		return 0;
	}else if(!create){		
		brelse(bh);
		return err;
	}else{
		set_buffer_new(bh);
		if(xpr_ino_i->i_reserved&&xpr_ino_i->i_blocks){//如果已经封顶
			xpr_ino_i->i_reserved=xpr_ino_i->i_blocks+xpr_ino_i->i_reserved-iblock;
			xpr_ino_i->i_blk_end=xpr_ino_i->i_blk_start+iblock-1;
			xpr_ino_i->i_blocks=iblock;
		}else //reserved!=0&&xpr_ino_i->i_blocks==0的情况不会出现,因为i_blocks至少为一
			xpr_ino_i->i_blk_end=xpr_ino_i->i_blk_start+iblock-1;
		
		map_bh(bh,inode->i_sb,le32_to_cpu(block));		
		mark_buffer_dirty_inode(bh,inode);
	}
	printk("xpr_get_block %d\n",block);
	return 0;
}

static int xpr_writepage(struct page *page,struct writeback_control *wbc){

	return block_write_full_page(page,xpr_get_block,wbc);
}

static int xpr_readpage(struct file *file,struct page *page){
	return block_read_full_page(page,xpr_get_block);
}

int __xpr_write_begin(struct file *file,struct address_space *mapping,loff_t pos,unsigned len,unsigned flags,struct page *pagep,void **fsdata){
	return block_write_begin(file,mapping,pos,len,flags,pagep,fsdata,xpr_get_block);

}

static int xpr_write_begin(struct file *file,struct address_space *mapping,
loff_t pos,unsigned len,unsigned flags,struct page **pagep,void **fsdata){
	*pagep=NULL;
	return __xpr_write_begin(file,mapping,pos,len,flags,pagep,fsdata);
}

static sector_t xpr_bmap(struct address_space *mapping,sector_t block){
	return generic_block_bmap(mapping,block,xpr_get_block);
}

const struct address_space_operations xpr_aops ={

	.readpage		=xpr_readpage,
	.writepage		=xpr_writepage,
	.sync_page		=block_sync_page,
	.write_begin		=xpr_write_begin,
	.write_end		=generic_write_end,
	.bmap			=xpr_bmap,
};

