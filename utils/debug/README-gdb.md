# General Advices

As much as possible the debug should be done on the machine where the core file was created.
When debugging on a differeent node, some libraries could eventually be different, and gdb will not
be able to rebuild the calling stack.  In such situation, the configuration of the defunct host has
to be reproduce which is not an easy task: using docker container should be evaluated.  If it is not
possible, the stack unwinding has to be done manually using the stack pointer (i.e. rsp register)
and the function call addresses recorded in the stack.

To have the source code annotation, the debuginfo and the source rpms of the executable and its
libraries shall be installed.

Install of the glibc debuginfo rpms could be very useful for debugging heap memory corruption.


# GDB Tips

## Useful Commands

```GDB
p /x $rip # Print @ of the instruction pointer
p /x $rsp # Print @ of the stack pointer
p /x $rdi # Print value of the function first parameter
x /10i <@>  # Dump 10 instructions stored in mem before @
x /10gx	<@> # Dump 10 quad words
x /s <@>    # Dump string

disass /s <@> # Dump disassembled code from @ as machine instructions and source source code
              # This command accept range of address as arguments
```

```GDB
info shared       # list the shared libs loaded
info proc mapping # defines the offset of the loaded shared libs
```

```GDB
thread apply all bt # Print the backtrace of all the threads
```


## Assembler tips

Default GNU C calling convention use some registers to pass parameters:
- `rdi`: first function's parameter
- `rsi`: second function's parameter
- `rdx`: third function's parameter
- `rcx`: fourth function's parameter
- `r8`: fiveth function's parameter
- `r9`: sixth function's parameter
- `rax`: function's return value
Other parameters should be passed with the stack.

The prefix name of a register define its size:
- `r`: 64 bits
- `e`: 32 bits
- `l`: 16 bits

## Memory Corruption Tips

When the `free()` function is failing it could be due to some buffer overflow which have corrupted
the header of a malloc chunk.
The size of a malloc chunk is defined by the `MALLOC_HEADER_SIZE` constant and is located just
before the chunk of storage return by malloc.

## Buffer Overflow Example

A code simulating a buffer overflow could be find in _examples/src/malloc\_overflow.c_.

### With the Call Stack

The output of the _malloc_overflow_ executable indicates that the call to the function `free()`
aborted.  This statement is validated with the backtrace shown by gdb:

```GDB
(gdb) bt
#0  0x00007fb50419ca9f in raise () from /lib64/libc.so.6
#1  0x00007fb50416fe05 in abort () from /lib64/libc.so.6
#2  0x00007fb5041df037 in __libc_message () from /lib64/libc.so.6
#3  0x00007fb5041e619c in malloc_printerr () from /lib64/libc.so.6
#4  0x00007fb5041e7a9c in _int_free () from /lib64/libc.so.6
#5  0x0000000000400735 in main () at malloc_overflow.c:24
```

In this case the program have been compiled with the -g and thus it is not that hard to debug

```GDB
(gdb) list malloc_overflow.c:24
19              }
20
21              printf("Malloc header value: 0x%x\n", *(size_t*)(buffer-1));
22
23              printf("Deallocating buffer...\n");
24              free(buffer);
25
26              return 0;
27      }
(gdb) frame 5
#5  0x0000000000400735 in main () at malloc_overflow.c:24
24              free(buffer);
(gdb) x/8g buffer-2
0x1a5a6a0:      0xffffffffffffffff      0x0
0x1a5a6b0:      0x1     0x2
0x1a5a6c0:      0x3     0x4
0x1a5a6d0:      0x5     0x6
```

This last command show that the malloc chunk header has been corrupted.

### Without the Call Stack

If gdb is not able to unwind the call stack, the only solution that we have is in a first time to
look at the assembly code and the content of the stack.

```GDB
(gdb) x/160a $rsp
0x7ffffd8695d0: 0x0     0x0
0x7ffffd8695e0: 0x7fb3ea3ba4e0  0x7fb3ea006cd8
...
0x7ffffd8696e0: 0x7ffffd869840  0x7fb3ea016e05 <abort+295>
...
0x7ffffd869930: 0x0     0x7fb3ea08ea9c <_int_free+748>
...
0x7ffffd8699b0: 0x0     0x400735 <main+207>
...
```

In the stack we could see that a main relative address equal to 0x400735 (i.e \<main+207\>)  was
stored at 0x7ffffd8699b0 just before a free relative address equal to 0x7fb3ea08ea9c (i.e.
\<_int_free+748\>).  The disassemble command allow to validate this hypothesis.

