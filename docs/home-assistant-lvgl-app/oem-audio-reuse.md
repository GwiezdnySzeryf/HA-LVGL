# OEM audio reuse assessment

This document records which parts of the stock Tuya/Alexa audio stack can be
used by the native Home Assistant panel application. The tests were performed
on the MOES TPP01-Z with an RK3308 SoC and Linux 5.10.110.

## Decision

Reuse the kernel audio devices, RK3308 codec configuration, ALSA interfaces,
high-pass filters, microphone privacy controls, PCM clock compensation, and
ALSA Loopback. Do not link or redistribute the stock Alexa binaries, Pryon
Lite library, Alexa wake-word models, certificates, or sound assets.

The stock AEC path is not exposed as a reusable DSP library. The exported
`tuya_avs_audio_feed_aec()` function accepts an already processed AEC stream;
it does not perform echo cancellation. The initial Assist implementation must
therefore use push-to-talk and stop microphone streaming while TTS is playing.

## Hardware interfaces

| Purpose | ALSA endpoint | Verified format | Result |
| --- | --- | --- | --- |
| Speaker | `hw:0,0` | S16_LE, 48 kHz, stereo | Working |
| Microphone | `hw:0,0` | S16_LE, 16 kHz, stereo | Working |
| Loopback write | `hw:7,0,0` | S16_LE, 48 kHz, stereo | Working |
| Loopback read | `hw:7,1,0` | S16_LE, 48 kHz, stereo | Receives loopback writes |

Card 0 is reported as `rockchip,rk3308-vad`. Its PCM device supports S16_LE,
S24_LE, and S32_LE, 2 through 16 channels, and sample rates from 8 to 192 kHz.
The hardware does not expose a one-channel capture mode, so Assist should
capture stereo and extract the first channel into a mono 16 kHz stream.

Physical playback is not routed into ALSA Loopback automatically. A future AEC
implementation must duplicate each playback frame to the speaker and a
reference stream, or retain the exact submitted PCM frames in an in-process
ring buffer. The in-process buffer is preferable because it avoids an extra
ALSA device and provides a known playback timestamp.

## Measurements

A one-second 16 kHz stereo capture produced 64,000 bytes. In the initial test,
channel 1 had an RMS amplitude of about 8,480 and a peak of 32,002, while
channel 2 had an RMS amplitude of about 238 and a peak of 480. This identifies
channel 1 as the useful microphone channel for the first implementation.

Full-duplex capture and playback on `hw:0,0` also works. During a controlled
speaker test, microphone channel 1 rose from a background peak of -41.3 dBFS
to -20.4 dBFS. The roughly 21 dB increase confirms substantial acoustic
coupling and the need to avoid open-microphone TTS until AEC is available.

The loopback test captured the complete 48 kHz stereo test signal. Capturing
the same loopback endpoint while playing only to `hw:0,0` produced digital
silence, confirming that the loopback reference must be fed explicitly.

## Reusable codec configuration

The stock `/tuya/app/audio_setup.bin` configures useful hardware defaults:

- ADC ALC groups 0 and 1: level 14.
- ADC ALC groups 2 and 3: level 24.
- ADC high-pass filters: enabled.
- DAC LINEOUT left and right: level 2.

The device also exposes mixer controls for eight ADC inputs, MICBIAS, ALC,
high-pass filters, DAC routing, `Main Mic Switch`, `Headset Mic Switch`,
`Speaker Playback Switch`, `VAD Switch`, and PCM input/output clock
compensation. These controls are stable kernel interfaces and can be managed
without depending on the OEM applications.

`/userdata/custom_audio_setup.bin` contains locally modified values and is not
the OEM baseline. It must not be treated as the source of default calibration.

## Stock Alexa architecture

The original voice stack consists primarily of:

- `/tuya/app/bin/voice_control`: Tuya application, ALSA and GStreamer client.
- `/tuya/app/bin/SampleApp`: small launcher for the Alexa client.
- `/tuya/app/lib/libLibSampleApp.so`: Alexa Client SDK implementation.
- `/tuya/app/lib/libpryon_lite-PRL2000.so.2.16`: proprietary wake-word engine.
- `/tuya/app/res/X.*.alexa.bin`: Alexa-specific wake-word models.

`voice_control` directly depends on ALSA, GStreamer, and `libtuya_app.so`.
`libLibSampleApp.so` directly depends on ALSA, GStreamer, and Pryon Lite, but
neither binary declares a separate AEC, AGC, or noise-suppression library.

