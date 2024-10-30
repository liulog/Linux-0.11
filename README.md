Linux-0.11
==========

为了更加优雅的调试 Linux-0.11, 进行了一点点改动, 灵感来自蒋岩炎的OS课程

添加了 init.py 脚本文件, 其中包含了一些 gdb 命令, 添加了 make monitor, 用于启动 gdb-multiarch

仅针对 Linux 进行了测试

## 如何使用

```bash
$ make help		// get help
$ make  		// compile
$ make start		// boot it on qemu
```

```bash
$ make debug		// debug it via qemu & gdb, you'd start gdb to connect it.
$ make monitor      // new a terminal and start gdb-multiarch
```