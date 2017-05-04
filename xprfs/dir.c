#include "xpr.h"
#include <linux/buffer_head.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/swap.h>


typedef struct xpr_dentry xpr_dirent;

static inline void *xpr_next_entry(void *de){
	return (void *)((char *)de+sizeof(struct xpr_dentry));
}

static unsigned xpr_last_byte(struct inode *inode, unsigned long page_nr){
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}
static inline unsigned long xpr_dir_pages(struct inode *inode){
	return (inode->i_size+PAGE_CACHE_SIZE-1)>>PAGE_CACHE_SHIFT;
}

inline void xpr_put_page(struct page * page){
	kunmap(page);
	page_cache_release(page);
}

static struct page *xpr_get_page(struct inode *dir,unsigned long n){
	struct address_space *mapping=dir->i_mapping;
	struct page *page=read_mapping_page(mapping,n,NULL);
	if(!IS_ERR(page)){
		kmap(page);
		if(!PageUptodate(page))
			goto fail;
	}
	return page;
fail:
	xpr_put_page(page);
	return ERR_PTR(-EIO);
}

static int xpr_commit_chunk(struct page *page, loff_t pos, unsigned len){
         struct address_space *mapping = page->mapping;
         struct inode *dir = mapping->host;
         int err = 0;
         block_write_end(NULL, mapping, pos, len, len, page, NULL);
 
         if (pos+len > dir->i_size) {
                 i_size_write(dir, pos+len);
                mark_inode_dirty(dir);
         }
         if (IS_DIRSYNC(dir))
                 err = write_one_page(page, 1);
         else
                 unlock_page(page);
         return err;
 }



static int xpr_readdir(struct file *filp,void *dirent,filldir_t filldir){
	loff_t pos=filp->f_pos;
	
	struct inode *inode=filp->f_path.dentry->d_inode;
	
	unsigned int offset=pos & ~PAGE_CACHE_MASK;
	unsigned long n=pos >> PAGE_CACHE_SHIFT;
	unsigned long npages=xpr_dir_pages(inode);
	unsigned chunk_size=sizeof(struct xpr_dentry);
	lock_kernel();
	pos=(pos+chunk_size-1)&~(chunk_size-1);
	if(pos>=inode->i_size)
		goto done;	
	for(;n<npages;n++,offset=0){
		char *kaddr,*limit;
		char *p;
	
		struct page *page=xpr_get_page(inode,n);
	
		if(IS_ERR(page)){
			continue;
			printk("Page Error\n");
		}
	
		kaddr=(char *)page_address(page);
	
		p=(kaddr+offset);
	
		limit=kaddr+xpr_last_byte(inode,n)-chunk_size;
		for(;p<=limit;p=xpr_next_entry(p)){
			struct xpr_dentry * de=(struct xpr_dentry*)p;
		 	 
			if(de->ino){	
					
				offset=p -kaddr;
				unsigned name_len=strnlen(de->name,XPR_NAME_LEN);
				unsigned char d_type=DT_UNKNOWN;
				int over=filldir(dirent,de->name,name_len,(n<<PAGE_CACHE_SHIFT)|offset,le32_to_cpu(de->ino),d_type);
				if(over){
					xpr_put_page(page);
					goto done;
				}
			}			
		}
		xpr_put_page(page);
	}
done:
	filp->f_pos=(n<<PAGE_CACHE_SHIFT)|offset;
	unlock_kernel();
	return 0;	
}

static inline int namecompare(int len,int maxlen,const char *name,const char *buffer){

	if(len<maxlen&&buffer[len])
		return 0;
	return !memcmp(name,buffer,len);
}

/*
在目录中寻找dentry->d_name.name的目录项
是不是直接可以生成一个xpr_dentry呢？
不能，只能填充name
ino没有办法填充
*/
struct xpr_dentry * xpr_find_entry(struct dentry *dentry,struct page **res_page){
	const char *name=dentry->d_name.name;
	int namelen=dentry->d_name.len;
	struct inode *dir=dentry->d_parent->d_inode;
	
