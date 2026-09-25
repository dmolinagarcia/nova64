---
categories: system
tags: [libc, toolchain, syscalls]
---

## printf doesn't talk to the kernel

The word "libc" appears all over the noVa64 document. Sheet O says the libc is "about twenty three-line syscall stubs". The console sheet says the shell "calls only the libc surface". I've written, read and approved sentences like those many times. Then it was time to design noVa64's own C library, and I realised I couldn't explain how one is put together.

So before designing ours, I looked at how an existing one works: Linux, glibc, and a ten-line program.

### The experiment

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int main(void)
{
    char *p = malloc(100000);            /* ask for memory */
    printf("hello, world\n");            /* print something */
    if (open("/no/such/file", O_RDONLY) < 0)
        perror("open");                  /* get an error from the kernel */
    free(p);
    return 0;
}
```

It asks for memory, prints a line and makes the kernel say no. I built it statically, so the whole library ends up inside the executable where I can look at it, and took it apart with three tools any Linux box has: `gcc -v`, `strace` and `objdump`.

### A header is a promise

The first surprise is how little is in a header. The only trace of `printf` in `stdio.h` is this line:

```c
extern int printf (const char *__restrict __format, ...);
```

It's a declaration: it tells the compiler the function exists and what it takes, so it can check my call. There is no code.

### The library is a box of spare parts

The code lives in `libc.a`, and `libc.a` is an archive of 2,211 separate object files, roughly one per function: `printf.o`, `malloc.o`, `write.o`, `strlen.o`… When the linker builds the program, it takes only the pieces the program uses, and the pieces those use.

### Your program is never alone

`gcc -v` shows what really gets linked:

```
crt1.o  crti.o  crtbeginT.o      <- startup code, placed BEFORE mine
hello.o                          <- my program
-lgcc -lc                        <- the libraries, libc among them
crtend.o  crtn.o                 <- closing code
```

That `crt1.o` is the famous *crt0*, the C runtime startup.

### main() is not where it starts

The real entry point is `_start`, inside `crt1.o`, and it's tiny:

```
_start:
    pop  %rsi            ; argc, which the kernel left on the stack
    mov  %rsp,%rdx       ; argv, right below it
    mov  $main,%rdi      ; a pointer to my main
    call __libc_start_main
    hlt                  ; never reached
```

The kernel loads the program, leaves `argc` and `argv` on the stack and jumps to `_start`. `__libc_start_main` sets up the library, calls `main`, and hands whatever `main` returns to `exit()`. `exit()` flushes whatever is still buffered, and only then asks the kernel to end the process.

### What actually reaches the kernel

`strace` lists every call that crosses into the kernel. With the output going to a file, and trimmed to the interesting lines, the program does this:

```
brk(0xec9d00)                              = 0xec9d00     <- malloc
ioctl(1, TCGETS, ...)                      = -1 ENOTTY    <- "is stdout a terminal?"
openat(AT_FDCWD, "/no/such/file", O_RDONLY) = -1 ENOENT   <- my open
write(2, "open: No such file or directory\n", 32) = 32    <- perror
write(1, "hello, world\n", 13)             = 13           <- my printf. LAST.
exit_group(0)
```

Four things there surprised me:

- **`printf` didn't talk to the kernel.** It formatted the text into a buffer inside the library, and the actual `write` happened at `exit()`, after the error message, even though `printf` ran first. The `ioctl` is the library asking whether it is talking to a terminal. Run it on a terminal and the `write` happens at the newline, before the `open`. All of stdio is ordinary code sitting on top of a single `write` and a single `read`.
- **`malloc` isn't a kernel call either.** It asked the kernel once to move the end of the heap up by about 132 KB (`brk`, also known as `sbrk`), and from then on it hands out pieces by itself. The next small `malloc` won't reach the kernel at all.
- **Errors come back as negative numbers.** The kernel answered −2, "no such file". My program saw −1 and `errno = 2`, and `perror` turned that 2 into text. The message lives in the library, and the kernel stores no text at all.
- **Out of 2,211 objects, a handful talk to the kernel.** `strlen`, `qsort`, the formatting half of `printf` and the bookkeeping half of `malloc` are plain computation.

### The door

This is glibc's `write`, disassembled from my executable. I've dropped a path for multithreaded programs:

```
write:
    mov   $1, %eax          ; service number 1 = write
    syscall                 ; into the kernel
    cmp   $-4096, %rax      ; did it return something between -4095 and -1?
    ja    error
    ret                     ; no: that's the byte count
error:
    neg   %eax              ; -2 becomes 2
    mov   %eax, errno       ; errno = 2
    mov   $-1, %rax         ; and the program sees -1
    ret
