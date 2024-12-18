/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1
#include "blk.h"

char	*rd_start;
int	rd_length = 0;

void do_rd_request(void)
{
	int	len;
	char	*addr;

	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
	end_request(1);
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;
	rd_length = length;
	cp = rd_start;
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);
}

// 虚拟盘：分配内存时，即把虚拟盘分配出来了, 虚拟盘的末端即主内存的起始端
// 从软盘中读取
/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */
void rd_load(void)
{
	struct buffer_head *bh;
	struct super_block	s;
	// 根文件系统映像文件在 boot 盘第 256 磁盘块开始处
	int		block = 256;	/* Start at block 256 */
	int		i = 1;
	int		nblocks;
	char		*cp;		/* Move pointer */
	
	if (!rd_length)			// 没有虚拟盘
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)	// 如果不是软盘, 那么就返回
		return;

	// 读软盘块 256+1, 256, 256+2
	// block+1 指超级块
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	// 将 s 指向缓冲区中的磁盘超级块
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	if (s.s_magic != SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */
		return;

	// 如果数据块数大于内存中虚拟盘所能容纳的块数，则不能加载
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;

	// 将磁盘上的根文件系统映像文件复制到虚拟盘上
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);
		brelse(bh);
		printk("\010\010\010\010\010%4dk",i);
		cp += BLOCK_SIZE;
		block++;
		nblocks--;
		i++;
	}
	printk("\010\010\010\010\010done \n");
	ROOT_DEV=0x0101;	// 修改 root_dev, 使其指向虚拟盘 ramdisk
}
