#include <6502.h>
#include <c64.h>
#include <conio.h>

/* Standalone keyboard-matrix tester: no assumptions about which CIA
   register is "row" and which is "column" (that's exactly what's been
   getting main.c's controls wrong, twice) -- just scans all 8
   CIA1_PRA select values every frame, and for whichever one has any
   bit low on CIA1_PRB, reports the raw (select-bit, test-bit) pair.
   That pair is exactly what main.c's scan_keys() needs hard-coded:
   `CIA1_PRA = ~(1 << select); ... !(CIA1_PRB & (1 << test))`.

   Press each key you want to use one at a time and note the numbers
   printed -- holding a key prints its pair once, not every frame, so
   it doesn't scroll off before you can read it. */

#define CIA1_PRA (*(volatile unsigned char *)0xDC00)
#define CIA1_PRB (*(volatile unsigned char *)0xDC01)

int main(void)
{
    unsigned char lastSelect = 0xFF, lastTest = 0xFF;

    clrscr();
    cprintf("keyboard matrix tester\r\n");
    cprintf("press one key at a time\r\n");
    cprintf("prints: select=N test=M\r\n");
    cprintf("(put those numbers into main.c's scan_keys())\r\n\r\n");

    for (;;) {
        unsigned char saved = CIA1_PRA;
        unsigned char foundSelect = 0xFF, foundTest = 0xFF;
        unsigned char sel, test, prb;

        SEI();
        for (sel = 0; sel < 8; sel++) {
            CIA1_PRA = ~(1 << sel);
            prb = CIA1_PRB;
            if (prb != 0xFF) {
                for (test = 0; test < 8; test++) {
                    if (!(prb & (1 << test))) {
                        foundSelect = sel;
                        foundTest = test;
                    }
                }
            }
        }
        CIA1_PRA = saved;
        CLI();

        if (foundSelect != 0xFF) {
            if (foundSelect != lastSelect || foundTest != lastTest) {
                cprintf("select=%u  test=%u\r\n", foundSelect, foundTest);
            }
            lastSelect = foundSelect;
            lastTest = foundTest;
        } else {
            lastSelect = 0xFF;
            lastTest = 0xFF;
        }
    }

    return 0;
}
