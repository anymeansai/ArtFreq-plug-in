# Are the screenshots accurate?

## In plain words (since the measured preview)

**Yes, for screenshots taken with this version, once the canvas says so.** The canvas no longer
shows an ideal picture of the drawing: a moment after each change it renders one pass of the
plug-in's real output (the same sound as playback and Bounce WAV) and shows it as a spectrogram
analyser with your settings shows it (a Blackman-Harris window, the FFT size and level range
from Settings). So the blur and thickening below about 250 Hz and the scan lines in the highs at
the default Density are on the canvas, because they are in the sound and in every analyser. (The
haze and the streaks under low shapes, which this page's audit found in the sound of 0.5 and
0.6, are gone from the sound: see [the lows](#the-lows-since-the-standing-comb) below.) The Live view uses the same analysis and colours, so the same sound looks
the same in both.

What it does not claim:

- **The analyser's settings.** FFT 8192 and -90 to 0 dBFS are ArtFreq's defaults and its own
  assumptions, not MiniMeters' known defaults (MiniMeters was not available here and we found
  none published). The canvas is as right as these settings are: set them to the user's analyser.
- **The analyser's window.** The canvas and the Live view analyse with a Blackman-Harris window,
  which shows a clean tone as a clean line. Many analysers use Hann, which draws the lows a
  little thinner and adds a faint skirt under every low tone, down to the bottom of the picture
  where the display's range is deep (60 dB under a tone it is still seven bins wide). That skirt
  is the analyser's, not the sound's, and the canvas does not show it.
- **The colours.** The canvas shows the same *levels* as the analyser, in ArtFreq's own palette
  over the level range from Settings. The analyser's colour map is its own (and MiniMeters has
  settings such as a Brightness Boost that this does not model): the shapes and their relative
  brightness are what matches, not the hues.
- **ArtFreq FX before a pass has played.** ArtFreq FX's canvas carves **the user's own track as
  it came in during the last pass** (the status line says "Real output, on your track's last
  pass"). Until a pass has played with the window open, it carves a stand-in for a loud master
  ("Real output, on a stand-in song"), which can look very different from the user's track (on a
  bass track the stand-in shows carved words in the mids and highs where the track has nothing to
  carve).
- **While you drag, paint or type, or turn a setting that changes the sound,** the canvas shows a
  quick estimate (the status line says "Updating…"): the last real picture wherever the drawing
  did not change near since, a model of the analyser where it did. The model is close in the mids
  and highs; in the lows it can still show a shape a little cleaner than it will be (numbers
  below). Screenshots should be taken once the line says "Preview of the real sound" or "Real
  output, …" (the snapshot tool does).

- **ArtFreq:** the canvas is its real output, analysed. Checked against the real processor's
  output analysed separately (the truth lab, below): correlation 0.95 - 1.00 in every band and at
  FFT 4096, 8192 and 16384, mean difference within 0.3 dB, and the drawing stands out no more on
  the canvas than in reality (excess contrast -0.6 to +0.1 dB). The old canvas: correlation as
  low as 0.40 in the lows and a drawing that stood out 29 - 88 dB more than in reality.
- **ArtFreq FX, once a pass has played:** the canvas is the real carve of the song that played.
  On the dense test master: correlation 0.96 - 1.00 by band, mode and FFT size, mean within
  0.25 dB, excess contrast within 2 dB (1.1 at 8192). On songs the stand-in was never tuned to (`--scenarios`): a bass track
  (a kick and a sub, nothing above 200 Hz), the stand-in 20 dB quieter and a bass-heavy master,
  correlation 0.92 - 1.00 (the bass track's empty highs: 0.92 between two floors), mean within
  0.2 dB, excess within 0.4 dB. Carving the stand-in instead, as before (and as before any pass):
  on the bass track +56 dB more contrast in the low mids and +44 in the highs than reality, on the
  quiet mix +20, on the bass-heavy one +9.
- **The screenshots in `docs/screenshots` and the website's are of the old canvas**: re-shoot them
  with this version (`ArtFreqSnapshot`), and they are accurate for the plug-in's output at the
  default analyser settings (FFT 8192, -90 to 0 dBFS, log 20 Hz - 20 kHz). ArtFreq FX's
  screenshots carve the stand-in (the snapshot tool plays no song) except
  `artfreq-fx-carve-your-track.png`, which carves a bass track that played: caption the others
  "on a stand-in song". Until then the guidance below for the old screenshots stands.

The rest of this page first gives the verdict on the lows since the standing comb, then the
measurements of the measured canvas (made with 0.6: Hann, -100 to 0 dBFS, the gliding comb),
then the audit of the old canvas, which led to it.

## The lows since the standing comb

The audit below measured, in the real output of 0.5: under bright low shapes a haze about 40 dB
down reaching to 20 Hz, vertical streaks hanging under thin low shapes and under edges, and a
dot at 40 - 65 Hz coming out as an upright streak from 23 to 81 Hz. The customer saw the same in
a DAW: "a 'drip' from the image or text that drips into the lower frequencies, even if the canvas
doesn't have anything drawn down there". **It was in the sound, and it no longer is.** (Why, and
what replaced the gliding comb: docs/DESIGN.md, "Smooth lows".)