```GDB
(gdb) disassemble 0x400735
Dump of assembler code for function main:
   0x0000000000400666 <+0>:     push   %rbp
   0x0000000000400667 <+1>:     mov    %rsp,%rbp
   0x000000000040066a <+4>:     sub    $0x20,%rsp
   ...
   0x0000000000400688 <+34>:    mov    %rax,%rdi
   0x000000000040068b <+37>:    callq  0x400570 <malloc@plt>
   0x0000000000400690 <+42>:    mov    %rax,-0x18(%rbp)
   0x0000000000400694 <+46>:    mov    -0x18(%rbp),%rax
   ...
   0x0000000000400729 <+195>:   mov    -0x18(%rbp),%rax
   0x000000000040072d <+199>:   mov    %rax,%rdi
   0x0000000000400730 <+202>:   callq  0x400540 <free@plt>
   0x0000000000400735 <+207>:   mov    $0x0,%eax
   ...
```

As we are not able to unwind the call stack, thus we will have to use the assembler code to find the
address of the buffer returned by malloc.  We could see that the `free()` function is called with
the address of the buffer in the `rdi` register as it is the first parameter of this function.
However the value of this register could not be trusted as it could have been overwritten in the
code of the following function calls.  However, we could see that this address was retrieve from the
memory:

```GDB
   0x0000000000400729 <+195>:   mov    -0x18(%rbp),%rax
```

In the prologue of the function we could see that the `rbp` register is used for storing the value
of the stack pointer (i.e. `rsp` register):

```GDB
   0x0000000000400666 <+0>:     push   %rbp
   0x0000000000400667 <+1>:     mov    %rsp,%rbp
   0x000000000040066a <+4>:     sub    $0x20,%rsp
```

The `rbp` register is then used for storing the address of the buffer:

```GDB
   0x000000000040068b <+37>:    callq  0x400570 <malloc@plt>
   0x0000000000400690 <+42>:    mov    %rax,-0x18(%rbp)
```

Thus at the instruction \<+42\>, the address of the buffer is recorded at the following place in the
buffer.

<pre>
                |                                |
                +--------------------------------+
         -0x20  |                                | <- rsp
                +--------------------------------+
         -0x18  | buffer@                        |
                +--------------------------------+
         -0x10  |                                |
                +--------------------------------+
         -0x08  |                                |
                +--------------------------------+
          0x00  | old rsp value                  | <- rbp
                +--------------------------------+
                | return @ from main()           |
                +--------------------------------+
                |                                |
</pre>

The `rbp` register is then used for retrieving the address of the buffer from stack and pass it to
the `free()` function through the `rdi` register.

```
   0x0000000000400729 <+195>:   mov    -0x18(%rbp),%rax
   0x000000000040072d <+199>:   mov    %rax,%rdi
   0x0000000000400730 <+202>:   callq  0x400540 <free@plt>
```

The stack of the return address done by the `callq` instruction at line \<+202\> could be used for finding the
address of the buffer stored into the stack.

<pre>
                |                                |
                +--------------------------------+
          0x0   | return @ from free()           |
                +--------------------------------+
         +0x08  |                                | <- rsp
                +--------------------------------+
         +0x10  | buffer@                        |
                +--------------------------------+
                |                                |
                +--------------------------------+
                |                                |
                +--------------------------------+
                | old rsp value                  | <- rbp
                +--------------------------------+
                | return @ from main()           |
                +--------------------------------+
                |                                |
</pre>

We are now able get the address of the buffer and to show that the malloc chunk header has been
corrupted.

```GDB
(gdb) x/a 0x7ffffd8699b8
0x7ffffd8699b8: 0x400735 <main+207>
(gdb) p/x 0x7ffffd8699b8+0x10
$4 = 0x7ffffd8699c8
(gdb) x/a 0x7ffffd8699c8
0x7ffffd8699c8: 0x9b96b0
(gdb) x/8g 0x9b96b0-0x10
0x9b96a0:       0xffffffffffffffff      0x0
0x9b96b0:       0x1     0x2
0x9b96c0:       0x3     0x4
0x9b96d0:       0x5     0x6
```

## Segmentation Fault

In the directory _examples/patch_ could be find the following patches:
- _0000-daos-segfault.patch_: This patch introduce a segmentation fault in the code of the
  _daos_engine_.
