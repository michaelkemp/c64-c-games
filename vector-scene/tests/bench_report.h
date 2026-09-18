#ifndef BENCH_REPORT_H
#define BENCH_REPORT_H

/* Every benchmark in this folder is read by tools/bench.sh through
   VICE's remote monitor rather than by eyeballing the emulator screen
   (see README.md's "Automated benchmarking" section) -- that needs the
   result sitting at a fixed, known memory address as raw bytes, not
   just printed as text via conio.

   $3F00-$3F04 is cfg/lowmem.cfg's SCRATCH region -- 256 bytes ld65
   guarantees are never handed to CODE/RODATA/DATA/BSS/the stack (see
   that file's comment for why this needs to be a linker-enforced
   guarantee, not a guess: an earlier version used a bare "$2000 is
   probably still empty" address, which for a large enough program
   (two full asm modules plus a table) turned out to be inside the
   program's own compiled code, corrupting it. report_result() is
   still followed by a normal cprintf in every bench's main() too, so a
   human watching the emulator directly sees the same number. */

#define BENCH_RESULT_ADDR 0x3F00
#define BENCH_READY_BYTE  0xA5

static void report_result(unsigned long cycles)
{
    volatile unsigned char *r = (volatile unsigned char *)BENCH_RESULT_ADDR;
    r[0] = (unsigned char)(cycles >> 24);
    r[1] = (unsigned char)(cycles >> 16);
    r[2] = (unsigned char)(cycles >> 8);
    r[3] = (unsigned char)(cycles);
    r[4] = BENCH_READY_BYTE; /* written last: the flag tools/bench.sh polls for */
}

#endif
