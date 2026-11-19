# Demo clips

Short clips of ArtFreq and ArtFreq FX at work, for anymeansaudio.com. Each clip is the plug-in's
own window, filmed while the plug-in plays. Nothing in a clip is mocked up, edited or composited.
The spectrogram in the Live part is ArtFreq's live view analysing the plug-in's real output, and
the sound in the MP4 is that same output. `tools/DemoReel.cpp` made all of them in one run (see
"Making them again" below).

| File | What it is | Size |
| --- | --- | --- |
| `artfreq-demo.mp4` | ArtFreq, 5.5 s with sound: 1280 x 822, 30 fps, 165 frames, H.264 (High, yuv420p, BT.709), AAC 48 kHz stereo, moov first ("faststart", so it starts playing while it loads) | 539 KB |
| `artfreq-demo.gif` | The same clip, silent, looping: 800 x 514, 15 fps (every other frame), 83 frames, 128 colours | 1.5 MB |
| `artfreq-demo-poster.png` | Its last frame (the whole drawing in the live view), 1280 x 822, for the video's `poster` | 483 KB |
| `artfreq-fx-demo.mp4` | ArtFreq FX in Carve, 5.8 s with sound: 1280 x 782, 30 fps, 174 frames, as above | 1.2 MB |
| `artfreq-fx-demo.gif` | The same, silent, looping: 800 x 489, 15 fps, 87 frames, 128 colours | 4.7 MB |
| `artfreq-fx-demo-poster.png` | Its last frame, 1280 x 782 | 1.2 MB |
| `artfreq-fx-carve-draw-demo.mp4` | ArtFreq FX in Carve + Draw, 5.8 s with sound, as above | 1.1 MB |
| `artfreq-fx-carve-draw-demo.gif` | The same, silent, looping, as above | 4.0 MB |
| `artfreq-fx-carve-draw-demo-poster.png` | Its last frame, 1280 x 782 | 1.1 MB |

On a web page: `<video src="artfreq-demo.mp4" poster="artfreq-demo-poster.png" autoplay muted loop playsinline>`
plays the MP4 like the GIF (browsers only autoplay muted video), sharper and at a quarter to a
third of the GIF's size. The GIFs are for places that take no video.

## What each clip shows

**ArtFreq** (`artfreq-demo`). The canvas for a second with the DAW stopped: the word "ArtFreq"
in the high band over a picture of a striped sun going down behind mountains. At 1.02 s the DAW
starts; at 1.2 s, as the drawing starts to sound, the window switches to the Live view, and the
drawing scrolls in from the right as it plays. At the end the whole drawing is in view.

**ArtFreq FX, Carve** (`artfreq-fx-demo`). The canvas for a second with the DAW stopped: the word
"AnyMeans" drawn. At 1.02 s the DAW starts playing a song through ArtFreq FX; at 1.1 s, as the
song comes out of the plug-in (one latency, 85 ms, later), the window switches to the Live view.
The song scrolls in, and the word, carved out of it, shows as dark letters. The song carries on
after the drawing's pass.

**ArtFreq FX, Carve + Draw** (`artfreq-fx-carve-draw-demo`). The same song and word in Carve +
Draw: the word's frequencies carved a little wider than the letters and the drawing played into
the hole, so the letters show bright with a dark edge.

## The settings

Everything not listed is the plug-in's default. Both plug-ins are activated (a test licence on a
fake licence server, so the licence screen never shows and nothing reaches the real server). The
window is at 100 % (1120 x 720 for ArtFreq, 720 x 440 for ArtFreq FX), drawn at 2x as on a Retina
display and scaled down to 1280 px with Lanczos.

| | ArtFreq | ArtFreq FX, Carve | ArtFreq FX, Carve + Draw |
| --- | --- | --- | --- |
| Drawing | "ArtFreq" (text, Inter Bold, solid) 3.5 - 16 kHz, 4 - 96 % of the length; the picture `sunset.png` (made by the tool: opaque shapes on a transparent ground, Auto mode) 70 Hz - 2.8 kHz, 20 - 80 % | "AnyMeans" (text, Inter Bold, solid) 300 Hz - 12 kHz, 8 - 92 % | as Carve |
| Mode, Depth | | Carve, -80 dB | Carve + Draw, -80 dB |
| Density, Contrast, Level, Lows | 512 lines (the default is 256), 40 dB, -6 dB, Smooth | (not used in Carve) | 256 lines, 40 dB, -6 dB, Smooth |
| Length, Trigger, Start | 2 bars, Transport, On play (the drawing plays once from where the DAW starts) | as ArtFreq | as ArtFreq |
| Tempo | Host: 120 BPM, 4/4 | as ArtFreq | as ArtFreq |
| Analyser | Log, 20 Hz - 20 kHz, slope 0 dB/oct | as ArtFreq | as ArtFreq |

The DAW is the tool itself: 48 kHz, blocks of 400 samples, its transport stopped and then playing
from bar 1. The live view shows 5 s (one 4 s pass of the drawing and a quarter more), analysed
as it always is: 4096 points, Blackman-Harris, a column every 512 samples, colours from -89 dB
(black) to -29 dB (white) per analyser cell.