- _0000-daos-dump_stack.patch_: This patch introduce a new function `dump_stack()` allowing to print
  a backtrace in a json format.
- _0001-daos-print_stack.patch_: This patch print a backtrace just before the segfault introuced by
   the patch _0000-daos-segfault.patch_.

On the following backtrace we could see that the `SEGFAULT` occurred in the `get_frag_overhead()`
function from the _libvos_srv.so_ shared lib.

```GDB
(gdb) bt
#0  0x00007f413ee97983 in get_frag_overhead () from /usr/lib64/daos_srv/libvos_srv.so
#1  0x00007f413ee97d74 in vos_space_sys_init () from /usr/lib64/daos_srv/libvos_srv.so
#2  0x00007f413ee046ab in pool_open () from /usr/lib64/daos_srv/libvos_srv.so
#3  0x00007f413ee0acc3 in vos_pool_create_ex () from /usr/lib64/daos_srv/libvos_srv.so
#4  0x00007f413ee0b37e in vos_pool_create () from /usr/lib64/daos_srv/libvos_srv.so
#5  0x00007f413ee9aaeb in db_open_create () from /usr/lib64/daos_srv/libvos_srv.so
#6  0x00007f413ee9c529 in vos_db_init_ex () from /usr/lib64/daos_srv/libvos_srv.so
#7  0x00007f413ee9c637 in vos_db_init () from /usr/lib64/daos_srv/libvos_srv.so
#8  0x0000555d9cd7dab3 in dss_sys_db_init ()
#9  0x0000555d9cd87a82 in dss_srv_init ()
#10 0x0000555d9cd6a8b4 in server_init ()
#11 0x0000555d9cd6ba92 in main ()
```

Not having the file containing this functions indicates that the _debuginfo_ of this shared library
could not be find by gdb.

```GDB
(gdb) info sharedlibrary
From                To                  Syms Read   Shared Object Library
...
0x00007f413edd2450  0x00007f413eebd3a7  Yes (*)     /usr/lib64/daos_srv/libvos_srv.so
...
(*): Shared library is missing debugging information.
```

Thus, the only way to find where the `SEGFAULT` occurred in the code is to look at the assembly code
and associate it with the C code.

```GDB
(gdb) disassemble $rip
Dump of assembler code for function get_frag_overhead:
   ...
   0x00007f413ee9794e <+185>:   callq  0x7f413edd0ee0 <dump_stack@plt>
   0x00007f413ee97953 <+190>:   mov    0x25561e(%rip),%rax        # 0x7f413f0ecf78
   0x00007f413ee9795a <+197>:   mov    0x10(%rax),%edi
   0x00007f413ee9795d <+200>:   test   %edi,%edi
   0x00007f413ee9795f <+202>:   jne    0x7f413ee9798c <get_frag_overhead+247>
   0x00007f413ee97961 <+204>:   lea    0xc(%rsp),%rdx
   0x00007f413ee97966 <+209>:   lea    0x8(%rsp),%rax
   0x00007f413ee9796b <+214>:   cmp    %rax,%rdx
   0x00007f413ee9796e <+217>:   jbe    0x7f413ee97a84 <get_frag_overhead+495>
   0x00007f413ee97974 <+223>:   mov    %rdx,%rax
   0x00007f413ee97977 <+226>:   lea    0x8(%rsp),%rdx
   0x00007f413ee9797c <+231>:   sub    %rdx,%rax
   0x00007f413ee9797f <+234>:   sar    $0x2,%rax
=> 0x00007f413ee97983 <+238>:   mov    (%rax),%esi
   0x00007f413ee97985 <+240>:   lea    0x1(%rsi),%edx
   0x00007f413ee97988 <+243>:   mov    %edx,(%rax)
   ...
(gdb) p/x $rax
$1 = 0x1
```

In the assembly code code, we could see that the move to the memory at the address 0x1 is the origin
of the `SEGFAULT`.  At line +185 we can see a call to the function `dump_stack()` which could be
used to identify which memory access is faulty.

