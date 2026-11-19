# Are the screenshots accurate?

**Short answer:** in the mids and highs, yes: text, pictures and carved shapes land where the
canvas shows them, with the right shape. **In the lows (below about 250 Hz, and clearly below
about 120 Hz) the screenshots look sharper and cleaner than any analyser will show.** Two smaller
things differ everywhere: the colours (the canvas paints the loudest part white, MiniMeters
paints by dB) and, at the default Density, the highs (fine horizontal lines, not solid letters).

The canvas is a *preview*: it is drawn from the plan of sine waves, as an ideal analyser with
perfect resolution would show it. A real analyser has to trade time for frequency (that is the
FFT size), and the low octaves get only a few FFT bins each. No sound can beat that, so this is
mostly about what the screenshots promise, not about the sound.

## How it was checked

For every screenshot with a drawing, the exact drawing and settings were saved from the snapshot
tool and played, one pass, through the real plug-in (activated, as a buyer's copy): ArtFreq on its
own, ArtFreq FX on a dense synthetic master (drums, bass, chords, a voice, cymbals and a noise bed
at -10 dBFS RMS). The output was analysed the way MiniMeters' spectrogram works: Hann window, FFT
4096, 8192 and 16384 points, log scale 20 Hz - 20 kHz, a column every 5 - 7 ms, coloured with the
canvas's colour map over -100 to 0 dBFS, at 48 kHz. (MiniMeters' own defaults could not be looked up here:
its dB range, window and colour map are assumptions. The shapes do not depend on them; the colours
do.)

Each comparison puts the screenshot's canvas beside the real output, at the same size and on the
same frequency axis (`docs/accuracy/compare-*.png`; the full sheets, with 4096 and 16384 and the
numbers, come out of `ArtFreqTruthLab`, see the end). Bands: **lows** below 250 Hz, **low mids**
250 Hz - 2 kHz, **highs** above 2 kHz.

![The lows, close up](accuracy/lows-closeup.png)

## What is accurate

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

## What is not

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

## Verdict per screenshot

| Screenshot | Lows (< 250 Hz) | Low mids (250 Hz - 2 kHz) | Highs (> 2 kHz) | Use for marketing? |
| --- | --- | --- | --- | --- |
| artfreq-default | nothing drawn; faint streaks below | accurate | letter tops show fine lines | yes, with the caption |
| artfreq-rich, -on-play, -playing and the other UI states of that drawing | **not accurate**: thicker, blurred, hazy; dot becomes a streak | accurate | HELLO striped, not solid | only to show the interface |
| artfreq-layers | **not accurate**: low text does not read | accurate | as rich; the sunset striped | no |
| artfreq-linear (1 bar) | small, roughly right | roughly right | blurred at 8192, unreadable at 16384 | no |
| artfreq-150 / -75 (Density 200, 1x) | as rich | accurate | lines shown, and real | only to show the interface |
| artfreq-live | real measurement | real | real | yes |
| artfreq-fx-default | nothing drawn | accurate (dark, sharp) | accurate | **yes** (best FX shot) |
| artfreq-fx-carve | **not accurate**: dot and bottom edge vanish | big shapes yes; thin wave and frame sides much fainter | accurate at 4096 / 8192 | no |
| artfreq-fx-carve-draw | **not accurate**: dot becomes a dark gap | roughly | accurate, even stronger | not for the lows |
| artfreq-fx-stencil | brighter and thicker than shown | accurate, more colourful | accurate | with the caption |

## Fixes

### (a) Marketing: what to show and say

- **Lead with real measurements**, not the canvas: `docs/marketing/artfreq-real-output.png`
  (ArtFreq's default drawing), `artfreq-real-output-hello.png` (HELLO over WORLD, Density 1024),
  `artfreq-fx-real-output.png` (ArtFreq FX carving its default drawing into a dense master). Each
  is the plug-in's real audio analysed (Hann, FFT 8192, log 20 Hz - 20 kHz, -100 to 0 dBFS),
  2400 x 1280 (1200 x 640 at 2x), captioned as a measurement on the image. Say so on the site:
  *"Real measurement of ArtFreq's output, not a mock-up."* For the FX image add: *"on a
  synthetic test song"*.
- **Interface screenshots** are fine to show the interface. Put a caption under them: *"The
  canvas previews your spectrogram. Text and shapes in the mids and highs come out as drawn;
  below about 150 Hz every analyser blurs detail, so keep low art big and simple. Colours depend
  on your analyser's dB range."*
- **Do not present as "what you will see"**: artfreq-layers, artfreq-fx-carve,
  artfreq-fx-carve-draw, artfreq-rich / on-play (their low-band parts), artfreq-linear.
  `artfreq-fx-default.png` and `artfreq-default.png` are safe with the caption.
- Cheaper than re-shooting by hand: move the screenshots' low-band parts (the dot at 50 Hz, the
  logo's frame at 70 - 90 Hz, "Bass drop / second line") above about 150 Hz in the snapshot
  tool's drawings, and use Density 1024 there; the canvas and the analyser then agree.
- Tell buyers the settings that show it best: FFT 4096 or 8192, Length 4 bars or more for text.

### (b) The canvas: the smallest changes that would make it honest (not made; for approval)

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
ARTFREQ_SNAPSHOT_STATES=1 xvfb-run -a build/ArtFreqSnapshot_artefacts/Release/ArtFreqSnapshot snap
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab snap truth       # truth-<state>.png, metrics.txt
xvfb-run -a build/ArtFreqTruthLab_artefacts/Release/ArtFreqTruthLab --marketing docs/marketing
```

`--states a,b`, `--ffts 4096,8192,16384`, `--range -100,0` and `--wav` choose what is measured
and how it is coloured. Everything runs against the test licensing environment (a fake server);
nothing reaches the licence server.