The song in the ArtFreq FX clips is made by the tool (it is not a real track): a loop at 120 BPM
in A minor (Am, F, C, G, two beats each), four-on-the-floor kick, a clap on 2 and 4, hats on every
sixteenth, a side-chained saw bass, a detuned saw pad, a plucked arpeggio, a room reverb, a quiet
bed of air and its highs lifted as a master's are. It peaks at -3 dBFS, about -16 LUFS. The
sound of the clips: ArtFreq -17.8 LUFS (peak -7.5 dBFS), ArtFreq FX in Carve -16.8 LUFS, in Carve
+ Draw -15.2 LUFS (peaks -3.0 dBFS).

## How they were checked

The tool fails if any of these does not hold, and printed, for this run:

- **The picture is the sound.** In every frame of the Live part, the newest column of the live
  view (its right edge, "now") was compared with an analysis of the clip's sound made separately
  by the tool, up to the end of that frame's audio: they match to 0.5 (ArtFreq) and 0.1 (ArtFreq
  FX) colour steps of 255 on average; the picture moved a frame earlier or later differs by 25 to
  33. So the drawing shows in the live view in the frame in which it sounds in the MP4.
- **Nothing dropped or doubled.** The whole live picture in the last frame (403 and 440 analyser
  columns) matches the separate analysis column for column (0.6 and 0.1 steps); moved by one
  column it differs by 12 to 17. The view took every column the plug-in analysed, once, in each
  frame, and the analyser had nothing left over.
- **The editor changes nothing in the sound.** Each clip was also played without the window; the
  filmed run's output is the same, sample for sample.
- **Nothing that sounds is missed.** The window switches to the Live view at the start of the
  frame in which the output first sounds; everything before is silence (below -200 dBFS). The
  DAW starts at the block that brings that sound to the start of a frame, so the live view's
  first frame already shows it and never says "Nothing is playing".
- **The files.** ffprobe: every frame in the MP4 (165, 174, 174), picture and sound each exactly
  the clip's length (5.5 s, 5.8 s, 5.8 s), H.264 yuv420p and AAC 48 kHz, every other frame in the
  GIF (83, 87, 87), each GIF under 6 MB. Frames from the MP4s and GIFs were also looked at.

## What could mislead

- **The canvas is not a measurement.** The first second of each clip is the canvas, where the
  drawing is made. ArtFreq's canvas shows what an analyser should show; ArtFreq FX's canvas draws
  a stand-in song texture under the word (the plug-in cannot know the song while drawing). Only
  the Live part analyses what really comes out. In ArtFreq FX the stand-in looks smoother and
  more even than the real song that follows it, and in Carve + Draw it shows the letters pink, the
  Live view white-hot.
- **The lows.** In the ArtFreq clip the picture's foot (70 - 150 Hz) is crisp on the canvas but
  soft and streaky in the Live view, with faint purple streaks reaching below it to 20 Hz. That
  is mostly how an analyser of 4096 points draws those octaves: its cells are 11.7 Hz wide (half
  an octave at 25 Hz), so it spreads the 70 Hz edge downwards. The sound itself has little there:
  below 45 Hz its power is 63 dB under the picture's, and an analyser of 32768 points reads the
  streaks 7 to 12 dB lower, at the bottom of the colours. MiniMeters with a larger FFT shows the
  lows sharper (and the edges in time softer); with other colour ranges, scales or slopes any
  analyser looks different from ArtFreq's live view. The Live part shows the lows as they are at
  4096 points, not at their best.
- **The song is synthetic**, made to have sound everywhere from the kick to 16 kHz. A real song
  shows the carved letters only where it has sound: little in the top octave of a dark mix, none
  over a silence. Its lows (kick and bass) are white in the live view, as a loud master's are.
- **Density 512** in the ArtFreq clip, twice the default, for solid letters; the footer shows it.
  At 256 the letters show more of their scan lines.
- **The sound of the ArtFreq clip is the drawing itself**: a dense, tonal, noisy sound, not music.
  In Carve + Draw the drawing's tones are heard over the song.
- **Made offline.** The clips play at real speed (a second of video is a second of the plug-in's
  output), but they were rendered frame by frame, not captured from a DAW: no DAW window, no mouse
  pointer, and the switches to the Live view happen without a click. On a computer the live view
  scrolls at 60 Hz; the MP4 shows 30 frames a second and the GIF 15.
- **Latency.** ArtFreq FX's sound in its clips is its output as it comes out, 85 ms after the
  song went in (a DAW makes up for that with the other tracks).
- The GIFs have 128 colours with ordered dithering; the MP4s are 4:2:0 (fine coloured detail is
  a little softer than in the plug-in).

## Making them again

```sh
cmake --build build --target ArtFreqDemoReel        # not built by default; needs ffmpeg and ffprobe
xvfb-run -a -s "-screen 0 2400x1600x24" build/ArtFreqDemoReel_artefacts/Release/ArtFreqDemoReel docs/marketing --work /tmp/reel
```

(On macOS, run it as it is, without `xvfb-run`; it has only been run on Linux so far.) It takes
about a minute and, with the same ffmpeg, gives the same files every time. The work folder keeps
every frame at 1280 px (`frames-<clip>/`), each clip's sound as a WAV and the song going into
ArtFreq FX. `--clips artfreq,fx,fx-carve-draw` picks clips; the drawings,
settings and the song are in `tools/DemoReel.cpp` (`makeDrawing`, `makeProcessor`, `makeSong`).