```C
 24 static inline daos_size_t
 25 get_frag_overhead(daos_size_t tot_size, int media, bool small_pool)
 26 {
 27         daos_size_t     min_sz = (2ULL << 30);  /* 2GB */
 28         daos_size_t     max_sz = (10ULL << 30); /* 10GB */
 29         daos_size_t     ovhd = (tot_size * 5) / 100;
 30
 31         /*
 32          * Don't reserve NVMe, if NVMe allocation failed due to fragmentations,
 33          * only data coalescing in aggregation will be affected, punch and GC
 34          * won't be affected.
 35          */
 36         if (media == DAOS_MEDIA_NVME)
 37                 return 0;
 38
 39         /* If caller specified the pool is small, do not enforce a range */
 40         if (!small_pool) {
 41                 if (ovhd < min_sz)
 42                         ovhd = min_sz;
 43                 else if (ovhd > max_sz)
 44                         ovhd = max_sz;
 45         }
 46
 47         if (media == DAOS_MEDIA_SCM) {
 48                 int *my_pointer ;
 49                 char call_stack[2048];
 50
 51                 dump_stack(call_stack, sizeof(call_stack));
 52                 D_ERROR("Dump stack before SEGV: stack=%s", call_stack);
 53
 54                 my_pointer=(int*)((&media > (int*)&small_pool)?(&media-(int*)&small_pool):((int*)&small_pool-&media));
 55                 (*(int*)my_pointer)++;
 56         }
 57
 58         return ovhd;
 59 }
```

With lookiing at the source code, we can conclude that the invalid memory access should be done at
line 55.  This can be confirmed with installing the debuginfo rpms.

```GDB
(gdb) disassemble /s $rip
Dump of assembler code for function get_frag_overhead:
src/vos/vos_space.c:
26      {
   0x00007f413ee97895 <+0>:     push   %rbp
   0x00007f413ee97896 <+1>:     push   %rbx
   0x00007f413ee97897 <+2>:     sub    $0x828,%rsp
   0x00007f413ee9789e <+9>:     mov    %esi,0xc(%rsp)
   ...
51                      dump_stack(call_stack, sizeof(call_stack));
   0x00007f413ee97944 <+175>:   lea    0x10(%rsp),%rdi
   0x00007f413ee97949 <+180>:   mov    $0x800,%esi
   0x00007f413ee9794e <+185>:   callq  0x7f413edd0ee0 <dump_stack@plt>

52                      D_ERROR("Dump stack before SEGV: stack=%s", call_stack);
   0x00007f413ee97953 <+190>:   mov    0x25561e(%rip),%rax        # 0x7f413f0ecf78
   0x00007f413ee9795a <+197>:   mov    0x10(%rax),%edi
   0x00007f413ee9795d <+200>:   test   %edi,%edi
   0x00007f413ee9795f <+202>:   jne    0x7f413ee9798c <get_frag_overhead+247>

53
54                      my_pointer=(int*)((&media > (int*)&small_pool)?(&media-(int*)&small_pool):((int*)&small_pool-&media));
   0x00007f413ee97961 <+204>:   lea    0xc(%rsp),%rdx
   0x00007f413ee97966 <+209>:   lea    0x8(%rsp),%rax
   0x00007f413ee9796b <+214>:   cmp    %rax,%rdx
   0x00007f413ee9796e <+217>:   jbe    0x7f413ee97a84 <get_frag_overhead+495>
   0x00007f413ee97974 <+223>:   mov    %rdx,%rax
   0x00007f413ee97977 <+226>:   lea    0x8(%rsp),%rdx
   0x00007f413ee9797c <+231>:   sub    %rdx,%rax
   0x00007f413ee9797f <+234>:   sar    $0x2,%rax

55                      (*(int*)my_pointer)++;
=> 0x00007f413ee97983 <+238>:   mov    (%rax),%esi
   0x00007f413ee97985 <+240>:   lea    0x1(%rsi),%edx
   0x00007f413ee97988 <+243>:   mov    %edx,(%rax)
   0x00007f413ee9798a <+245>:   jmp    0x7f413ee97920 <get_frag_overhead+139>
   ...
End of assembler dump.
```

If gdb was not able to unwind the callstack this could be done manually with investigating the
stack.  In this use case what is important to note that the allocation of the the `call_stack`
array will makes the compiler stack at least 2048 (0x800) _char_ at the beginning of the function.
This is confirmed with looking at line <+2> of the function.  Thus the caller address of
this function should be at least beyond 0x828 of the current value of the `rsp` register. To this
value we could also add the previous two `push` instructions.

