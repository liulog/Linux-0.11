/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h> */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
int sys_access(const char * filename,int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (current->gid == inode->i_gid)
		res >>= 6;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	iput(current->root);
	current->root = inode;
	return (0);
}

int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}

//
// 本质上：建立 filp[20] -> file_table[64] -> inode_table[32] -> inode -> 文件 的一条链路
// filp 指针数组, file_table 记录文件相关信息
// open 返回的 fd 即是 filp 的索引
//
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	// umask [本人、同组、其他], 每个三位, (RWX)
	// 0644 -> 本人可读可写、同组与其他可读
	// 0022 -> 同组和其他的可写位
	// mode 也就是去除 同组、其他 的可写权限
	mode &= 0777 & ~current->umask;		// INIT_TASK.current->umask 初始 0022
	
	// 遍历 filp[20], NP_OPEN=20, 找一个空闲
	for(fd=0 ; fd<NR_OPEN ; fd++) 		// fd -> filp 空闲位
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);	// 将该文件句柄执行时关闭标志设置为 0, 防止加载 exec 时关闭文件, 不让 fd 传递给新的可执行文件

	f=0+file_table;
	// 遍历 file_table[64], NR_FILE=64
	for (i=0 ; i<NR_FILE ; i++,f++)		// 找 file_table 中引用计数为 0 的空位
		if (!f->f_count) break;
	if (i>=NR_FILE)
		return -EINVAL;
	// filp[fd] 是一个指针, 指向 file_table[64] 中的一个 file
	(current->filp[fd]=f)->f_count++; 	// [重要] 1. filp 和 file_table 建立了联系

	// 打开 inode, inode 表示 inode_table[32] 中的一个节点
	// 其中 mode 经过修改, 返回后 inode 指向得到最终文件的 i 节点
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		current->filp[fd]=NULL;	// inode_table[32] 中没找到, 那么直接返回
		f->f_count=0;	// 引用计数 = 0
		return i;
	}

/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	if (S_ISCHR(inode->i_mode)) {
		if (MAJOR(inode->i_zone[0])==4) {
			if (current->leader && current->tty<0) {
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
		} else if (MAJOR(inode->i_zone[0])==5)
			if (current->tty<0) {
				iput(inode);
				current->filp[fd]=NULL;
				f->f_count=0;
				return -EPERM;
			}
	}
/* Likewise with block-devices: check for floppy_change */
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);

	f->f_mode = inode->i_mode;	// 
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;		// [重要] 2. 建立 file_table[64] 与 inode_table[32] 的联系
	f->f_pos = 0;
	return (fd);			// flip 中的 index【句柄】
}

int sys_creat(const char * pathname, int mode)
{
	// 最终调用的是 sys_open, FLAG->O_CREATE | O_TRUNC
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL;	// current->filp[fd] 断开指向 file_table
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)		// file_table 中的这个文件 f_count--
		return (0);
	iput(filp->f_inode);
	return (0);
}