```

Four instructions, and every `write` goes through them. Two details made the rest of this post click.

**The arguments are never touched.** The file descriptor, the buffer and the length are already in `rdi`, `rsi` and `rdx`, because that's where the compiler puts the first three arguments of any function call. Linux's system calls expect them in the same registers, so there's nothing to move.

**The service number goes in a register that carries no argument.** In the C calling convention `rax` only holds return values, so the stub can overwrite it for free.

On the kernel side, Linux keeps the list of services in one file, `syscall_64.tbl`, with lines like `1 common write sys_write`. The numbers and the dispatch table are generated from it.

### The layers

Put together, a C library is four things:

| Layer | What it is | Examples | Talks to the kernel? |
|---|---|---|---|
| ISO C | What the C standard defines, the same on any system | `printf`, `strlen`, `malloc`, `fopen` | Hardly ever |
| POSIX | What "Unix" defines: files, processes, directories | `open`, `read`, `opendir` | Yes: thin wrappers |
| Stubs | The few instructions that cross | the `write` above | They are the door |
| crt0 | What runs before `main` | `_start` | Receives what the kernel left |

### How small machines do it

On Linux, the compiler, the library and the kernel are three separate projects. Small systems usually work the way **newlib** does: the library ships with all of ISO C written, and asks whoever ports it for about twenty glue functions (`_write`, `_read`, `_sbrk`, `_exit`…).

It turns out Calypsi, the C compiler the noVa64 kernel will be built with, works the same way. Its library needs a dozen glue functions called `_Stub_open`, `_Stub_read`, `_Stub_write` and so on. I learnt that by reading the startup code and glue of two public Calypsi ports, for the Foenix C256 and for an Atari 8-bit with a 65816 accelerator.

### What this means for noVa64

What came out of all this is a design note, now [sheet LB1]({{ '/docsV3/' | relative_url }}#/sec_ai_lb1) of the document. The short version is that **noVa64's C library is three layers, and we only have to write the two thin ones.** ISO C comes with Calypsi. We write the POSIX layer, the glue Calypsi's library asks for, the startup code, and the stubs that cross into the kernel through `COP`, the 65816's equivalent of `syscall`.

One trick falls out of Linux's naming for free. The console is built twice ([sheet CN1]({{ '/docsV3/' | relative_url }}#/sec_ai_cn1)): once as a single image with no processes, and once with the shell as a real user process. If the stubs are named exactly like the kernel functions behind them (`sys_write` and so on), the first build needs no stubs at all. The linker finds the kernel's own `sys_write`, and everything above it is the same code in both builds.

Reading other people's Calypsi code also turned up three things in our own document that were wrong or missing:

1. **The syscall number can't go in the accumulator.** The document has it in A ([J.3]({{ '/docsV3/' | relative_url }}#/sec_ai_j/j3)), but Calypsi passes a function's first argument in A, so `write(fd, …)` would lose its `fd`. It's the same problem Linux solved by picking `rax`: the number needs a register the calling convention doesn't use. The proposal is Y.
2. **Calypsi's `malloc` has no `sbrk`.** It manages a fixed block reserved when the program is linked. With virtual memory that's fine: a large heap that is never touched costs no physical memory, because pages only arrive when something writes to them.
3. **Small data lives in bank `$00`.** In Calypsi's small data model, plain 16-bit pointers address bank `$00`, the same bank as the stack. The document had program data sitting after the code. But on noVa64 every process already has its own private, virtual bank `$00`, so every program gets its own 64 KB for data. The first page of it is left unmapped, so a NULL pointer faults instead of quietly scribbling over the direct page.

And one practical snag: Calypsi's Linux packages are x86-64 only, while [my devbox]({{ '/docsV3/' | relative_url }}#/sc_v7) is ARM64. The plan is to run the compiler under `qemu-user`.

The sheet is still in the AI part of the document. Every decision in it is a proposal until I've reviewed it, and two of the three findings above rest on reading other people's code rather than on compiled output of my own. So the next step is to install Calypsi, compile a few functions, and check the calling convention against the real thing. That's gate [LIB-00]({{ '/docsV3/' | relative_url }}#/sec_ai_lb1/lib-00) in the sheet, and E0.8 in the build plan.

### Try it yourself

On any Linux machine with gcc:

```sh
gcc -static -o hello hello.c
strace ./hello > out.txt                    # what crosses into the kernel
strace ./hello                              # the same, with stdout on a terminal
objdump -d hello | less                     # look for <_start> and <__libc_write>
```

It takes five minutes, and it explained more to me than a lot of the reading I'd done.
