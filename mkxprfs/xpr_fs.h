#include <linux/fs.h>
#include <linux/types.h>


#define XPR_ROOT_INO	1

#define XPR_MAGIC	0x20130001

#define XPR_LINK_MAX	200	

#define BLOCK_SIZE_BITS		10
#define BLOCK_SIZE	(1<<BLOCK_SIZE_BITS)
#define XPR_BLOCK_SIZE	1024

#define XPR_INODE_SIZE		(sizeof(struct xpr_inode))
#define XPR_INODE_COUNT		(XPR_BLOCK_SIZE/XPR_INODE_SIZE)
#define XPR_INODE_BLOCK_COUNT		((XPR_INODE_COUNT*XPR_INODE_SIZE+XPR_BLOCK_SIZE-1)/XPR_BLOCK_SIZE)
#define XPR_INODES_PER_BLOCK	((XPR_BLOCK_SIZE)/XPR_INODE_SIZE)

#define XPR_BLOCK_RESERVED	10
#define DIRSIZE		(sizeof(struct xpr_dir_entry))

struct xpr_inode {
	__le16	i_mode;
	__le16	i_uid;
	__le16	i_gid;
	__le32	i_size;
	__le32	i_atime;
	__le32	i_ctime;
	__le32	i_mtime;
	__le32	i_dtime;
	__le16	i_nlinks;
	__le32	i_flags;
	__le32	i_blk_start;
	__le32	i_blk_end;
	__le32	i_blocks;
	__le16	i_dev;
	__le32	i_reserved;
	__u8	fitblk[5];
};

struct xpr_super_block {
	__le32 s_ino_cnt;
	__le16 s_ino_size;
	__le32 s_blk_cnt;
	__le32 s_fblk_cnt;
	__le32 s_fino_cnt;
	__le32 s_fdblk;
	__le32 s_fino;
	__le32 s_link_max;
	__le32 s_log_bsize;
	__le32 s_mtime;
	__le32 s_wtime;
	__le32 s_magic;
};

typedef unsigned long xpr_fsblk_t;


#define XPR_NAME_LEN	60	
struct xpr_dir_entry {
	__le32 ino;
	char name[XPR_NAME_LEN];
};