```GDB
(gdb) x/10a $rsp+0x838
0x7ffc465cafe8: 0x7f413ee97d74 <vos_space_sys_init+45>  0x0
0x7ffc465caff8: 0x7ffc465cb090  0x0
0x7ffc465cb008: 0x7f41157be490  0x7f41157be4b8
0x7ffc465cb018: 0x7f413ee046ab <pool_open+2293> 0x7f413e077008
0x7ffc465cb028: 0x0     0x203de689dc
(gdb) disassemble vos_space_sys_init+45
Dump of assembler code for function vos_space_sys_init:
   0x00007f413ee97d47 <+0>:     push   %r13
   0x00007f413ee97d49 <+2>:     push   %r12
   0x00007f413ee97d4b <+4>:     push   %rbp
   0x00007f413ee97d4c <+5>:     push   %rbx
   0x00007f413ee97d4d <+6>:     sub    $0x8,%rsp
   ...
   0x00007f413ee97d6c <+37>:    mov    %r13,%rdi
   0x00007f413ee97d6f <+40>:    callq  0x7f413ee97895 <get_frag_overhead>
   0x00007f413ee97d74 <+45>:    mov    %rax,0x108(%rbx)
   ...
End of assembler dump.
```
At the line 0x7ffc465caff0 we could indeed find the return address of the caller of the function.


# Useful Debugging Tools

The `sosreport` command could be used, to identify the libraries installed on a node where a core
file was generated.

For SLES distribution, the `supportconfig` is the equivalent of the `sosreport` command.  It can be
installed with the following command: `zypper install supportutils`.

The `eu-unstrip` command could be used with a core file to have details on the libs which were
loaded when the application crashed.

The `eu-readelf command could be used with an executable to have information on the libs linked to
this last one.

The `objdump` command could be used with an executable and libraries to find the culprit libraries
with doing some pattern matching of its assembler code and the one from the core file.

Source code of standard open project such as the linux kernel, the gnu Clib, etc. could be find at
https://elixir.bootlin.com

The perf tool could be used to find to profile the DAOS server and find critical function for
performance.


# External Libs Debugging Tips

## Argobots

When a DAOS engine is hanging, the signal SIGUSR2 could be used to dump the ULT argobot stacks.
This signal makes the target engine to generate a file "/tmp/daos\_dump\_\<date\>" containing the
call stack of each argobot ULT.  The call stacks are dumped in a symbolic way and raw way.  In the
raw way, the hexadecimal value of the stack content is dumped: this could be used to find the
function call address when the call stack is corrupted and the symbolic call stack could not be
unwind.  We can then this @ with firstly using `gdb -p  <engine pid>` and then use the command
`x /i <@>` to find the context of the hang.

Stack trace starting with the following pattern are complete Argobots stack trace:
- ???
- `make_fcontext()`
- ...

NOTE: the ULTs dump stacks are not complete: some internal Argobots stacks are missing.  A ticket
was opened to fix this issue: Ticket #391 in the Argobots GitHub project, and DAOS-10051.  Nobody
working on this until now.

Referents: Niu, LiWei, WangDi, Xuezhao

## Libifabric

Memory issues with libfabric are very common.  When such issue is discovered,  a ticket should be
opened in the GitHub of the project.  This ticket shall at least contains the trace of the core
file.

OFI is using the same kind of logging facilities as DAOS with provider, log levels...  This could be
set thanks to dedicated environment variables. With a production lib the maximal level of verbosity
is info.

Referents: Jerome, Alex


## Pmdk

Referents: Jonas

## Gcore

The `gcore` is used to generate core file of a running process.  It should be used with the `-a`
option.

## Core File congfiguration

By default, `mmap'ed` areas are not dumped in corefiles.
But this may be of interest in some cases, this page describes how to configure in order to make it
happen.

Default wide `coredump_filters` mask value is 0x33 (i.e.
`MMF_DUMP_ANON_PRIVATE | MMF_DUMP_ANON_SHARED | MMF_DUMP_ELF_HEADERS | MMF_DUMP_HUGETLB_PRIVATE`
bits).
All available bits are described in `include/linux/sched.h`.
It can be changed without recompiling Kernel by using `coredump_filter=<newvalue>` boot parameter.
It can also be changed per-process using `/proc/<PID>/coredump_filter`.
In order to allow for mmap()'ed areas to also be dumped in corefiles, both `MMF_DUMP_MAPPED_SHARED`
and `MMF_DUMP_MAPPED_PRIVATE` bits must also be present.
This can be achieved by running `echo 0x1f > /proc/<PID>/coredump_filter` command and the new mask
will be immediately active for `<PID>` process and be inherited by any forked child.

Notes: Niu should be a good person to contact on this topic.
