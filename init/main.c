/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
// __attribute__((always_inline)) gcc 扩展，告诉编译器强制内联该函数, 不应该忽视
static inline fork(void) __attribute__((always_inline));
static inline pause(void) __attribute__((always_inline));
// static 表示该函数仅在当前文件内可见, 而不能被其他文件引用
// inline 提示编译器尽可能将该函数的代码嵌入到调用该函数的地方
static inline _syscall0(int,fork)	// 包装系统调用 fork, 表示这是一个不带参数的系统调用, 返回类型为 int
static inline _syscall0(int,pause)	// 包装系统调用 pause, 表示这是一个不带参数的系统调用, 返回类型为 int
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)

// 指定的根文件系统所在的设备
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)	// bootsec 中定义的 root_dev


/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

 	ROOT_DEV = ORIG_ROOT_DEV;			// ROOT_DEV
 	drive_info = DRIVE_INFO;			// 拷贝硬盘信息
	// 1MB + EXT_MEM, 具体的内容
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	// memory
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	// buffer_memory
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;

	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end);
	trap_init();
	blk_dev_init();	// 初始化 request[32] 所有 request.dev=1, request.next=NULL
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();	// 设置进程 0 的 TSS、LDT, 并设置了 int80 系统调用 以及时钟中断
	buffer_init(buffer_memory_end);	// 构造 buffer_head 双向链表
	hd_init();		// 挂载 do_hd_request 到 blk_dev[3].request_fn, 并配置中断
	floppy_init();	// 挂载 do_fd_request 到 blk_dev[2].request_fn, 并配置中断
	sti();					// 都初始化完成之后, 才开启中断
	move_to_user_mode();	// move_to_user_mode
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };


// 物理设备
// 
// 逻辑设备（干活的，文件系统的装载单位，一个逻辑设备只有一个根i节点）
// mount 挂载文件系统
// 
// 根文件系统 - 根设备 ROOT_DEV
// 	先指定根设备(逻辑的), 装载根文件系统, 再 monut 其他
// 
// super_block 超级块, 这里是 8个
// 	
// 物理设备 [引导块 + 超级块... 超级块...], 分区就是分出来多个逻辑
//	分区信息即是存储在引导块, 指示了各超级块的位置
//  引导块 - 一个物理盘一个，超级块 - 一个逻辑盘一个
//	引导块，物理盘都有（包括数据盘、系统盘）
//	引导块的第一个扇区 -> 引导扇区, 0xAA55 表示分区表结束标志
// 
// 引导块 [ 超级块, i节点位图, 逻辑块位图, i节点， 数据块 ]
//								｜--------------｜
//									i节点和数据块通过 i节点的i_zone[9] 链接 
//
// 块是 OS 的概念
// 设备号、块号（跟逻辑块位图有关） - 逻辑
//
// inode_table - 32项, m_inode 类型, 打开文件, 就要挂一个 i 节点, 不记录重复的
// file_table  - 64项, file 类型, 允许重复, 整个系统打开的文件
// task_struct 中 filp[20], 类型也是 file, 一个进程最多打开 20 个 file, fopen 返回的句柄
//
// inode_table 和 file_table 
// 缓冲块大约 3k 个
// 用 缓冲区 驻留数据

void init(void)
{
	int pid,i;

	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