Measured with `ArtFreqLab <dir> --drip` (tools/DripLab.h): eight drawings at Density 256, the
real engine, looped; a 65536-point Blackman-Harris analysis, whose own side lobes are 92 dB down;
the share of the sound's energy more than a sixth of an octave below the lowest frequency drawn
under each analysis window (dB; in brackets, the loudest cell there against the loudest cell of
the drawing):

| drawing | Smooth, 0.6 | Smooth, now | Stripes, 0.6 | Stripes, now |
| --- | --- | --- | --- | --- |
| one row at 100 Hz (a single tone) | -30.7 (-30.5) | -50.1 (-42.3) | -30.6 (-30.4) | -49.8 (-42.2) |
| block 60 - 250 Hz | -32.8 (-19.0) | -77.3 (-64.2) | -42.6 (-33.4) | -75.7 (-70.1) |
| LOW in the Low band | -24.2 (-12.7) | -57.6 (-39.9) | -26.7 (-20.9) | -40.5 (-28.6) |
| thin wavy stroke 130 - 350 Hz | -28.5 (-27.6) | -45.6 (-43.3) | -30.0 (-29.9) | -46.0 (-35.4) |
| dot 40 - 65 Hz | -15.6 (-18.7) | -67.0 (-63.0) | -22.8 (-25.9) | -56.9 (-59.0) |
| frame, bottom bar 80 - 90 Hz | -40.9 (-28.7) | -69.6 (-60.8) | -9.9 (0.0) | -71.8 (-64.0) |
| MID in the Mid band | -52.0 (-36.4) | -66.3 (-55.2) | -49.7 (-39.0) | -66.1 (-58.4) |
| portrait 40 Hz - 2 kHz | -43.8 (-33.5) | -74.3 (-60.6) | -39.0 (-30.9) | -73.7 (-64.2) |

Verdict for the lows:

- **Filled shapes, pictures, mid text, a dot: clean.** What lies below the drawing is 66 to 77
  dB under the sound's energy (the target was 60); nothing is played below the lowest drawn
  frequency.
  The dot is a dot (35 - 90 Hz on an analyser of 8192 points, which cannot show it smaller).
- **Low text: 58 dB, not 60.** The letters' lowest lines, at 40 - 50 Hz, fade in and out over a
  quarter of a second at most, and a tone that short is a few Hz wide whatever plays it: some of
  its energy lies just past the sixth-of-an-octave mark. No haze, no streaks.
- **Thin strokes that move in pitch: 46 dB.** A stroke that climbs or falls is played by one
  fixed line after another, and faint arcs run off it where it bends (the brightest 43 dB under
  the stroke). 0.6 played such a stroke as a staircase with a streak to the bottom under every
  step (28 dB). Visible on a display whose range is deeper than about 60 dB.
- **A single tone: 50 dB**, all of it within a sixth of its frequency: the width of a tone that
  starts and stops over 12 cycles.
- **Stripes.** The same fades and the same rule (no line below the drawing), so it is as clean
  as Smooth where its lines stand inside what is drawn. Where a stroke lies between two of its
  lines, 25 Hz apart, it is played by one of them, the one that has more of it: that is Stripes
  (the frame's bar at 80 - 90 Hz is played at 97 Hz; 0.6 played it at 72 and 97).
- **The fill is kept**: ArtFreqLab's table in docs/DESIGN.md (fill .93/.93 at 4096 points,
  .93/.94 at 8192, .91/.93 at 16384, .79/.90 at 32768, low / mid; the gliding comb: .93/.92,
  .93/.93, .88/.91, .81/.90).
- **What an analyser adds.** With a Hann window and a range deeper than about 70 dB, an analyser
  still paints its own skirt under low shapes (side-by-side renders: `ArtFreqLab --drip`,
  `*-hann8192.png` against `*-bh8192.png`). That is why the canvas and the Live view no longer
  use Hann.

### A second opinion

