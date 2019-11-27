#ifndef __ia16
// Make VSCode's C analyzer work
#define __far
#endif

//
// Utility types
//

typedef unsigned char byte;
// ia16-gcc likes unsigned integers better sometimes
typedef unsigned int uint;

//
// Convenient access to the low and high bytes of a word (like l/h registers)
//

typedef union {
    uint word;
    struct
    {
        byte lo;
        byte hi;
    };
} uint_lh;

static inline byte HI(uint n) { return n >> 8; }
static inline byte LO(uint n) { return (byte)n; }

//
// Video memory access
//

static char __far *const vidc = (char __far *)0xB8000000;
// Video memory word at `addr`
#define vidw(addr) (*(uint __far *)(vidc + addr))

//
// Replicas of x86 instructions
//
// Store word `value` at `addr`, increment `addr`
#define vid_stosw(addr, value)  \
    {                       \
        vidw(addr) = value; \
        addr += 2;          \
    }

#define outb(port, value) \
    asm volatile(         \
        "outb %%al, %0\n" \
        :                 \
        : "i"(port), "Ral"((byte)(value)));

// Note: `value` should be a `byte` type variable
#define inb(port, value) \
    asm volatile(        \
        "inb %1, %%al\n" \
        : "=Ral"(value)  \
        : "i"(port));