The Tuya bridge exports three similarly shaped functions:

- `tuya_avs_audio_feed_asr(data, size)`
- `tuya_avs_audio_feed_silent(data, size)`
- `tuya_avs_audio_feed_aec(data, size)`

Disassembly shows that these wrappers pass channel IDs 0, 1, and 2 to one
internal stream-routing function. The Alexa library has corresponding ASR,
silent, and AEC stream consumers. This proves that the functions transport
three precomputed streams rather than provide a callable echo-cancellation
algorithm. Any OEM DSP that generated them is statically embedded or hidden
inside the Tuya application and has no supported ABI.

Pryon Lite does expose local VAD and wake-word processing, but it is tied to
the proprietary PRL2000 library and Alexa models. It is not a suitable basis
for Home Assistant Assist.

## VAD status

The kernel contains an RK3308 VAD driver and ALSA exposes a `VAD Switch`, but
the active device tree is incomplete. Boot diagnostics report:

```text
rk-multicodecs vad-acodec-sound: Failed to get ADC channel
rk-multicodecs vad-acodec-sound: ASoC: Property 'rockchip,audio-routing' does not exist or its length is not even
rk-multicodecs vad-acodec-sound: Audio routing invalid/unspecified
```

Hardware VAD must therefore remain disabled in the product design until the
device tree is repaired and suspend/wake behavior is tested. Remote wake-word
detection in Home Assistant is the safer first option.

## Assist implementation path

1. Open `hw:0,0` for S16_LE, 16 kHz, stereo capture.
2. Extract channel 1 into 20 ms mono frames and stream them only while the user
   holds push-to-talk.
3. Stop capture streaming before opening the TTS playback path.
4. Play decoded TTS through `hw:0,0` and resume capture after drain completes.
5. Add remote wake-word detection only after push-to-talk is stable.
6. Benchmark a small maintained DSP such as SpeexDSP before enabling
   full-duplex conversations. Feed it the exact playback PCM reference from an
   in-process ring buffer.

Do not use `wyoming-satellite` as the embedded runtime. It is archived and the
complete Linux Voice Assistant image is too large for this panel. Implement a
small native client for the current ESPHome Voice Assistant protocol instead.

## Sendspin implementation path

The verified speaker path already accepts Sendspin's initial PCM target of
S16_LE, 48 kHz, stereo. The player should use ALSA timestamps and delay/status
queries to report when frames actually reached the device. The RK3308 PCM
clock compensation controls are promising for long-running synchronization,
but they require measured drift and bounded adjustments; they are not a
replacement for Sendspin's timing protocol.

ALSA Loopback is not required for normal Sendspin playback. It becomes useful
only as an AEC reference if Assist is allowed to listen while Sendspin or TTS
audio is playing.

## Reuse matrix

| Component | Reuse | Reason |
| --- | --- | --- |
| RK3308 ALSA capture/playback | Yes | Stable kernel interface and verified operation |
| OEM mixer values and HPF setup | Yes | Hardware calibration, no runtime dependency |
| ALSA Loopback | Yes | Verified reference transport for later AEC work |
| PCM clock compensation | Evaluate | Potential Sendspin drift correction; needs calibration |
| GStreamer runtime libraries | Optional | Present and functional, but direct ALSA is smaller |
| Hardware VAD | Not yet | Device-tree errors make it unreliable |
| Tuya AVS feed functions | No | Internal stream sinks, not DSP APIs |
| `voice_control` and Alexa SDK | No | Closed, oversized, account-specific stack |
| Pryon Lite and Alexa models | No | Proprietary and Alexa-specific |
| OEM sounds and credentials | No | Licensing and security concerns |

## Verification commands

The essential non-destructive checks are:

```sh
arecord -D hw:0,0 -d 1 -t raw -f S16_LE -r 16000 -c 2 /tmp/mic.raw
aplay -D hw:0,0 /path/to/test-48k-stereo.wav
arecord -D hw:7,1,0 -d 2 -t wav -f S16_LE -r 48000 -c 2 /tmp/loop.wav
aplay -D hw:7,0,0 /path/to/test-48k-stereo.wav
```

Never run the stock AVS client merely to test audio. It starts obsolete cloud
services, accesses persisted credentials, and does not expose reusable DSP.
