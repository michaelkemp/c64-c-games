#include <c64.h>
#include <cbm.h>
#include <conio.h>

#include "sid.h"
#include "note_table.h"

/* Mirrors HOLD/NOTE_REST from every song directory's pattern.h --
   there's no pattern.h here since rows come from a loaded file, not
   compiled-in data, but the sentinel byte values must match what
   tools/gen_tune.py writes. */
#define HOLD      0xFE
#define NOTE_REST 0xFF

/* Generous headroom over what any song written so far needs (the
   hymn is 320 rows); a MAX_ROWS*3-byte buffer is a few KB, easily
   affordable in the C64's ~38K free RAM. A .tune with more rows than
   this is rejected at load time (see load_tune()) rather than
   silently truncated. */
#define MAX_ROWS 1024

#define TUNE_LFN    2
#define TUNE_DEVICE 8

#ifndef GATE_GAP_FRAMES
#define GATE_GAP_FRAMES 2
#endif

static unsigned char rows[MAX_ROWS * 3]; /* interleaved soprano,alto,bass per row */
static unsigned row_count;
static unsigned char row_frames;

/* One instrument per voice (soprano, alto, bass), loaded from the
   .tune file's header -- see tools/gen_tune.py's resolve_instrument().
   Each voice's AD/SR is written once at startup and never rewritten
   during playback (only GATE toggles per note), so the SID envelope
   generator's documented "ADSR delay bug" -- a stall of up to ~34ms
   if the A/D/R *rate* is rewritten while a smaller one is still
   pending -- doesn't apply here the way it would if rates were being
   swapped in and out per note or per row (as sid.c's comment notes
   voice 3 used to do in an earlier version of this idea). If a
   particular instrument's attack/decay/release ever sounds audibly
   delayed on real hardware, that bug is the first thing to suspect. */
static unsigned char voice_waveform[3];
static unsigned voice_pw[3];
static unsigned char voice_ad[3];
static unsigned char voice_sr[3];

/* Reads exactly `size` bytes or fails -- cbm_read() isn't guaranteed
   to fill the buffer in one call, unlike host-OS read(). Returns 0 on
   success, nonzero if the file ran out first. */
static unsigned char read_exact(unsigned char lfn, void *buffer, unsigned size)
{
    unsigned char *p = (unsigned char *)buffer;
    int n;
    while (size > 0) {
        n = cbm_read(lfn, p, size);
        if (n <= 0) {
            return 1;
        }
        p += n;
        size -= n;
    }
    return 0;
}

/* Prints why the load failed and halts -- there's no graceful "now
   what" for a player with nothing to play, but a silent hang would be
   much harder to debug than a message on screen. */
static void fail(const char *reason)
{
    cbm_close(TUNE_LFN);
    clrscr();
    cprintf("tune load failed:\r\n%s", reason);
    __asm__("jmp *"); /* halt */
}

/* Loads a .tune file (see tools/gen_tune.py for the format) from
   device 8. */
static void load_tune(const char *name)
{
    unsigned char header[5];

    /* CBM_SEQ, not CBM_READ/CBM_WRITE -- those secondary addresses
       (0/1) are the KERNAL LOAD/SAVE convention. This just OPENs the
       file for a plain byte-by-byte read (cbm_read()/CHKIN, not the
       LOAD vector), so the file's on-disk type (however c1541
       happened to write it) doesn't matter -- no "first two bytes
       are a load address" stripping applies here either way. */
    if (cbm_open(TUNE_LFN, TUNE_DEVICE, CBM_SEQ, name) != 0) {
        fail("can't open file");
    }
    if (read_exact(TUNE_LFN, header, sizeof(header))) {
        fail("truncated header");
    }
    /* 0x54, not 'T': cc65 compiles char literals through its default
       PETSCII charset mapping, which isn't guaranteed to land on the
       same byte value tools/gen_tune.py's ord('T') wrote -- confirmed
       by hand (a debug dump here once showed the file's bytes were
       exactly right and this comparison was the thing that was wrong).
       A raw byte value has no such ambiguity. */
    if (header[0] != 0x54 || header[1] != 2) {
        fail("bad magic/version");
    }

    row_frames = header[2];
    row_count = header[3] | (header[4] << 8);
    if (row_count > MAX_ROWS) {
        fail("too many rows for MAX_ROWS");
    }

    {
        unsigned char instruments[15];
        unsigned char i, off;
        if (read_exact(TUNE_LFN, instruments, sizeof(instruments))) {
            fail("truncated instrument block");
        }
        for (i = 0; i < 3; i++) {
            off = i * 5;
            voice_waveform[i] = instruments[off];
            voice_pw[i] = instruments[off + 1] | ((unsigned)instruments[off + 2] << 8);
            voice_ad[i] = instruments[off + 3];
            voice_sr[i] = instruments[off + 4];
        }
    }

    if (read_exact(TUNE_LFN, rows, (unsigned)row_count * 3)) {
        fail("truncated row data");
    }
    cbm_close(TUNE_LFN);
}

static void wait_frame(void)
{
    while (VIC.rasterline != 250) ;
    while (VIC.rasterline == 250) ;
}

static void wait_frames(unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        wait_frame();
    }
}

static void step_voice(struct __sid_voice *voice, unsigned char note, unsigned char waveform)
{
    if (note == HOLD) {
        return;
    }
    if (note == NOTE_REST) {
        sid_note_off(voice);
        return;
    }
    sid_note_on(voice, NOTE_FREQ[note], waveform);
}

static void play_row(unsigned i, unsigned next_i)
{
    const unsigned char *row = &rows[i * 3];
    const unsigned char *next = &rows[next_i * 3];

    step_voice(&SID.v1, row[0], voice_waveform[0]);
    step_voice(&SID.v2, row[1], voice_waveform[1]);
    step_voice(&SID.v3, row[2], voice_waveform[2]);

    wait_frames(row_frames - GATE_GAP_FRAMES);

    if (next[0] != HOLD) {
        sid_note_off(&SID.v1);
    }
    if (next[1] != HOLD) {
        sid_note_off(&SID.v2);
    }
    if (next[2] != HOLD) {
        sid_note_off(&SID.v3);
    }

    wait_frames(GATE_GAP_FRAMES);
}

int main(void)
{
    unsigned i;

    load_tune("TUNE");

    sid_init();
    sid_set_envelope(&SID.v1, voice_ad[0], voice_sr[0]);
    sid_set_envelope(&SID.v2, voice_ad[1], voice_sr[1]);
    sid_set_envelope(&SID.v3, voice_ad[2], voice_sr[2]);
    /* Pulse width only matters for WAVE_PULSE voices, but it's harmless
       to set on every voice -- the other waveforms ignore it. */
    SID.v1.pw = voice_pw[0];
    SID.v2.pw = voice_pw[1];
    SID.v3.pw = voice_pw[2];

    while (1) {
        for (i = 0; i < row_count; i++) {
            play_row(i, (i + 1) % row_count);
        }
    }

    return 0;
}