Before launch the lows were checked again with other drawings, by someone who had not built
them (`ArtFreqLab <dir> --skeptic`, tools/SkepticLab.h: the real engine's output written as raw
floats, analysed outside the plug-in's code). Density 256, a 4-bar pass (8 s), looped, unless
said; the share of the sound's energy more than a sixth of an octave below the lowest frequency
drawn under each window of a 16384-point Blackman-Harris analysis (a third of a second, so each
letter and each bend of a stroke is held to its own lowest edge), in dB, and in brackets the
loudest cell there against the loudest cell of the drawing:

| drawing (Smooth) | 0.6 | now |
| --- | --- | --- |
| the default text, ArtFreq, moved to the Low band | -30.0 (-17.9) | -63.9 (-43.6) |
| ... at Density 64 / 1024 | -30.5 / -31.0 | -64.0 / -63.9 |
| ... over 1 bar / 16 bars | -23.0 / -32.9 | -62.0 / -59.7 |
| ... as a single pass (Repeat off) | -31.4 (-16.0) | -63.9 (-43.6) |
| small text, 60 - 200 Hz | -23.8 (-15.0) | -59.1 (-53.2) |
| large text, 60 - 200 Hz | -29.3 (-15.5) | -67.8 (-56.2) |
| two soft blobs, 40 Hz - 2 kHz (every level, no hard edge) | -67.4 (-59.3) | -94.3 (-84.9) |
| a filled blob with a soft rim, 80 - 300 Hz | -42.7 (-27.1) | -79.1 (-64.6) |
| a portrait over the whole height | -53.6 (-32.7) | -91.3 (-66.3) |
| blocks touching both ends of the pass, looped / once | -39.3 / -40.1 | -78.0 / -76.0 |
| a thin stroke rising from 50 to 800 Hz | -24.5 (-12.0) | -44.2 (-41.5) |
| thin level strokes at 62.5, 100 and 137.5 Hz | -28.8 (-19.4) | -46.6 (-30.8) |
| linear scale, Density 1024: text over 200 Hz - 3 kHz | -60.1 (-35.0) | -70.8 (-50.4) |

What it confirmed: the haze and the streaks are gone from the sound, at every Density and
Length, looped and once, and at the wrap of a loop; Smooth kept its fill and is steadier than it
was (a block over 30 Hz - 6 kHz: the level of every analyser row, averaged over half a second,
moves by 1.2 dB at most at 4096 to 16384 points, where the gliding comb moved by up to 9 dB at
16384; no beat where the comb's two grids meet at 500 Hz, none where the comb ends); the highs
play as they did (text in the High band: its bright cells within 0.1 to 0.6 dB of 0.6 at 2048 to
8192 points); the loudest moments are no louder (peaks at Level 0 dB, 0.6 -> now: the low text
-3.8 -> -2.7 dBFS, the soft blobs -2.7 -> -6.4, the portrait -0.8 -> -3.1; a block over the whole
height and dense text reach the safety clipper at Level 0 dB, as they did: +0.8 -> +0.4 dBFS
going into it; at the default Level, -6 dB, nothing does); the canvas and the Live view at their defaults show the wave, the dot and the frame
of the snapshot states with nothing under them, and agree.

What it found, and what was done:

- **Thin level strokes between two of the comb's lines beat.** A stroke that touched neither
  line's own row and lay half in one line's span and half in the other's was played by both,
  5 Hz apart, alone: a row of beads on an analyser, a tremolo to the ear. One in six of all
  strokes 2 to 6 rows thick between 40 and 300 Hz. 0.6 did not do this (it played thin strokes
  on separate lines of their own). Now such a stroke goes to the line that has more
  of it: no stroke at any height from 40 Hz to 1.7 kHz, 1 to 24 rows thick, is played by two
  lines, and none is lost (`strokes.txt`). In Stripes the same rule keeps a thin stroke on one
  of its lines.
- **Long passes kept streaks at the edges of low shapes.** At 16 bars a canvas column lasts 31
  ms, and fades of two or three columns are too coarse to do their work. Low lines now fade
  over four columns at least: the low text at 16 bars reads -59.7 dB below it (-57.6 before
  this, -32.9 in 0.6) and -63.0 above (-57.5).

What is still not perfect, and will stay so:

- **Low shapes start and end softly.** A low line comes and goes over 12 cycles of its own sine,
  half of that before the drawn edge and half after: at 60 Hz a tenth of a second either side.
  Anything faster is a click, which is what the streaks were. Small text in the lows therefore
  reads as a textured band with the tall letters showing, not as letters (it did not read in 0.6
  either, under its haze): an analyser needs about as long to tell 60 Hz from 70.
- **A thin stroke that moves quickly in pitch** has faint wisps beside it, the brightest 42 dB
  under the stroke (12 dB in 0.6): each fixed line it crosses sounds for a moment, and the
  engine moves loudness in straight strokes from one canvas column to the next, which leaves
  traces at multiples of 128 Hz from the line at a 4-bar length (one over a column's 7.8 ms).
  The ends of a level stroke have the same traces, 60 dB under it. Seen only on displays that
  reach that far down.
- **Thin level strokes play on the comb's grid**: a stroke drawn at 62.5 Hz sounds at 60 or 65.
- **At 32768 points** an analyser begins to tell the comb's lines apart below 500 Hz (5 Hz
  apart): fill 0.79 in the lows where the gliding comb had 0.81, with half the flicker.
- **Pressing play or stop in the middle of a low shape** cuts it within 5 ms, which is a click:
  one upright streak at that moment, as in every version.
- **A Hann analyser with a range of 80 dB or more** still paints its own skirt under low shapes
  (thin streaks from the shape's lower edge towards 20 Hz at -100 dBFS): it is in the analyser,
  not in the sound (the same sound through a Blackman-Harris window shows none).

The numbers in the next two sections were measured with 0.6 (Hann, -100 to 0 dBFS, the gliding
comb). Run again on this build (`ArtFreqTruthLab`, now with the canvas's window; FFT 8192, the
canvas against the real processor's output analysed separately): ArtFreq's default, rich, layers
and linear states, correlation 0.99 - 1.00 in every band, mean difference within 0.1 dB, excess
contrast within 0.3 dB; ArtFreq FX once a pass has played, 0.97 - 1.00, within 0.2 dB and 0.2 dB.
The canvas is still the real output, analysed.


## The new canvas against the real output

`ArtFreqTruthLab <after> <out> --before <before>` (see Reproduce): for each audited state, the old
screenshot's canvas, the new canvas (rendered exactly as the editor renders it, at each FFT size)
and the real output (the activated processor, played as a DAW plays it, ArtFreq FX on the dense
test master, analysed by the lab's own code), per band: lows below 250 Hz, low mids 250 Hz -
2 kHz, highs above 2 kHz. All levels as the analyser's colour key reads them (-100 to 0 dBFS,
clipped to it). **r**: correlation of canvas and real over the band; **mean**: canvas - real (dB);
**excess**: how much more the canvas makes of the drawing than reality (its contrast between
drawn cells and the cells around them, against the real contrast; > 0: the canvas looks better
than it is). ArtFreq FX's new canvas is the one shown once a pass of the song has played; in
brackets, before any pass (the stand-in, against the master: two different songs). At FFT 8192
(the default), old -> new [before a pass]:

| State | Band | r | mean dB | excess dB |
| --- | --- | --- | --- | --- |
| default | low mids | 0.85 -> 0.99 | 3.2 -> -0.1 | 36.9 -> 0.0 |
| default | highs | 0.84 -> 1.00 | 1.5 -> 0.0 | 57.5 -> 0.0 |
| rich | lows | 0.62 -> 0.96 | -10.4 -> 0.1 | 45.0 -> -0.5 |
| rich | low mids | 0.85 -> 0.99 | 2.6 -> 0.0 | 32.4 -> 0.0 |
| rich | highs | 0.76 -> 1.00 | 4.7 -> 0.0 | 48.4 -> -0.1 |
| layers | lows | 0.56 -> 0.96 | -15.1 -> 0.2 | 61.4 -> -0.3 |
| layers | low mids | 0.84 -> 0.99 | 2.1 -> 0.0 | 32.6 -> 0.0 |
| layers | highs | 0.73 -> 1.00 | 7.5 -> 0.0 | 54.0 -> 0.0 |
| linear | lows | 0.64 -> 1.00 | 0.2 -> 0.0 | 71.4 -> 0.1 |
| linear | low mids | 0.56 -> 1.00 | -1.5 -> 0.0 | 38.0 -> 0.0 |
| linear | highs | 0.63 -> 1.00 | -2.4 -> 0.0 | 46.3 -> 0.0 |
| fx-default | low mids | 0.82 -> 0.99 [0.92] | -7.0 -> 0.2 [1.6] | -4.6 -> 0.0 [2.5] |
| fx-default | highs | 0.61 -> 1.00 [0.66] | -3.2 -> 0.0 [1.1] | -1.7 -> 0.0 [2.6] |
| fx-carve | lows | 0.52 -> 1.00 [0.90] | -8.3 -> 0.0 [2.0] | 34.3 -> 0.0 [1.4] |
| fx-carve | low mids | 0.78 -> 1.00 [0.89] | -7.1 -> 0.0 [1.4] | -0.6 -> 0.0 [-0.3] |
| fx-carve | highs | 0.77 -> 1.00 [0.85] | -4.2 -> 0.0 [1.0] | 4.1 -> 1.1 [0.5] |
| fx-carve-draw | lows | 0.33 -> 0.98 [0.89] | -4.9 -> 0.1 [1.6] | 6.6 -> 0.0 [2.1] |
| fx-carve-draw | low mids | 0.32 -> 0.97 [0.65] | -9.6 -> 0.0 [0.8] | 3.7 -> 0.0 [-1.3] |
| fx-carve-draw | highs | 0.32 -> 1.00 [0.76] | -5.8 -> 0.0 [0.7] | -4.0 -> 0.1 [-2.5] |
| fx-stencil | lows | 0.40 -> 0.99 [0.98] | -15.1 -> 0.0 [0.2] | -9.1 -> -0.2 [4.0] |
| fx-stencil | low mids | 0.68 -> 1.00 [0.96] | -6.8 -> 0.1 [0.6] | -18.6 -> 0.1 [1.5] |
| fx-stencil | highs | 0.82 -> 0.99 [0.97] | -2.0 -> -0.1 [0.4] | -0.9 -> 0.0 [2.6] |

(The default drawings have nothing in the lows.) At FFT 4096 and 16384 the same holds: ArtFreq r
0.95 - 1.00, |mean| within 0.3 dB, excess -0.6 to 0.1 dB; ArtFreq FX after a pass r 0.96 - 1.00,
|mean| within 0.25 dB, excess -0.5 to +2.0 dB (fx-default's highs at 16384). The editor's screenshot and the direct render agree
to a median 0.0 - 0.1 dB.

**ArtFreq FX on other songs** (`ArtFreqTruthLab --scenarios`, the drawing BASS / MIDS / HIGH,
Carve at -80 dB, FFT 8192; each song played through the processor, which kept it; the canvas as
the editor renders it then): canvas against the real output, per band lows / low mids / highs:

| Song | r | mean dB | excess dB | the stand-in instead (excess) |
| --- | --- | --- | --- | --- |
| the stand-in itself | 0.995 / 0.996 / 0.997 | 0.1 / 0.0 / -0.1 | -0.2 / 0.0 / 0.0 | -0.3 / 0.0 / 0.0 |
| a bass-heavy master | 0.996 / 0.996 / 0.998 | 0.1 / 0.0 / -0.1 | -0.2 / 0.0 / 0.0 | -2.7 / 8.6 / 9.2 |
| a bass track (nothing above 200 Hz) | 0.996 / 0.990 / 0.921 | 0.1 / -0.2 / 0.0 | -0.4 / 0.0 / 0.0 | 3.2 / 56.2 / 44.4 |
| the stand-in 20 dB quieter | 0.997 / 0.996 / 0.998 | 0.1 / 0.0 / 0.0 | -0.2 / 0.0 / 0.0 | -0.3 / 20.0 / 20.0 |
| the same, Draw (sines on top) | 0.907 / 0.985 / 0.999 | 0.0 / 0.0 / 0.0 | 0.0 / 0.0 / 0.0 | -8.9 / -18.7 / -19.9 |

**Verdict: no band where the new canvas looks clearly better than reality**: ArtFreq none (at
most 0.1 dB more contrast); ArtFreq FX, once a pass has played, at most 2 dB (1.1 at 8192), on
every song tried. Before a pass, ArtFreq FX's canvas is a stand-in's and can promise far more than a given
track delivers (above); it says so under the canvas.

### The quick estimate (shown while the drawing or the sound changes)

Where the drawing did not change near since the last measurement, it *is* that measurement (a
word moved in a dense project, against the new measurement: lows r 0.93 -> 0.97, mean |dB| 5.3 ->
1.9; ArtFreq FX's Carve 0.91 -> 0.97, 5.1 -> 1.7). Where it changed, and before the first
measurement, a model; its numbers alone (truth lab, FFT 8192):

- **ArtFreq FX** (it models the carver's bins, halo and band means and the analyser's lobe over
  the song): r 0.90 - 0.98 by band and mode; in the lows mean within 1.7 dB, excess -0.9 to
  +1.5 dB in the audited states (8 - 11 dB on the scenarios' low text, where the real carve is a
  little less deep); elsewhere mean within 2.2 dB, and where it differs most it shows thin carves
  fainter than they are (fx-carve's highs, fx-default's low mids: down to -23 dB, it promises
  less).
- **ArtFreq**: mids and highs r 0.88 - 0.98, mean within 3.4 dB, excess within 9 dB (the highs'
  scan lines at Density 384 a little sharper than measured). The haze under shapes is drawn at
  its measured level and in streaks (before, an even block 45 dB under the shape down to 20 Hz:
  in the scenarios 13.6 dB brighter than real on average under the default drawing; now -0.3).
  **The lows remain the weak spot**: low shapes are drawn blurred, with the comb's rising stripes
  and skirt, but still stand out 6 - 17 dB more than in the real sound at FFT 4096 and 8192
  (at 8192: rich 9.7, layers 10.4, the scenarios 6 - 14), up to 28 dB with Lows on Stripes, and on a linear
  scale's few low rows 23 dB at 8192 and 35 dB at 16384 (it leaves out a haze the sound has
  there). Before this version: 10 - 33 dB with Smooth lows. It is replaced by the measured
  picture as soon as the drawing stays still, and the measured picture (above) is exact.

Side by sides (old canvas | new canvas | real output at 8192, the other FFT sizes under them, and
for ArtFreq FX the master going in and the canvas before any pass; the numbers at the foot) come
out of the lab as `truth-<state>.png`; `--scenarios` writes `scenario-<name>.png` (measured | quick
estimate | real, and their differences).

## The old canvas (0.5): the audit

### What to put on the website (for the old screenshots)

1. **Lead with real measurements**, captioned as such: `docs/marketing/artfreq-real-output.png`,
   `artfreq-real-output-text.png` and `artfreq-fx-real-output.png` (real analyses of the plug-ins'
   audio), and the demo clips (`docs/marketing/*-demo.mp4` and `.gif`, made with the demo reel
   tool: the plug-ins' own window filmed while they play, with their Live view of the real output).
2. **Safe with a caption:** `artfreq-default.png` and `artfreq-fx-default.png`. Any interface
   screenshot is fine *to show the interface*, with the caption.
3. **Do not present as "what you will see":** artfreq-layers, artfreq-fx-carve,
   artfreq-fx-carve-draw, artfreq-linear, and artfreq-rich / -on-play (their low parts).
4. **Test content:** every screenshot with a drawing except `artfreq-default.png` and
   `artfreq-fx-default.png` shows the test drawing: the word HELLO and a test picture named
   `logo.png` (in the layer list too). That includes `artfreq-live.png`, which is otherwise a real
   analysis. Fine in the documentation; on the website it looks like a placeholder, so better
   re-shot with a real drawing (see Fixes) or replaced by a demo clip's poster frame.

Captions, ready to paste:

- Under an ArtFreq canvas screenshot: *"The canvas previews your spectrogram. Text and shapes in the
  mids and highs come out as drawn; below about 150 Hz every analyser blurs detail, so keep low art
  big and simple. Colours depend on your analyser's settings."*
- Under an ArtFreq FX canvas screenshot: *"The canvas previews the carve over a stand-in texture,
  not your song; the Live view shows the real result. Carving reads best in the mids and highs: in
  the bass, a song's kick and bass fill small shapes back in."*
- Under a real-output image: *"Real measurement of ArtFreq's output, not a mock-up."* For the FX
  image add *"on a synthetic test song"*.

The rest of this page is the detail behind these points.

### How it was checked

For every screenshot with a drawing, the exact drawing and settings were saved from the snapshot
tool and played, one pass, through the real plug-in (activated, as a buyer's copy): ArtFreq on its
own, ArtFreq FX on a dense synthetic master (drums, bass, chords, a voice, cymbals and a noise bed
at -10 dBFS RMS). The output was analysed as an FFT spectrogram such as MiniMeters' works: Hann
window, FFT 4096, 8192 and 16384 points, log scale 20 Hz - 20 kHz, a column every 5 - 7 ms, coloured
with the canvas's colour map over -100 to 0 dBFS, at 48 kHz. (MiniMeters itself was not available
here and its own defaults could not be looked up: its dB range, window and colour map are
assumptions. The shapes do not depend on them; the colours do. "Analysed like MiniMeters" in the
figures means this.)

Each comparison puts the screenshot's canvas beside the real output, at the same size and on the
same frequency axis (`docs/accuracy/compare-*.png`; the full sheets, with 4096 and 16384 and the
numbers, come out of `ArtFreqTruthLab`, see the end). Bands: **lows** below 250 Hz, **low mids**
250 Hz - 2 kHz, **highs** above 2 kHz.

![The lows, close up](accuracy/lows-closeup.png)

### What is accurate

- **Text and shapes from about 250 Hz up** land where they are drawn, with the right shape, at FFT
  4096 and 8192 (16384 blurs them in time, see 7). ArtFreq's default drawing ("ArtFreq", 300 Hz - 3 kHz): 97 - 98 % of the letters' cells within
  12 dB of what the canvas shows, correlation 0.86 with the canvas at FFT 8192.
- **Smooth lows fill shapes** in the low mids (the logo's disc and bar, the letters' bodies) as
  the canvas shows, with a fine shimmer texture the canvas does not draw.
- **ArtFreq FX, Carve, in the mids and highs**: dark and sharp, as on the canvas. The default
  "ArtFreq" is carved by a median of 59 dB (FFT 8192) to 65 dB (4096) below the song; 69 - 90 %
  of the letters' cells are at least 30 dB darker than the song.
- **Carve + Draw in the highs** stands out more than the canvas shows (+21 dB over the song,
  the canvas +10). **Stencil in the highs** passes the song as shown (97 % within 6 dB).
- **The Live view screenshot** (`artfreq-live.png`) is not a preview: it is a real analysis of the
  sound (FFT 4096), and it agrees with these measurements, blur in the lows included.

### What is not

1. **The lows are blurred and thicker.** An analyser's resolution is a fixed number of Hz
   (Hann, FFT 8192 at 48 kHz: about +-12 Hz; 4096: +-23 Hz; 16384: +-6 Hz). At 1 kHz that is
   nothing; at 100 Hz it is a third of an octave (8192) to two thirds (4096); at 50 Hz, two thirds
   to one and a half octaves. So, in the "rich" screenshot:
   - the soft dot at 40 - 65 Hz shows as an upright streak, about 31 - 74 Hz at FFT 8192 and
     23 - 81 Hz at 4096 (within 20 dB of its peak), with a faint tail up to about 110 Hz;
   - the logo's bottom edge, a thin bar at 80 - 90 Hz on the canvas, shows as a band about
     59 - 110 Hz at 8192 (55 - 115 Hz at 4096, 66 - 103 Hz at 16384): several times as thick;
   - the wave's lower half is softer and thicker than the crisp line on the canvas.

   A longer FFT helps less in the lows than these figures suggest: the Smooth lows are played by
   gliding sines, and a long window sees each one smeared along its glide.
2. **A faint haze under bright low strokes.** Under the logo's bottom edge the analyser shows a
   dim fill down to 20 Hz (about -79 dBFS at 8192, 40 dB under the edge: dark purple on a
   -100 dB scale, black on a -80 dB one), and faint vertical streaks hang under the wave and
   under letters below 2 kHz (about -80 to -95 dBFS for the default "ArtFreq"). The canvas shows
   black there. Unlike the blur, this is in the sound (a long FFT of the output finds it too): energy
   spilled where shapes start and stop and where the Smooth lows' gliding sines enter them. It
   stays 40 dB or more under the drawing.
3. **Text in the low band does not read.** In `artfreq-layers.png` "Bass drop" (100 - 250 Hz) is
   only just legible at 4096 and 8192, and "second line" (40 - 100 Hz) is an unreadable block at
   every FFT size. The canvas shows both as clean letters.
4. **ArtFreq FX in the lows, on a real song.** A song's kick and bass are bright lines (on the test
   master the loudest 5 % of low cells reach -12 dB at FFT 4096, 12 dB over the canvas's stand-in),
   and both the carver and the analyser have only a few bins down there, so they fill small holes
   back in. In `artfreq-fx-carve.png`:
   the black dot at 50 Hz is invisible; the logo's bottom edge (85 Hz) is invisible at 4096 and
   8192; the disc's lower part (below about 150 Hz) is not dark; the median carve below 250 Hz is
   21 - 26 dB, not the 80 dB the black on the canvas suggests (only 31 - 45 % of those cells are
   30 dB under the song). Stencil shows the same lows brighter and thicker than the canvas.
5. **Thin strokes in time vanish in FX at long FFTs.** The logo frame's left and right sides last
   about 70 ms; carved into a song, they are faint at FFT 4096 and gone at 8192 and up (the
   window is longer than the gap). The thin carved wave is a faint line, not the bold black wave
   of the canvas.
6. **The highs show fine horizontal lines at the default Density.** At Density 256 - 384 the
   sine lines above 2 kHz are far enough apart for an analyser at 8192 points to show each one,
   with dark gaps between them, on a display as sharp as the canvas (a Retina screen). The canvas
   fills them in. HELLO in the "rich" screenshot: only 50 % of its cells within 12 dB of the
   canvas at Density 384; **99 % at Density 1024**, where it is solid
   (`docs/accuracy/highs-density.png`). At 150 % and 75 % window size (1x screenshots) the canvas
   does draw these lines, and it matches.
7. **FFT 16384 smears everything in time** (a 0.34 s window): letters run into each other, carved
   letters fill in (FX highs: 26 - 59 % of cells still 30 dB dark, against 76 - 90 % at 4096). A
   short Length makes it worse: `artfreq-linear.png` (1 bar, 2 s) reads at 4096, is blurred at
   8192 and unreadable at 16384.
8. **Colours.** The canvas paints the drawing's loudest point white. ArtFreq's output, at the
   default Level, reaches about -35 to -55 dBFS in an analyser cell, which a -100 to 0 dB scale
   shows orange to pink, never white (the "canvas's colours" panel of each sheet shows the same
   sound with the canvas's mapping). ArtFreq FX's canvas stands a smooth texture in for the song;
   the test master looks brighter in the lows and darker in the mids and highs (by 13 - 21 dB)
   than that stand-in.

### Verdict per screenshot

| Screenshot | Lows (< 250 Hz) | Low mids (250 Hz - 2 kHz) | Highs (> 2 kHz) | Use for marketing? |
| --- | --- | --- | --- | --- |
| artfreq-default | nothing drawn; faint streaks below | accurate | letter tops show fine lines | yes, with the caption |
| artfreq-rich, -on-play, -playing and the other UI states of that drawing | **not accurate**: thicker, blurred, hazy; dot becomes a streak | accurate | HELLO striped, not solid | only to show the interface (test drawing: HELLO, logo.png) |
| artfreq-layers | **not accurate**: low text does not read | accurate | as rich; the sunset striped | no |
| artfreq-linear (1 bar) | small, roughly right | roughly right | blurred at 8192, unreadable at 16384 | no |
| artfreq-150 / -75 (Density 200, 1x) | as rich | accurate | lines shown, and real | only to show the interface |
| artfreq-live | real measurement | real | real | accurate, but shows the test drawing (HELLO, logo.png): a demo clip's poster is better |
| artfreq-fx-default | nothing drawn | accurate (dark, sharp) | accurate | **yes** (best FX shot) |
| artfreq-fx-carve | **not accurate**: dot and bottom edge vanish | big shapes yes; thin wave and frame sides much fainter | accurate at 4096 / 8192 | no |
| artfreq-fx-carve-draw | **not accurate**: dot becomes a dark gap | roughly | accurate, even stronger | not for the lows |
| artfreq-fx-stencil | brighter and thicker than shown | accurate, more colourful | accurate | with the caption (see the note below) |

Note on `compare-fx-stencil.png`: the bright full-height lines at its left and right edges are in
the sound. After the pass, Stencil lets the song through again, as designed. At the pass's start,
which is also where the test song starts, the first 5 ms of the song come through before the
stencil closes (a peak of -4 dBFS fading out over 256 samples): a short tick, which an analyser
draws as a line from top to bottom. Not changed here (it is the plug-in's behaviour); worth a
look by the developer.

### Fixes

#### (a) Marketing: what to show and say

- What to show and the captions: see "What to put on the website" at the top.
- The real-output images: `docs/marketing/artfreq-real-output.png` (ArtFreq's default drawing),
  `artfreq-real-output-text.png` (ARTFREQ over ANYMEANS, Density 1024) and
  `artfreq-fx-real-output.png` (ArtFreq FX carving its default drawing into a dense synthetic
  master). Each is the plug-in's real audio analysed (Hann, FFT 8192, log 20 Hz - 20 kHz, -100 to
  0 dBFS), 2400 x 1280 (1200 x 640 at 2x), captioned as a measurement on the image.
- Cheaper than re-shooting by hand: in the snapshot tool's drawings, replace the test word and
  picture (HELLO, `logo.png`) with real ones, move the low-band parts (the dot at 50 Hz, the
  logo's frame at 70 - 90 Hz, "Bass drop / second line") above about 150 Hz, and use Density 1024
  there; the canvas and the analyser then agree.
- Tell buyers the settings that show it best: FFT 4096 or 8192, Length 4 bars or more for text.

#### (b) The canvas: the smallest changes that would make it honest (since made, and more: see the top)

1. **Show the analyser's blur.** After `CanvasPlanImage` has worked out its levels, blur them as
   an FFT of 8192 points would (the size the Smooth lows are tuned for): vertically by the Hann
   main lobe, +-11.7 Hz turned into canvas rows at each height (wide in the lows, little above
   about 1 kHz), and horizontally by about 85 ms. One separable pass over the picture, in
   `CanvasPlanImage::render` (and over the masks before `EffectPreviewImage` carves its stand-in,
   so thin carved strokes come out faint, as they do). The lows would then look as soft on the
   canvas as in MiniMeters. A later option: an "Analyser FFT" choice (4096 / 8192 / 16384) beside
   Scale and Slope in Settings.
2. **Draw scan lines as the analyser draws them.** `CanvasPlanImage` fills the gap between lines
   that are up to 2 logical pixels apart (4 image pixels on a Retina screen); an analyser of the
   same size shows each line about one pixel tall. Make `maxLineThickness` one device pixel (or
   the FFT's main lobe, where wider). Density 1024 stays solid; Density 256 - 384 shows its lines
   in the highs, as it really does.
3. **ArtFreq FX's stand-in song (optional):** it is flatter than a real master. On the test master
   (bass-heavy, as much current music is) the mids and highs sit 13 and 21 dB lower, against its
   lows, than on the stand-in, and the kick and bass are bright lines. A steeper stand-in with
   brighter bass lines would show the mix as it will look; with change 1 it would also show carved
   shapes in the lows as filled-in as they will be. (One test song: real masters vary.)
4. **Colours:** no change needed in the sound. If the canvas should match MiniMeters' colours,
   it could map absolute dB like the Live view does (its window is -89 to -29 dBFS) instead of
   putting the loudest point at white; otherwise the caption above covers it.

These change only how the canvas looks, not the sound. 1 and 2 make the screenshots less crisp
in the lows and highs: that is the honest picture.

## Reproduce

```
# the old canvas: the snapshot tool of 0.5 (git f347d45), the new one: this version's
ARTFREQ_SNAPSHOT_STATES=1 xvfb-run -a <0.5 build>/ArtFreqSnapshot before 2
ARTFREQ_SNAPSHOT_STATES=1 xvfb-run -a build/ArtFreqSnapshot_artefacts/Release/ArtFreqSnapshot after 2
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab after truth --before before   # truth-<state>.png, metrics.txt
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --bench                        # the previews' cost
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --drag                         # ... per drag step
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --scenarios scenarios          # other songs, rates, tempos
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --scenarios standin --standin  # ArtFreq FX before a pass
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --marketing docs/marketing
build/ArtFreqLab_artefacts/Release/ArtFreqLab drip --drip --lows smooth    # the lows: what sounds outside the drawing (drip.txt, PNGs)
build/ArtFreqLab_artefacts/Release/ArtFreqLab second --skeptic --lows smooth  # ... other drawings, as raw sound (strokes.txt, .f32, .json)
```

`--states a,b`, `--ffts 4096,8192,16384`, `--range -100,0` and `--wav` choose what is measured
and how it is coloured. The audited states: default, rich, layers, linear, fx-default, fx-carve,
fx-carve-draw, fx-stencil. Everything runs against the test licensing environment (a fake
server); nothing reaches the licence server.
