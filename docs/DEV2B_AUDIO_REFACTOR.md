# dev.2b audio-refactor r1

This revision changes only the audio qualification path and leaves the dev.2a Spotify Zeroconf discovery gate intact.

## Root cause addressed

WLED AudioReactive uses 32-bit I2S samples by default. The qualified WLED Buzzer shared-I2S backend therefore follows the AudioReactive 22.05 kHz clock family and can emit 32-bit TX slots. The previous Spotify backend followed the shared clocks but configured I2S1/ES8311 as 16-bit, while feeding 16-bit stereo directly to the slave TX path. That mismatch can produce distorted/noisy output and incorrect apparent playback duration even though `i2s_write()` returns success.

## r1 design

- canonical Spotify/test PCM remains signed 16-bit stereo;
- standalone output remains 44.1 kHz / 16-bit stereo;
- shared AudioReactive mode is 22.05 kHz / **32-bit stereo slots**;
- the output task expands each signed 16-bit sample into the upper 16 bits of a signed 32-bit I2S slot;
- DMA remains 8 x 256 frames, matching the hardware-qualified Buzzer geometry;
- audio task is priority 1 and unpinned, matching the qualified Buzzer scheduling model;
- the test tone is generated inside the audio task, so WLED loop/Web/JSON timing cannot modulate the tone;
- `/spotify-test?action=start&tone=1000` runs indefinitely until `/spotify-test?action=stop`;
- the WLED Usermod Volume field is converted to a 0..100 slider by `appendConfigData()`.

## Hardware gate

1. Enable Spotify Connect and save.
2. Set the new Volume slider to a comfortable value (70% recommended) and save.
3. Reboot.
4. Confirm `/json/info` reports `source 16-bit stereo -> output 32-bit` in shared mode.
5. Start `/spotify-test?action=start&tone=1000`.
6. Let it run for at least 60 seconds, then stop it explicitly.
7. Expected: clean, constant pitch; no early stop; `err=0`, `short=0`, `late=0` or at least no sustained late-write growth; no reset.
