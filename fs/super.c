/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res ; \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

// 内存上有 8 个超级块，最多打开 8 个文件系统
// 超级块和根 i 节点有关
struct super_block super_block[NR_SUPER];			// 内存中的 super_block[8]
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

// 看 super_block 数组中有没有现成的 super_block
struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	// 遍历 super_block[8]
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {	// 找现成的
			wait_on_super(s);	
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;
		} else
			s++;
	return NULL;
}

void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

// 读指定设备的 super_block
// 同时也把 imap 和 zmap 都读取到缓冲区了
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;
	check_disk_change(dev);
	if ((s = get_super(dev)))   // 看有没有现成的 super_block, 有现成的直接用, 返回 s
		return s;

	// 找空闲的 super_block
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}

	// 初始化
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	lock_super(s);

	// 读取 dev 的第一个块
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	// d_super_block （disk中的）的内容拷贝到 super_block (内存中的)
	// 也很重要
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	
	brelse(bh); // 释放缓冲块, 也就是引用计数 -1
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	// 初始化 s_imap, s_zmap
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;

	// 读取 ‘inode位图’ 和 ‘逻辑块位图’ 到缓冲区里, 各 8 个块
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if ((s->s_imap[i]=bread(dev,block)))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if ((s->s_zmap[i]=bread(dev,block)))
			block++;
		else
			break;

	// 错误检测, 检测个数相等
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	// 成功 read_super
	// 这里的 1： i 节点位图的 0 号不能使用, 防止和找不到返回 0 号冲突
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

// 每一个 inode 都有一个 izone[9], 存储的时块号, 用来组织这个文件
// task_struct.filp->file_table->inode_table->找到设备上的inode, inode 的izone[9] 表示这个文件的块, 有序

// 内存中的 inode 都与 inode_table[32] 有关系

// mount 一般的文件系统
// 一个文件系统, 一个逻辑设备, 一个 super_block
// 设备名. 路径名
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	// 找 dev 对应 inode
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];	// 设备文件 inode 的 izone[0] 指的是设备号, 与普通文件的 inode 不同, 获取设备号
	if (!S_ISBLK(dev_i->i_mode)) {	// 判断是不是 inode
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	// 找 dir 对应的 inode
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	// 读取指定设备的超级块 super_block
	if (!(sb=read_super(dev))) {	// 有设备号, 则可以读取其超级块, 这里有 s_imount 信息
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {				// 检查是不是已经被挂过了
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}

	// (*)
	sb->s_imount=dir_i;		// super_block.i_mount 文件系统要挂在到的文件系统的 inode, 挂载上的关键一步
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))	// d_inode 是否标准
		panic("bad i-node size");
	for(i=0;i<NR_FILE;i++)				// 初始化 file_table
		file_table[i].f_count=0;		// file_table 中的引用计数清 0
	
	// 2 表示软盘, 根文件系统信息
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	
	// 初始化 super_block[8]
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;		// dev 初始化为 0
		p->s_lock = 0;
		p->s_wait = NULL;
	}
	// 物理内存: xx | xx ｜ 虚拟盘 | xx
	// 读指定设备的超级块 ROOT_DEV		---- 此时的 ROOT_DEV 根设备指的是 虚拟盘 ramdisk		super_block[8]
	if (!(p=read_super(ROOT_DEV)))			
		panic("Unable to mount root");

	// 读取指定的 inode, 根设备的 inode			inode_table[32]
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");

	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	
	// s_isup = s_imount = mi, 根i节点
	p->s_isup = p->s_imount = mi; 		/* (important!) 安装根文件系统的关键! 仅此一个 */
	
	// fork 会传递到 子进程, 能够顺着根 inode，找到所有文件
	current->pwd = mi;	/* 当前目录 */
	current->root = mi;	/* 根目录 */

	free=0;
	i=p->s_nzones;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);
	free=0;
	i=p->s_ninodes+1;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
