#include <conio.h>

/* Pipeline smoke test: compile with cl65, pack onto a d64, boot in VICE.
   See README.md in this folder for the build/run walkthrough. */
int main(void)
{
    clrscr();
    cprintf("hello, world!\r\n");
    cprintf("c64-c-games pipeline is alive.\r\n");

    while (1) {
        /* stay on screen instead of dropping back to BASIC */
    }

    return 0;
}
