/* E06-S03 - the game's audio, played through waveOut.
 *
 * waveOut (winmm) is present on every Windows 95 installation; DirectSound, the
 * ticket's first choice, depends on the DirectX version and on the card's driver.
 * This is the route that is always there. Its latency is tens of milliseconds,
 * which DKR's audio manager absorbs: it paces its synthesis on the length of the
 * queue (osAiGetLength), which `dkr_audio_out_feedback_frames` answers the same
 * way the SDL path does on the modern targets.
 *
 * Stereo, signed 16-bit, at the frequency the game asks for (22,050 Hz for DKR).
 * No locking: every call comes from a guest thread, and ultramodern runs one guest
 * thread at a time.
 */
#ifndef DKR_WIN95_AUDIO_OUT_H
#define DKR_WIN95_AUDIO_OUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opens (or reopens) the device at `hz`. Returns 1 on success. */
int dkr_audio_out_open(unsigned int hz);
void dkr_audio_out_close(void);

/* Queues `frames` stereo frames, interleaved left then right. Returns the frames
   accepted: fewer when every buffer of the ring is still playing. */
unsigned int dkr_audio_out_write(const short *frames_lr, unsigned int frames);

/* What osAiGetLength should report, in frames. See the .c for the formula. */
unsigned int dkr_audio_out_feedback_frames(void);

/* For the log when opening fails: how many waveOut devices Windows reports,
   and the last MMRESULT of waveOutOpen (2 BADDEVICEID, 32 WAVERR_BADFORMAT...). */
unsigned int dkr_audio_out_devices(void);
unsigned int dkr_audio_out_last_error(void);

/* Counters for the log: frames written, underruns, frames dropped (ring full). */
void dkr_audio_out_stats(unsigned long *written, unsigned long *underruns,
                         unsigned long *dropped);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_AUDIO_OUT_H */