	unsigned long n;
	unsigned long npages=xpr_dir_pages(dir);
	struct page *page=NULL;
	char *p;

	char *namx;
	__u32	inumber;
	*res_page=NULL;
	
	for(n=0;n<npages;n++){
		char *kaddr,*limit;
		page=xpr_get_page(dir,n);	
		if(IS_ERR(page))
			continue;
		kaddr=(char *)page_address(page);
		limit=kaddr+xpr_last_byte(dir,n)-sizeof(struct xpr_dentry);
		for(p=kaddr;p<=limit;p=xpr_next_entry(p)){
			struct xpr_dentry *de=(struct xpr_dentry *)p;
			namx=de->name;
			inumber=de->ino;
			if(!inumber)
				continue;
			if(namecompare(namelen,XPR_NAME_LEN,name,namx))
				goto found;
		}
		xpr_put_page(page);
	}
	return NULL;
found:
	*res_page=page;
	return (struct xpr_dentry *)p;
} 
/*
将目录项写到父目录中去
*/
int xpr_add_link(struct dentry *dentry,struct inode *inode){
	struct inode *dir=dentry->d_parent->d_inode;
	const char *name=dentry->d_name.name;
	int namelen=dentry->d_name.len;

	struct page *page=NULL;
	unsigned long npages=xpr_dir_pages(dir);
	unsigned long n;
	char *kaddr,*p;
	int ino;
	struct xpr_dentry *de;
	loff_t pos;
	int err;
	char *namx=NULL;
	__u32 inumber;

	for(n=0;n<=npages;n++){
		char *limit,*dir_end;
		page=xpr_get_page(dir,n);
		err=PTR_ERR(page);
		if(IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr=(char *)page_address(page);
		dir_end=kaddr+xpr_last_byte(dir,n);
		limit=kaddr+PAGE_CACHE_SIZE-sizeof(struct xpr_dentry);
		for(p=kaddr;p<=limit;p=xpr_next_entry(p)){
			de=(struct xpr_dentry *)p;
			namx=de->name;
			inumber=de->ino;
			if(p==dir_end){
				goto got_it;
			}
			if(!inumber)
				goto got_it;

			err=-EEXIST;
			if(namecompare(namelen,XPR_NAME_LEN,name,namx))
				goto out_unlock;
			ino=de->ino;
		}
		unlock_page(page);
		xpr_put_page(page);
	}
	BUG();
	return -EINVAL;
got_it:
	pos=(page->index>>PAGE_CACHE_SHIFT)+p-(char *)page_address(page);
	err=__xpr_write_begin(NULL,page->mapping,pos,sizeof(struct xpr_dentry),AOP_FLAG_UNINTERRUPTIBLE,&page,NULL);
	if(err)
		goto out_unlock;
	memcpy(namx,name,namelen);
	//将名字数组多余的空间清空
	memset(namx+namelen,0,XPR_NAME_LEN-namelen-4);
	de->ino=inode->i_ino;
	err=xpr_commit_chunk(page,pos,sizeof(struct xpr_dentry));
	dir->i_mtime=dir->i_ctime=CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
out_put:
	xpr_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

int xpr_delete_entry(struct xpr_dentry *de,struct page *page){
	struct address_space *mapping=page->mapping;
	struct inode *inode=(struct inode*)mapping->host;
	//页的虚拟地址
	char *kaddr=page_address(page);
	//得到在inode中的偏移
	loff_t pos=page_offset(page)+(char *)de -kaddr;
	unsigned len=sizeof(struct xpr_dentry);
	int err;
	
	lock_page(page);
	err=__xpr_write_begin(NULL,mapping,pos,len,AOP_FLAG_UNINTERRUPTIBLE,&page,NULL);
	if(err==0){
		de->ino=0;
		err=xpr_commit_chunk(page,pos,len);
	}else
		unlock_page(page);
	xpr_put_page(page);
	inode->i_ctime=inode->i_mtime=CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	return err;
}

/*
检查目录是否为空
*/
int xpr_empty_dir(struct inode * inode){
         struct page *page = NULL;
         unsigned long i, npages = xpr_dir_pages(inode);
         
         char *name;
        __u32 inumber;
 
         for (i = 0; i < npages; i++) {
                 char *p, *kaddr, *limit;
 
                 page = xpr_get_page(inode, i);
                 if (IS_ERR(page))
                         continue;

                kaddr = (char *)page_address(page);
                 limit = kaddr + xpr_last_byte(inode, i) - sizeof(struct xpr_dentry);
                 for (p = kaddr; p <= limit; p = xpr_next_entry(p)) {
                       
                        xpr_dirent *de = (xpr_dirent *)p;
                         name = de->name;
                         inumber = de->ino;
                
 
                        if (inumber != 0) {
                                /* check for . and .. */
                                 if (name[0] != '.')
                                         goto not_empty;
                                 if (!name[1]) {
                                         if (inumber != inode->i_ino)
                                                 goto not_empty;
                                 } else if (name[1] != '.')
                                         goto not_empty;
                                 else if (name[2])
                                         goto not_empty;
                         }
                 }
                 xpr_put_page(page);
         }
        return 1;
 
 not_empty:
        xpr_put_page(page);
         return 0;
}

int xpr_make_empty(struct inode *inode, struct inode *dir)
{
        struct address_space *mapping = inode->i_mapping;
         struct page *page = grab_cache_page(mapping, 0);
        
         char *kaddr;
         int err;
 
        if (!page)
                 return -ENOMEM;
         err = __xpr_write_begin(NULL, mapping, 0, 2 * sizeof(struct xpr_dentry),AOP_FLAG_UNINTERRUPTIBLE, &page,NULL);
        if (err) {
                unlock_page(page);
                goto fail;
         }
 
        kaddr = kmap_atomic(page, KM_USER0);
        memset(kaddr, 0, PAGE_CACHE_SIZE);


        struct xpr_dentry *de = (struct xpr_dentry *)kaddr; 
        de->ino = inode->i_ino;
        strcpy(de->name, ".");
        de = xpr_next_entry(de);
        de->ino = dir->i_ino;
        strcpy(de->name, "..");
        
         kunmap_atomic(kaddr, KM_USER0);

        err = xpr_commit_chunk(page, 0, 2 * sizeof(struct xpr_dentry));
 fail:
         page_cache_release(page);
         return err;
}

void xpr_set_link(struct xpr_dentry *de, struct page *page, struct inode *inode){
         struct address_space *mapping = page->mapping;
         struct inode *dir = mapping->host;
       
	//pos表示de在inode中的偏移
         loff_t pos = page_offset(page) +(char *)de-(char*)page_address(page);
         int err;
 
         lock_page(page);
 
         err = __xpr_write_begin(NULL, mapping, pos,sizeof(struct xpr_dentry),AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
         if (err == 0) {
                 de->ino = inode->i_ino;
                 err = xpr_commit_chunk(page, pos, sizeof(struct xpr_dentry));
         } else {
                 unlock_page(page);
         }
         xpr_put_page(page);
         dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
         mark_inode_dirty(dir);
}

struct xpr_dentry * xpr_dotdot (struct inode *dir, struct page **p){
		struct page *page = xpr_get_page(dir, 0);
		//struct xpr_super_block *xpr_sb = XPR_SB(dir->i_sb);
		struct xpr_dentry *de = NULL;
		
		if (!IS_ERR(page)) {
		//".."在"."之后，所以要xpr_next_entry;
			de = xpr_next_entry(page_address(page));
			*p = page;
		}
		return de;
}

const struct file_operations xpr_dir_operations ={
	.llseek		=generic_file_llseek,
	.read		=generic_read_dir,
	.readdir	=xpr_readdir,
	.fsync		=xpr_sync_file,
};
