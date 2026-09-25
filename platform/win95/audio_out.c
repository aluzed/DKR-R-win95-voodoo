/* E06-S03 - implementation. See audio_out.h. */
#include "audio_out.h"

#include <windows.h>
#include <mmsystem.h>
#include <string.h>

#define RING_BUFFERS 16
#define RING_FRAMES  4096   /* per buffer; DKR's blocks are 720 to 848 frames */

static HWAVEOUT     g_out;
static WAVEHDR      g_hdr[RING_BUFFERS];
static short        g_pcm[RING_BUFFERS][RING_FRAMES * 2];
static unsigned int g_next;            /* next buffer of the ring to fill */
static unsigned int g_hz;

/* The feedback's state, the SDL path's (runtime_platform.cpp) in the same terms. */
static int           g_started;        /* playing, rather than priming */
static unsigned long g_written;        /* frames handed to waveOut since open */
static unsigned long g_played_base;    /* waveOut's position counts from the last reset */
static unsigned int  g_nominal_block;  /* the first block's size */
static unsigned int  g_cushion = 2;    /* blocks held back before playback starts */
static unsigned long g_underruns, g_dropped;
static unsigned int  g_last_error;     /* waveOutOpen's last MMRESULT */

static unsigned long played_frames(void)
{
    MMTIME t;
    t.wType = TIME_SAMPLES;
    if (g_out == NULL || waveOutGetPosition(g_out, &t, sizeof(t)) != MMSYSERR_NOERROR ||
        t.wType != TIME_SAMPLES) {
        return g_played_base;
    }
    return g_played_base + t.u.sample;
}

static unsigned long queued_frames(void)
{
    const unsigned long played = played_frames();
    return g_written > played ? g_written - played : 0;
}

static unsigned int prime_target(void)
{
    return g_nominal_block * g_cushion;
}

int dkr_audio_out_open(unsigned int hz)
{
    WAVEFORMATEX f;
    int i;
    if (g_out != NULL && g_hz == hz) { return 1; }
    dkr_audio_out_close();
    g_hz = 0;
    memset(&f, 0, sizeof(f));
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = hz;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = hz * 4u;
    {
        const MMRESULT r = waveOutOpen(&g_out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL);
        g_last_error = (unsigned int)r;
        if (r != MMSYSERR_NOERROR) {
            g_out = NULL;
            return 0;
        }
    }
    /* Paused until the cushion is queued: starting on the first block would
       underrun on the second. */
    waveOutPause(g_out);
    for (i = 0; i < RING_BUFFERS; i++) {
        memset(&g_hdr[i], 0, sizeof(g_hdr[i]));
        g_hdr[i].lpData = (LPSTR)g_pcm[i];
        g_hdr[i].dwBufferLength = sizeof(g_pcm[i]);
        waveOutPrepareHeader(g_out, &g_hdr[i], sizeof(g_hdr[i]));
        g_hdr[i].dwFlags |= WHDR_DONE;     /* free */
    }
    g_hz = hz;
    g_next = 0;
    g_started = 0;
    g_written = 0;
    g_played_base = 0;
    g_nominal_block = 0;
    g_cushion = 2;
    return 1;
}

void dkr_audio_out_close(void)
{
    int i;
    if (g_out == NULL) { return; }
    waveOutReset(g_out);
    for (i = 0; i < RING_BUFFERS; i++) {
        waveOutUnprepareHeader(g_out, &g_hdr[i], sizeof(g_hdr[i]));
    }
    waveOutClose(g_out);
    g_out = NULL;
}

unsigned int dkr_audio_out_write(const short *frames_lr, unsigned int frames)
{
    WAVEHDR *h;
    if (g_out == NULL || frames == 0) { return 0; }
    if (frames > RING_FRAMES) { frames = RING_FRAMES; }
    if (g_nominal_block == 0) { g_nominal_block = frames; }

    /* Underrun: playing, and nothing left in the queue. Pause, and prime again
       with one more block of cushion, up to three -- the SDL path's rule. */
    if (g_started && queued_frames() == 0) {
        g_underruns++;
        waveOutPause(g_out);
        g_started = 0;
        if (g_cushion < 3) { g_cushion++; }
    }

    h = &g_hdr[g_next];
    if (!(h->dwFlags & WHDR_DONE)) {
        g_dropped += frames;           /* every buffer still queued */
        return 0;
    }
    memcpy(h->lpData, frames_lr, (size_t)frames * 4u);
    h->dwBufferLength = frames * 4u;
    h->dwFlags &= ~WHDR_DONE;
    waveOutWrite(g_out, h, sizeof(*h));
    g_next = (g_next + 1) % RING_BUFFERS;
    g_written += frames;

    if (!g_started && queued_frames() >= prime_target()) {
        waveOutRestart(g_out);
        g_started = 1;
    }
    return frames;
}

/* osAiGetLength, as the SDL path answers it. DKR's audio manager picks its next
   synthesis quantum (720 to 848 frames at 22,050 Hz) from the AI's reported
   residual. The host cushion is excluded from what is reported: exposing it
   would make every decision look full and lock production to the minimum.
   While priming, the AI reads as idle, so that the manager fills the cushion
   quickly. The report never exceeds one DMA, a thirtieth of a second. */
unsigned int dkr_audio_out_feedback_frames(void)
{
    unsigned long queued;
    unsigned int authored, target_residual, protected_frames, one_dma;
    if (g_out == NULL || !g_started) { return 0; }
    queued = queued_frames();
    authored = ((g_hz + 29u) / 30u + 15u) & ~15u;
    target_residual = g_nominal_block > authored ? g_nominal_block - authored : 0;
    protected_frames = prime_target() > target_residual ? prime_target() - target_residual : 0;
    one_dma = g_hz / 30u ? g_hz / 30u : 1u;
    if (queued <= protected_frames) { return 0; }
    queued -= protected_frames;
    return queued < one_dma ? (unsigned int)queued : one_dma;
}

void dkr_audio_out_stats(unsigned long *written, unsigned long *underruns,
                         unsigned long *dropped)
{
    if (written) { *written = g_written; }
    if (underruns) { *underruns = g_underruns; }
    if (dropped) { *dropped = g_dropped; }
}

unsigned int dkr_audio_out_devices(void) { return (unsigned int)waveOutGetNumDevs(); }
unsigned int dkr_audio_out_last_error(void) { return g_last_error; }
