# earview — "ears via eyes" for the sound-design loop

Renders a WAV as things an image/vision model can actually read, so a track
can be *inspected* (and compared to a reference) rather than guessed at:

1. a **log-frequency spectrogram PNG** (view it): low freq at bottom, blue
   verticals = beats. Kicks = low-freq thumps, hats = high ticks, bass chug =
   mid blocks, sidechain = dark "pumping" gaps after each kick.
2. a **beat-aligned per-band onset grid** (read it): KICK / BASS / SNARE / MID /
   HAT onsets quantized to 1/16 steps over N bars. Rough on very dense/mastered
   material (onset detection saturates) — the spectrogram is the reliable eye.

## Usage
    python3 tools/earview/earview.py <wav> <bpm> <out.png> [start_s|auto] [bars]

Example loop:
    rtg_cli --out /tmp/t.wav --seconds 40 --seed 42 --genre riddim --aggression 85 --bpm 145 --library /tmp/lib
    python3 tools/earview/earview.py /tmp/t.wav 145 /tmp/t.png auto 4
Render a reference the same way and compare the two spectrograms side by side,
then edit the engine and re-render (generate -> see -> compare -> edit).

Deps: numpy, scipy (stdlib zlib/struct for the PNG writer). No matplotlib/PIL.
