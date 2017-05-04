#ifndef _XPR_FS_H
#define _XPR_FS_H

#include <linux/types.h>


#define XPR_ROOT_INO	1

#define XPR_MAGIC 0x20130001


#define XPR_BLOCK_SIZE	1024

#define XPR_BLOCKS(s)		(XPR_SB(s)->s_xs->s_blk_cnt)


#define XPR_INODE_SIZE	sizeof(struct xpr_inode)
#define XPR_INODE_COUNT(s)		(XPR_SB(s)->s_xs->s_ino_cnt)
#define XPR_INODE_BLOCK_COUNT(s)		((XPR_INODE_COUNT(s)*XPR_INODE_SIZE+XPR_BLOCK_SIZE-1)/XPR_BLOCK_SIZE)
#define XPR_INODES_PER_BLOCK	((XPR_BLOCK_SIZE)/(sizeof(struct xpr_inode)))


#define XPR_BLOCK_RESERVED	10
#define XPR_NAME_LEN	60

//XPR文件系统的INODE结构
struct xpr_inode {
	__le16	i_mode;        //节点权限设置
	__le16	i_uid;         //拥有节点的用户ID
	__le16	i_gid;         //拥有节点的用户组ID
	__le32	i_size;        //
	__le32	i_atime;       //
	__le32	i_ctime;       //
	__le32	i_mtime;       //
	__le32	i_dtime;       //
	__le16	i_nlinks;      // 
	__le32	i_flags;       //
	__le32	i_blk_start; //索引节点开始块
	__le32	i_blk_end;   //索引节点结束块
	__le32	i_blocks;      //索引节点所占的块数
	__le16	i_dev;         //所在的设备名
	__le32	i_reserved;    //为该索引节点预留的块数
};

//XPR文件系统的超级块结构
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

//将超级块信息结构
struct  xpr_sb_info{
	struct xpr_super_block * s_xs;
	struct buffer_head * s_sbh;
};

typedef unsigned long xprblk_t;

//目录结构
struct xpr_dentry {
	__le32 ino;
	char name[XPR_NAME_LEN];
};

#endif 
