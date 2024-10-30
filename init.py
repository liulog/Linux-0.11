import gdb
import re

R = {}

def stop_handler(event):
    if isinstance(event, gdb.StopEvent):
        regs = [
            line for line in 
                gdb.execute('info registers',
                            to_string=True).
                            strip().split('\n')
        ]
        for line in regs:
            parts = line.split()
            key = parts[0]

            if m := re.search(r'(\[.*?\])', line):
                val = m.group(1)
            else:
                val = parts[1]

            if key in R and R[key] != val:
                print(key, R[key], '->', val)
            R[key] = val

gdb.events.stop.connect(stop_handler)

gdb.execute('file tools/system')
gdb.execute('set architecture i386')
gdb.execute('target remote :1234')
gdb.execute('b *0x7C00')            # 进入到的是 boot/bootsect.s 的 _start 处，此时可以调试 bootsect
                                    #（注：gdb打印的pc是eip，而非 x86 的 cs:eip)
                                    # 在进入 bootsec 时，cs=0，此时设置断点地址到 0x7C00 即可

gdb.execute('c')
