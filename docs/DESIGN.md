# ArtFreq: design

ArtFreq is an AnyMeans instrument plug-in (VST3, macOS first) that turns text, pictures and
brush strokes into sound whose **spectrogram shows them**. Open a spectrogram (MiniMeters, or
any analyser with a scrolling spectrogram) on the output and the drawing scrolls past. The same
code is also built as **ArtFreq FX**, an audio effect that carves the drawing into a song (see
[ArtFreq FX](#artfreq-fx)).

## How it works

1. **The canvas is a picture of the spectrogram.** x is time across the drawing's *length*
   (1 beat to 32 bars, synced to the tempo); y is frequency on the *same scale as the user's
   analyser* (logarithmic by default, like MiniMeters; linear and mel are offered), between
   20 Hz and 20 kHz by default. What the user sees on the canvas is what the analyser will show.
2. **The document** (`core/Document.h`) is a paint layer plus objects (text, pictures). Objects
   are placed by time (fraction of the length) and by frequency in Hz, so "put this in the
   highs" keeps meaning the highs. The paint layer is tagged with the scale it was painted on
   and mapped through it, so strokes keep their frequencies too when the scale or range changes.
   Objects are kept back to front; the order decides which one a click on the canvas picks where
   they overlap (never the sound: layers combine by their maximum). Any object, and the paint
   layer, can be hidden: it stays in the drawing and is saved with it, but is silent (documents
   from before this load with everything shown).
3. **The compositor** (`core/Compositor.h`) flattens the document into a 1024 x 1024 canvas of
   loudness values 0..1 (max of all shown layers).
4. **The plan** (`core/ArtPlan.h`) turns the canvas into a bank of sine "lines". Density =
   number of lines across the canvas height (few = scan lines, many = solid); lines closer than
   25 Hz are merged, and every canvas row plays on its nearest line. Each line's amplitude per
   column comes from the canvas, mapped to decibels with the *contrast* (dB range),
   compensated for the analyser's slope, and normalised so the loudest moment sits at -12 dBFS
   RMS before the output level. With *Lows* on Smooth (the default) the crowded lows are played
   by the rising comb instead of lines (see [Smooth lows](#smooth-lows)).
5. **The engine** (`dsp/ArtEngine.h`) plays the plan on the audio thread: one sine per line
   (and the comb's gliding sines), amplitudes interpolated across columns, click-free fades on
   start/stop/retrigger/bypass, crossfades when the plan changes.
6. **The tracker** (`dsp/PlayheadTracker.h`) decides the position in the drawing (see
   [Trigger](#trigger)): follow the DAW transport (once, from a bar of the song, or with Repeat
   looping every length on the host's bar lines; tempo changes and ramps bend the position, only
   relocations jump), or play per MIDI note (once, or with Repeat looping while a note is held);
   tempo from the host or typed in. Nothing plays by itself: without the DAW, only Preview does.

Why sound and not MIDI: a spectrogram shows the frequencies present at each moment. MIDI notes
can only make the few pitches of a synth patch, on a semitone grid, with harmonics drawn on top;
pictures need hundreds of exactly placed frequencies. So ArtFreq *is* the instrument: MIDI is
used as a trigger, and the drawing can also be bounced to a WAV file.

## Threads

- **Message thread**: the document, undo history, compositor and plan building (debounced by a
  timer, ~25 Hz; in ArtFreq FX also the masks), the editor, the live view analysis.
- **Audio thread**: tracker + engine + level + safety clipper (in ArtFreq FX: tracker + carver,
  and in the Draw modes the engine, level and clipper on the sines, then their delay); pushes
  output to the live analyser only while the live view is visible. No locks, no allocation.
- **Plans** travel through `PlanExchange`, the effect's masks through `MaskSetExchange` (wait-free
  acquire on the audio thread, garbage collected on the message thread). The carver reads a
  frame's masks over a hop of callbacks: new masks arriving meanwhile, the processor also reports
  the set the frame began with until the frame is done (`getFadingPlan`), so it is not freed.
- **Export** renders on a background thread from a shared (immutable) plan; a change that makes
  the render unwanted stops it (the message thread never waits for it).

## Parameters

| id | name | range / choices | default | automatable |
| --- | --- | --- | --- | --- |
| level | Level | -60..0 dB | -6 dB | yes |
| density | Density | 16..1024 lines | 256 | no |
| contrast | Contrast | 12..72 dB | 40 dB | no |
| lowEnd | Lows | Smooth, Stripes | Smooth (Stripes for states from before it) | no |
| length | Length | 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars, 16 bars, 32 bars | 4 bars | yes |
| trigger | Trigger | Transport, MIDI note | Transport | yes |
| repeat | Repeat | Off, On | Off | yes |
| startBar | Start bar | 1..999 | 1 | yes |
| tempoSource | Tempo | Host, Manual | Host | yes |
| bpm | BPM | 20..300 | 120 | yes |
| scale | Analyser scale | Log, Linear, Mel | Log | no |
| rangeLow | Analyser low | 10..1000 Hz | 20 Hz | no |
| rangeHigh | Analyser high | 2000..24000 Hz | 20000 Hz | no |
| slope | Analyser slope | 0, 3, 4.5, 6 dB/oct | 0 dB/oct | no |
| mode | Mode (ArtFreq FX only) | Carve, Carve + Draw, Stencil, Draw | Carve | yes |
| depth | Depth (ArtFreq FX only) | -120..0 dB | -80 dB | yes |

Density, Contrast and Lows reshape the plan, which the message thread rebuilds on its timer (by
wall-clock time, not in step with the audio): automated, they would land late, and somewhere
different on every faster-than-real-time bounce. So, like the analyser settings, they are not
automation targets.

Repeat and Start bar were added after 0.1 (parameter version hint 2). A saved parameter tree
that lacks a parameter loads it at its default (Repeat off, bar 1), not at whatever the instance
had; ArtFreq 0.1's third trigger, "Free loop" (index 2, or its name), loads as Transport with
Repeat on, the nearest thing left (a plain clamp to the two choices would make it MIDI note).
Host automation lanes recorded on the old three-choice Trigger cannot be rewritten: there,
Free loop now reads as MIDI note.

Lows was added after 0.2 (version hint 3). A saved tree without it was made with the stripes,
the only look there was, and loads with Stripes rather than the default Smooth, so an old project
looks and sounds as it did (`prepareLoadedParameters`, before the defaults for other missing
parameters are filled in).

State = APVTS tree + document tree + editor size, saved as XML in binary (`copyXmlToBinary`).
Neither the drawing nor the editor size is a parameter, so edits, undo/redo and resizes tell the
host the project changed (`updateHostDisplay` with `nonParameterStateChanged`); loading state
does not. Each picture is PNG-encoded once and its text kept while any document or undo step
still holds the picture (a loaded picture keeps the text it was loaded from), and a load keeps
the loaded tree as the state to save, so saving after an edit or a load encodes nothing again.

## Trigger

When the drawing plays is the Trigger (Transport | MIDI note) and Repeat (off by default). The
producer's rule: nothing plays by itself, and loops only while something is playing.

| | Repeat off | Repeat on |
| --- | --- | --- |
| **Transport** | Once: from the start of bar *Start bar* to *Length* later, following the DAW's timeline. Start the DAW inside it and it plays from there; before and after it is silent; a DAW loop over it plays it each time round. | Loops every *Length*, on the host's bar lines (bar 1, 1 + Length, ...), while the DAW plays. |
| **MIDI note** | Each note-on plays it once from the start (a new one restarts it); note-offs are ignored. | A note-on starts it (a new one restarts it) and it loops while any key is down; letting go of the last fades it out. |

- Bar N is the DAW's bar N, on the host's ppq, also with a typed-in tempo (which then only sets
  how fast the pass plays from where the DAW reaches its start bar); the pass lasts Length in the
  start bar's time signature. VST3 gives no bar numbers, so the tracker keeps the pass's place in
  the song. When playback starts or moves to before it, bar N is (N - 1) x quarterNotesPerBar
  quarter notes from the song start in the current signature (assumed constant from the song
  start), moved to the nearest of the host's bar lines (`ppqPositionOfLastBarStart`; the earlier
  one half way), so it is always on a bar line, and the DAW's bar N after a shorter odd bar. While
  the song plays towards it the bars still to go are counted in the bar it is in, so a signature
  change on the way (an odd bar, a new section) moves it with the bars; from its start on it stays
  put, so a change at, inside or after it moves nothing and a DAW loop over it plays the same pass
  (also after moving back into bars already counted). Only starting the DAW between a signature
  change and the start bar, without having played through the change, places it by the
  assumption.
- The edges of a single pass fade like a gate: the 5 ms fade-in starts at the pass's first
  sample, and the gate closes 5 ms (and a sample) before its end so the fade-out ends with the
  drawing, as a bounce and a MIDI one-shot do. A relocation into the middle fades the old sound
  out first.
- All Notes Off, All Sound Off and the DAW stopping end a MIDI pass either way and forget the keys
  held (so a missed note-off cannot keep a later note looping); notes while the DAW is stopped
  still play.
- **Preview** (the footer's play button) plays from the start whatever the trigger: with Repeat
  on a loop until pressed again, without it once, after which the button turns itself off (the
  audio thread clears the press it played, never a newer one).
- **Bounce WAV** always renders one pass.
- Changing Trigger, Repeat or the start bar while playing moves to wherever the new setting puts
  the position, with a jump fade when it does not follow on (turning Repeat off inside the single
  pass, at the same place as the loop, carries on seamlessly).

## Smooth lows

The customer, after 0.1: the stripes in the lows are a nice effect, but they lose a lot of a
picture. On a log scale the lows hold few lines (merged to 25 Hz apart, as closer lines beat and
pulse), and an analyser with a long FFT (MiniMeters at 8192 and up) resolves them into stripes
with dark gaps: low-band fill 0.38 at 8192, 0.21 at 16384. Three ways of filling the gaps were
built and measured with ArtFreqLab: lines that wobble inside their spans, narrow noise bands,
and a rising comb. The comb won (it filled best and flickered least, and at 4096, where the lines
already looked fine, it stayed as good), so it is **Smooth**, the default for new instances;
**Stripes** stays as the other choice, as the customer liked it as an effect.

- **What it replaces.** The lines from the bottom up to the last one that stands closer than
  twice the minimum spacing to the one below (the merged and crowded lows, below 2 kHz): about
  20 Hz - 1.8 kHz at density 256, up to 2 kHz at 1024, up to about 110 Hz at 16. A line with room
  on both sides is the density's own scan line and stays, so a low Density keeps its scan-line
  look above the comb. Nothing is crowded on a linear scale: no comb there. The lines above keep
  their frequencies (bit for bit) and spans; their amplitudes change only by the normalisation.
- **The comb.** Sines 20 Hz apart (at most 255) glide upwards together at 200 Hz/s, from 15 Hz
  below the band to 15 Hz above it, where each wraps round to the bottom; they fade in and out over
  those 15 Hz. Every height is passed every 0.1 s, so a short window blurs neighbouring sines
  together and a long one sees each smeared along its glide: energy at every height from 4096 to
  32768 points. All glide at one speed, so they never cross or crowd; fixed unrelated starting
  phases keep the passes from adding up in a repeating pattern. What remains is a fine rising
  shimmer (out of step from height to height), not a pulse of the whole band. 20 Hz and 200 Hz/s
  had the lowest worst case of the settings tried (25 Hz or 130 Hz/s pulse at up to 5 dB).
- **Its bottom.** The fade-in starts from silence wherever a sine can first be heard: never below
  10 Hz (nothing is played lower: with the analyser's low end at 10 Hz the glide starts at -2.5
  Hz), and more than one chunk's glide (1 Hz at 48 kHz) above the bottom of the glide, as a sine
  that passes the top during a chunk wraps round unheard and is next played that far up. It runs
  over 15 Hz from there, so for a low end under 17.5 Hz the lowest grid lines fade in too. (A sine
  coming on at a level of its own, up to a sixth of the bottom's at a 10 Hz low end and 7 % after
  a wrap, clicked every 0.1 s, put energy far above the drawing and rumble below 5 Hz.) A sine
  fading in that low still does so over about one cycle, which leaves a little content below 10
  Hz: about -35 dB against the whole for a drawing from the bottom to 200 Hz at the default low
  end (lines leave none). It is inaudible, and below what analysers show; a longer fade would
  darken the bottom of the canvas.
- **Its loudness.** The replaced band of the canvas becomes a grid of plan lines 5 Hz apart (the
  maximum of the canvas over each cell): finer than the sines, so edges stay where they were
  drawn. Each sine plays the grid interpolated at the height it has reached, between grid lines
  and between columns. The grid lines are never sines of their own (not in `activeLines`;
  `combActive` says where the comb can sound). At the edges of a run of drawn grid lines (in one
  column, five or more) the outermost plays at half its level and the next at 0.93: a sine then
  enters a shape over 75 ms, not 25, and splashes less energy below it and into holes (measured:
  1.3 dB less leakage in the lows, fidelity +0.01, fill unchanged; runs of four lines keep their
  level).
- **Thin strokes.** A run of drawn grid lines narrower than the sines' spacing (fewer than four, a
  one-row stroke, an outline, thin lettering) would be lit only while a sine is inside it: the
  whole stroke would blink ten times a second (15 - 28 dB on an analyser of 4096 points, 4 - 5 dB
  at 8192, a train of blips to the ear), while the canvas showed it steady. So, column by column,
  such a run is silenced in the grid and played instead by the replaced line nearest its middle,
  at the run's loudest value: steady, where Stripes would play it, and drawn as Stripes draws it.
  The replaced lines stay in the plan for this, after the grid (`combThinLines`; with the lines
  above, exactly a Stripes plan's lines), silent elsewhere. The comb's narrowest runs (four lines)
  shimmer as any shape does: about 3 dB at 4096 points, 1 dB at 8192.
- **Its brightness.** A line's energy stays in one analyser bin; the comb's is spread along its
  glide, about 3 dB darker per doubling of the FFT size. It is played 3.5 dB louder (combGain
  1.5), as bright as the lines above at 8192 (3 dB brighter at 4096, 3 - 7 dB darker at 32768).
  The normalisation counts it as its sines, each with the mean power of the grid it glides over.
  The lows carry about 4.5 dB more power than the stripes did, so in drawings with lows the rest
  sits lower by up to about 5 dB in absolute level (the balance is the intended correction).
- **The engine.** A separate SIMD kernel turns each sine's step by a fixed angle per sample, so
  its frequency rises exactly linearly. At every chunk each sine's frequency and phase are worked
  out afresh from the samples rendered since `reset()` (double-precision recurrences across the
  sines, no trig per sine), so where blocks and chunks fall changes the sound only by rounding
  and the phase never drifts. Buffers are sized in `prepare()`; nothing allocates, locks or waits.
  Plans are checked (`hasComb`) before the comb is played. Crossfades work on the sines'
  amplitudes, linear between plans of one layout, equal power otherwise, as for lines; switching
  Lows changes the layout, so it crossfades over 20 ms like a Density change.
- **Cost.** 93 sines + 88 lines at density 256 (Stripes: 142 lines), 101 + 295 at 1024 (366); in
  the worst case (a one-beat, fully drawn density-1024 plan) 65 - 95 x real time on one core
  (Stripes: 100 - 150 x); buildPlan 2 - 8 ms (1 - 3 ms); about 1.5 MB more per plan.
- **Measured** (ArtFreqLab, Hann, interpolated, mean over four drawings and both densities;
  low = 30 - 250 Hz / mid = 250 Hz - 2 kHz, Stripes -> Smooth):

  | FFT | fidelity | fill | gap dB | flicker dB | leak dB |
  | --- | --- | --- | --- | --- | --- |
  | 4096 | .84/.93 -> .89/.96 | .93/.86 -> .93/.92 | 4.2/6.9 -> 4.6/4.7 | 1.2/1.1 -> 2.9/2.8 | -9.7/-16.8 -> -15.4/-22.7 |
  | 8192 | .69/.76 -> .90/.95 | .38/.53 -> .93/.93 | 17.9/24.7 -> 4.4/3.7 | 0.9/0.8 -> 2.7/2.3 | -9.7/-17.1 -> -17.3/-21.3 |
  | 16384 | .43/.57 -> .88/.93 | .21/.21 -> .88/.91 | 18.5/28.0 -> 5.8/4.1 | 1.3/0.6 -> 3.7/2.1 | -10.7/-16.4 -> -17.8/-18.3 |
  | 32768 | .31/.49 -> .84/.90 | .30/.21 -> .81/.90 | 12.5/19.8 -> 7.1/3.9 | 1.7/0.8 -> 3.8/2.0 | -11.1/-14.8 -> -17.4/-15.4 |

  The flicker is the shimmer: steady cells move by about 3 dB from frame to frame (the stripes'
  were steadier, but dark). The worst regular pulse in any single case falls from 5.5 to 3.7 dB.
  Still to judge by eye and ear: real photos in MiniMeters on macOS, and the sound of the lows (a
  rising, shimmering texture instead of steady tones).

## Carving

The producer asked for a "negative mode" for full songs: instead of playing the drawing, mute
its frequencies in the incoming audio, so it shows as dark shapes in the song's spectrogram
(Carve), or let only the drawing through (Stencil). ArtFreq FX offers it (see
[ArtFreq FX](#artfreq-fx)); this is the engine side, and the lab two engines were measured in:

- **The mask plan** (`core/MaskPlan.h`) is built on the message thread from the same canvas as
  the sine plan: per canvas column a mask over frequency, 0 = untouched, 1 = carved by all of
  Depth (the canvas value itself, so a half-strength stroke carves half as many dB), with the
  scale that maps its rows to Hz, so an engine can sample it at whatever frequencies its
  analysis has (at a point, or averaged / maximised over a band). An optional halo widens
  shapes by rows and columns. It travels to the audio thread like the sine plan
  (`MaskPlanExchange`: `PlanExchange` is now one template for both).
- **The engine interface** (`dsp/SpectralCarver.h`): prepare (rate, block, channels), a constant
  latency, and an in-place process that takes the tracker's PlaySegments, so a carve follows
  Length, Trigger, Repeat, start bar and tempo exactly as the sines do; the gate fades over
  5 ms, a jump moves the mask at once (the audio jumped too). Outside the pass and with
  nothing drawn the audio passes unchanged apart from the latency (also in Stencil). Depth
  -120..0 dB; gain = 10 ^ (depth x amount / 20), amount = gate x mask (Carve) or gate x
  (1 - mask) (Stencil). `createSpectralCarver (n)` picks an engine by number.
- **The baseline engine** (`dsp/BaselineCarver.h`): an STFT of 2048 points at 48 kHz (about
  43 ms at any rate), sqrt-Hann windows, 75 % overlap, the mask at each bin's centre frequency
  at the frame's centre, no smoothing. Latency one frame (2048 samples, 42.7 ms). Frames that
  change nothing skip the FFT; once every frame over a hop is such a frame the delayed input is
  played bit for bit.
- **Engine 1, the tuned STFT** (`dsp/TunedCarver.h`): still one STFT, with every part chosen by
  measuring it in the lab. Frames of 85 ms (4096 samples at 48 kHz, the same time at any rate;
  44.1 kHz zero-pads 3764 into 4096): bins of 11.7 Hz instead of 23.4 Hz are what lets the lows
  carve deep, and the analyser blurs time about as much as the longer frame does. sin^3 analysis
  and synthesis windows at 75 % overlap (their product, Hann^3, adds up to a constant, and its
  sidelobes fall fast enough that carved and kept bands no longer leak into each other; 87.5 %
  overlap measured the same at twice the cost). Carve averages the drawing over each bin's band
  (a stroke thinner than a bin is carved by its share instead of missed) and widens what it
  carves by one bin (a halo: the window spreads each frequency over its neighbours, so a band
  carved exactly to its edge stays shallow there; this is what makes thin shapes such as a sun's
  rays dark). Stencil takes the drawing's maximum over each bin's band and no halo, so the kept
  shapes stay whole. The gate is applied per sample at the output: outside the pass the output
  is the delayed input bit for bit, and a frame reaching over the pass's start or end carves as
  the pass's nearest moment does, so the carve is complete from the pass's first sample and
  Stencil no longer mutes 15 ms either side of it. Latency 4096 samples (85.3 ms). Lab, depth
  -60, mean over pink and the mix and the four drawings (baseline in brackets), as judged, before
  the grafts below:

  | | Carve 4096 | Carve 8192 | Carve 16384 | Stencil 8192 |
  | --- | --- | --- | --- | --- |
  | depth / vis / r, all | 51.1 / 46.2 / 0.957 (46.0 / 43.9 / 0.940) | 51.7 / 46.2 / 0.938 (43.6 / 41.8 / 0.916) | 44.1 / 40.4 / 0.896 (36.8 / 36.0 / 0.881) | 57.3 / 52.7 / 0.845 (57.5 / 51.9 / 0.848) |
  | depth / vis, 30-250 Hz | 42.7 / 33.5 (33.2 / 29.3) | 45.9 / 36.6 (32.2 / 30.3) | 47.8 / 39.1 (32.8 / 31.3) | 56.2 / 55.8 (56.9 / 52.6) |
  | err / edge time ms / edge rows, all | 1.7 / 52 / 9.9 (5.8 / 49 / 10.8) | 1.0 / 87 / 9.4 (7.8 / 82 / 9.9) | 7.7 / 120 / 9.9 (13.8 / 120 / 9.5) | 0.1 / 77 / 8.5 (0.1 / 78 / 11.0) |
  | near / pre leakage, all | 2.7 / 1.6 (1.0 / 0.6) | 1.6 / 0.8 (0.6 / 0.4) | 1.2 / 0.7 (0.6 / 0.3) | 0.6 / 0.6 (0.9 / 1.0) |

  The worst case of the baseline, the sun over the mix's bass and kick (FFT 8192), goes from 29.6
  to 56.0 dB deep (err 29.0 to 4.4) and its rays show. Stencil starts and stops with the pass
  (lead / lag -0.1 / -0.4 ms, baseline 14.9 / 15.6). Click pre-echo -10.1 dB in Carve (-10.3),
  -3.2 in Stencil (-3.2); kick smear 1.4 dB in Carve (0.9), 2.5 in Stencil (2.2). Cost about
  7 ms of CPU per second of stereo audio (8.3), exact outside the pass and with nothing drawn, the
  same output for any block sizes. What it costs: twice the latency; carving spreads a little beside
  the drawing where a bin is wide (near leakage 6 dB within two display rows below 250 Hz at
  FFT 4096, pre-echo 3.5 dB there); time edges a few ms wider; and narrow strokes still look
  hollow on a 16384-point analyser (that needs carving wider in time, the plan's halo in columns).
- **Engine 1 won** against engine 2, a multi-resolution STFT (three bands of 171, 43 and 11 ms
  with crossover weights): at the default Depth both look as visible (a tie at -80 dB), engine 1
  costs a third of the CPU, has 43 ms less latency and much simpler code. Three things were
  grafted onto it from engine 2:
  - **The drawing over each hop.** A frame reads the drawing at points about every 2.7 ms
    (8 a hop at any rate; 128 samples at 48 kHz) over the hop around its centre (the second half of
    the hop before the window's centre and the first half of the next), weighted by the gate
    there, not at one moment: at a one-beat length a hop spans 40 columns, and a thin vertical
    stroke between two frames' centres was missed. Carve and Stencil take the points' mean (a
    stroke carves by its share); Carve + Draw (`holdLoudest`) their maximum, so the hole covers
    the sines wherever they sound in the hop. At a shape's edges the centres decide
    (`sharpEdges`): where the frame's centre is drawn it carves at least that much; where it is
    not but the centre of a frame beside it is, that frame carves the shape and this one does not.
    The mean alone spread every time edge over the frames whose hop reached it: in the lab 6 ms
    wider edges at FFT 4096 and more leakage before shapes (pre 1.6 -> 2.8 dB); with the rule
    edges, leakage and visibility are as judged (carve all, FFT 4096 / 8192 / 16384: edge time 52
    / 87 / 120 ms, visibility 46.2 / 46.3 / 40.6, r 0.958 / 0.940 / 0.898; stencil edges 53 / 77
    / 183 ms), every case is still exact across block sizes and outside the pass, and thin strokes
    are carved (tests: `tunedCarverReadsThinVerticalStrokes`). Cost about 8 ms of CPU per second
    of stereo audio here (7 - 8 in Carve, 10 in Stencil; the judge measured 7.1 before the grafts,
    on an idle machine).
  - **Column edges.** A point exactly on the edge between two columns (a pass is often a whole
    number of samples per column) reads the later one: the position plus a billionth of the pass
    (`columnNudge`), so where a host's rounding falls never changes which column is read.
  - **The halo per mode** (`CarveSettings::haloBins`): Carve 1 bin, Carve + Draw 3 (35 Hz either
    side, the low half of its dark edge), Stencil none.
- **Work per audio callback.** A frame's transforms need the last sample of its hop, and the
  first sample played out after it needs them, so they run in the callback that completes the hop
  (at this latency there is no way round that: spreading them would cost another hop). Reading
  the drawing for a frame, its columns at the points and then the gains, does not: it runs a
  stage (one column, or the last step) at a time over the callbacks while the frame's newest hop
  comes in, a stage ahead of an even share of the hop, so with blocks up to a thirteenth of a hop
  (78 samples at 48 kHz, 315 at 192 kHz) the callback that completes the hop only transforms
  (`tunedCarverSpreadsTheDrawingOverTheHop` counts it call by call). A frame reads the plan and
  the settings of the callback it began in to its end; a plan set in the middle of the hop is
  reported as still in use (`getFadingPlan`), and the processor keeps its mask set alive until the
  frame is done. The points are spaced in time, so their number does not grow with the rate, and
  only the bins below the top of the scale are read (at 192 kHz a fifth of them). Before this, the
  callback that completed a hop also read the drawing (32 points a hop at 192 kHz): measured here
  with a full-band drawing one beat long, its typical worst callback at 96 kHz / 32 samples went
  from 1.2 to 0.5 ms and at 192 kHz / 128 from 2.3 to 0.9 ms, about the transforms alone (JUCE's
  portable FFT on Linux; on the Mac vDSP's, several times faster). The mean cost halved at 192 kHz.
- **The lab** (`tools/CarveLab.cpp`, target `ArtFreqCarveLab`) plays dense songs (pink noise, a
  busy synthetic mix, or a WAV) through an engine with four drawings (text, a filled sun, a
  ramp, a photo-like portrait), one 8 s pass from bar 2, in Carve and Stencil, and measures
  what an analyser like MiniMeters (log, 20 Hz - 20 kHz, FFT 4096 / 8192 / 16384) shows: carve
  depth, visibility against the surroundings, fidelity to the drawing, edge widths in time and
  frequency, leakage and pre-echo around the drawing, musical noise, a click's pre-echo,
  transparency, block-size independence, cost and latency (see the top of the file). It writes
  metrics.txt / metrics.json and pictures of the input, output and attenuation.

## ArtFreq FX

The producer, on a full song: the played frequencies get lost in everything else, so could
there be a negative mode that mutes them instead, and could ArtFreq go on audio tracks and the
master as well as MIDI tracks? Hosts decide from a VST3's category where it may go (Ableton Live,
FL Studio and Cubase offer an instrument only on MIDI / instrument tracks and an effect only on
audio tracks, buses and the master), so no one plug-in can be both. The same plug-in is
therefore registered twice: **ArtFreq** (Instrument, Generator; unchanged) and **ArtFreq FX**
(Fx, Filter; plug-in code FqFx, bundle id com.anymeans.artfreqfx), one installer for both.

- **One code base.** `ArtFreqProcessor` takes a `Variant` (instrument | effect); the editor is the
  same class and asks the processor which it is. Each CMake target (`artfreq_add_plugin`) adds
  only its own `createPluginFilter` (`src/plugin/`). The effect has stereo or mono in and out (as
  many out as in), takes MIDI (hosts that send effects MIDI can trigger it), and reports one
  carver frame of latency.
- **Parameters.** Every instrument parameter, plus **Mode** (Carve, Carve + Draw, Stencil, Draw;
  default Carve) and **Depth** (-120..0 dB, default -80), both automatable and both acting on
  the audio thread at once: the exact and the halo masks are built together (`MaskPlanSet`) and
  Depth is read per frame. -80 dB: a master's bass and kick sit around -15 dB in an analyser's
  cells, so it takes 75 - 80 dB to reach a spectrogram's floor near -95 dB (the judge's sweep:
  visibility 58 / 60 at FFT 4096 / 8192 against 46 at -60; the cost, frequency edges 2.3 display
  rows wider and a little more kick smear).
- **What it does per block.** The tracker makes the segments (Length, Trigger, Repeat, start bar,
  tempo, MIDI, Preview: exactly the instrument's), the carver (engine 1) carves the block in
  place with them, and in Carve + Draw and Draw the sine engine renders the same segments, at
  Level (and through the safety clipper: the sines only, never the song), into a delay of the
  carver's latency, so the sines come out with the audio they were played against (Draw adds
  exactly the instrument's output, one latency late). Draw passes Depth 0 to the carver, which
  then changes nothing; Carve + Draw passes the halo masks, 3 halo bins and holdLoudest. The
  sines are not rendered in Carve and Stencil; turned on they start afresh with a fade-in, and
  turned off fade out over 20 ms, as Level changes do. The playhead and "sounding" come from the
  segments (no sines are needed to know where the pass is).
- **Carve + Draw's halo**, measured by the judge (pink noise with sines at the song's level, FFT
  4096): the hole is the drawing dilated by 4 canvas rows (2.7 %, about 2.4 display rows) and
  carved 3 bins wider (35 Hz): the rows make the dark edge in the mids and highs (-36 / -44 and
  -55 / -59 dB below / above), the bins in the lows (-18 / -20 dB; 2 bins lost the edge there
  with louder sines). In time, about 60 ms either side (-28 to -30 dB at FFT 4096; 40 ms gave
  only -12 to -17), in columns at the length in use (8 at 8 s), at most a sixteenth of the length
  (`maxHaloColumns`), rebuilt when the length or the tempo changes it; the gate at the output
  clips it to the pass. The dilation builds its window from doubling shifts, so its cost grows
  with log (radius): both masks take a few ms on the message thread.
- **Latency.** One frame: 4096 samples at 48 kHz, 85.3 ms at every rate (3764 at 44.1 kHz, 8192
  at 96 kHz), set in prepareToPlay (and at 48 kHz in the constructor, for hosts that ask before
  they prepare), the same in every mode, with any drawing and in bypass. Switching modes never
  changes it, so the host's compensation never moves the song.
- **Transparency.** Outside the pass (before and after a single pass, DAW stopped), with nothing
  drawn or everything hidden (also in Stencil: the gate stays shut) and in Draw, the output is the
  input one latency late, bit for bit (plus the sines in Draw); carving fades in and out with the
  gate's 5 ms S-curve at the output. A frame reaching over the pass's start or end carves as the
  pass's nearest moment.
- **Peaks.** The song is not limited or clipped (the safety clipper is on the sines only), so
  transparency holds; but carving changes the phases a limiter set the peaks with, and the Draw
  modes add sines. On pink noise at -11 dBFS RMS limited to -0.3 dBFS, with the default drawing
  and Level: Carve +0.8 dBFS, Carve + Draw +1.8, Draw +2.2, Stencil below the input
  (`effectPeaksOnALimitedMaster`). The README tells users to put ArtFreq FX before the master's
  final limiter, or leave that headroom.
- **Bypass** (`processBlockBypassed`): the same path with Depth 0 and the gate closing: the carve
  crossfades out over a frame, the sines fade out, then the output is the delayed input bit for
  bit. The latency stays, so a host's bypass never shifts the audio. The carver is never reset
  there (that would drop the audio in flight); the tracker and the sines are, once silent.
- **A host's flush** (`reset()`: VST3 setProcessing (false), on stop, on a seek, or while a host
  suspends a silent effect) drops the audio in flight: the carver's history and overlap-add, the
  sines' delay, the engine and the tracker. Otherwise the next playback would start with up to a
  latency of what played before, at full level. From there the output is silence for a latency
  and then the input that late, as after prepareToPlay (only clearing: no allocation; under the
  callback lock, as hosts may call it from another thread). The instrument holds no audio.
- **Mode and Depth changes** never click: the carve's gains change from frame to frame and the
  frames overlap-add (sin^3 windows, 75 %), the sines fade (tested with every mode switched in
  turn and Depth jumping 80 dB).
- **Preview** runs the pass from the start on whatever audio comes in, also while the DAW is
  stopped (most hosts keep feeding effects), once or on a loop with Repeat, and plays the sines in
  the Draw modes: the same as the instrument's Preview, on the song.
- **MIDI.** The effect asks for MIDI: Reaper, Bitwig, Cubase, Studio One and FL Studio (through
  the wrapper's MIDI input port) can send it to an audio effect, so Trigger = MIDI works there.
  Ableton Live never sends MIDI to audio effects: Transport needs none.
- **State.** The same XML as the instrument's (`ArtFreqState`, now with a `plugin` attribute
  naming the variant that saved it), so either loads the other's drawing and shared settings:
  the instrument leaves out Mode and Depth (unknown parameters are dropped on load, so they are
  not saved back as its own), the effect fills them in at their defaults. Hosts do not offer
  that, though: they keep presets and states per class ID (FqAr and FqFx differ, and the VST3
  SDK's preset loader refuses another class's preset), so moving a drawing between the two needs
  a feature of its own (copy and paste of the drawing, or an export and import of it), not yet
  built; the README says so.
- **The rename (0.4).** FreqArt became ArtFreq. The plug-in codes (Anym, FqAr, FqFx), and so the
  VST3 class IDs, and the parameter IDs are unchanged, so old projects open the renamed
  plug-ins. The state is written as `ArtFreqState` / `ArtFreqParameters` / `ArtFreqDocument`;
  the `FreqArt*` tags of earlier versions are read too. The installer removes old `FreqArt.vst3`
  and `FreqArt FX.vst3` bundles from the system VST3 folder once their replacements are in, so a
  DAW does not list each plug-in twice.
- **The editor** is the instrument's with:
  - **Header**: "ArtFreq FX" (FX in accent2); Canvas | Live beside the wordmark; then, centred
    between it and undo, MODE and a segmented Carve | Carve + Draw | Stencil | Draw (284 px; its
    tooltip says what each does) and DEPTH with a number field (72 px, "-80 dB": drag, click to
    type; greyed in Draw). 24 px either side of the group; it fits from 75 to 200 % (the
    snapshot tool checks nothing overlaps).
  - **Canvas** (`ui/EffectPreview.h`): the effect needs a song the canvas does not have, so a
    stand-in is drawn under the drawing: a texture with a song's shape (falling towards the highs,
    -22 to -40 dB of the 60 dB colour window, sixteen beats of kick in the lows and hats in the
    highs, held chords, grain on a fixed grid so resizing does not shimmer), carved by the
    processor's own masks as deep as Depth: Carve, dark shapes; Stencil, the shapes filled with the
    stand-in on black; Carve + Draw, the sines' picture (the instrument's canvas, as levels) in a
    hole carved with the halo masks; Draw, the sines over the stand-in. Nothing drawn: the
    stand-in as it is, as the effect leaves the song. In the Draw modes the sines are drawn at
    their own level, Level included (the canvas follows it), and added to the stand-in as powers:
    the window's top is a full-scale sine in an analyser cell, the stand-in sits where a loud
    master (-10 dBFS RMS) shows at FFT 4096 (about -32 dB a cell at 1 kHz; pink noise there: -34),
    and the sines are drawn 10 dB above their plan amplitude (`sinesCalibrationDb`), which gives
    the contrasts measured through the processor on that master: the default drawing at the
    default Level +5.3 dB above the song in Draw, +9.4 in Carve + Draw (drawn against undrawn
    cells, 400 Hz - 2.5 kHz); the canvas, +5.4 / +9.6. Before, the sines were drawn at the top of
    the window whatever the Level, about 31 dB above the stand-in: Draw looked far stronger than
    it is on a full song, which is the customer's complaint. At -60 dB the sines are gone.
  - **Live** shows what comes out (the song, carved): the processor pushes its output (the
    channels' mean) to the analyser.
  - **Footer**: Density, Contrast and Level are greyed in Carve and Stencil (they shape the sines
    only; their tooltips say so; Level is the sines' level, the song keeps its own). Export: in
    Carve and Stencil the column says "Bounce the track in your DAW" over "Carving needs the
    song" (a bounce of the effect needs the song, which only the DAW has; no render runs then);
    in the Draw modes Bounce WAV renders the sines as in the instrument ("The sines, one pass").
    Preview's tooltip says it runs on the audio coming in.
  - **Help**: its own steps (where it goes, the analyser, drawing, mode and depth, playing,
    bouncing in the DAW), the modes, and timing, MIDI per host and the latency.

## Interface

Window 1120 x 720 at 100 %, resizable from 75 % to 200 % with a fixed aspect ratio (corner
resizer + window-size choice in Settings), remembered per instance.

### Look

Dark, quiet, precise; colour only where it means something.

| token | value | use |
| --- | --- | --- |
| bg | #0B0D12 | window background |
| panel | #12151C | header, side panel, footer |
| raised | #1A1E27 | buttons, fields, segmented controls |
| raisedHover | #222735 | hover |
| line | #262B36 | dividers, control outlines |
| text | #E8EBF2 | primary text |
| textDim | #8C94A6 | labels, secondary |
| textFaint | #5A6274 | axis labels, hints |
| accent | #7C6CFF | selection, active tool, focused control, playhead |
| accent2 | #36D7C4 | "playing" state, preview button on, live indicator |
| danger | #FF5C6C | delete hover |

Spectrogram colour map (silent to loudest), used on the canvas and the live view so they look
alike: #07080C, #1B1036, #3D1466, #7A1F7F, #B83A6E, #E8664A, #F9A13A, #FCE7A0, #FFFFFF.

Fonts: Inter (UI), DM Mono (numbers). Sizes: 11 px labels (uppercase, +6 % tracking),
13 px controls, 15 px section titles, 20 px wordmark.

Corners 6 px on controls, 10 px on panels. No gradients on controls; 1 px `line` outlines;
focus/active = accent outline or fill. Hover states on everything clickable.

### Layout (100 %)

```
+--------------------------------------------------------------------------------------+
| Header 52: ArtFreq wordmark + "by AnyMeans" | [Canvas | Live] | undo redo | ? | gear  |
+--------------------------------------------------------------------------------------+
| Toolbar 44: [Select][Brush][Erase]  Size --o-- Soft --o-- Strength --o-- | +Text +Image | Clear |
+-------------------------------------------------------------------+------------------+
| Hz axis 44 |  CANVAS (time x frequency), playhead, objects        | Inspector 280    |
|            |                                                      |                  |
|            |  time axis 22 (bars/beats)                           |                  |
+-------------------------------------------------------------------+------------------+
| Footer 112: DENSITY CONTRAST LEVEL | LENGTH TRIGGER [Repeat] TEMPO [Host|Manual] BPM | PREVIEW | EXPORT |
+--------------------------------------------------------------------------------------+
```

### Canvas

- Shows the current plan as the analyser would: each line drawn at its height with the colour
  map of its amplitude in dB (relative to the loudest), lines 1-2 px tall so low density shows
  as visible scan lines and high density looks solid. With Smooth lows the comb's band is drawn
  filled: each 5 Hz cell of its grid over its whole height, at amplitude / combGain (the
  brightness an analyser of 8192 points shows; the first line above takes over where the band
  ends, so there is no seam), and thin strokes as the lines that play them, as Stripes draws
  them. Background = colour map at silence.
  Faint frequency grid lines (from `gridFrequencies`) and bar/beat lines.
- Hz labels on the left axis; bar/beat labels along the bottom (e.g. "1", "2", "3", "4" for
  4 bars; beats for lengths under a bar), in the host's time signature (its beats: three to a
  bar in 3/4, six eighths in 6/8). When the drawing plays once at a bar of the song (Transport,
  Repeat off) they are the song's bars: "9", "10", "11", "12" for 4 bars from bar 9 (under a bar,
  the beats of that bar: "9", "9.2").
- Playhead: a 2 px accent2 vertical line at the play position while sounding.
- **Select tool**: click an object to select it (the selected object if the point is inside it,
  so it can be moved even where another lies over it; else the front-most shown object whose
  rectangle contains the point); selected object shows its rectangle (1 px accent) and 8
  handles; drag inside to move, drag a handle to resize, click empty canvas to deselect.
  Alt/Option-click, or clicking again on the same spot without moving the mouse in between,
  picks the next object down under the mouse (round to the front again; on release, so a press
  and drag still moves the selection). Right-click on an object selects it and opens its menu:
  Bring to front, Send to back (greyed where they would change nothing), Hide (Show for a hidden
  one), Delete. Selecting on the canvas shows the object's settings in the inspector.
  Delete/Backspace removes the selected object. Arrow keys nudge (Shift = 10x); a held
  arrow is one undo step, which ends when the keys are let go, on a click, on another object or
  after a 1 s pause. Objects' rectangles are drawn as faint dashed outlines when not selected, so
  they can be found even when quiet.
- **Hidden objects** are not drawn (the canvas shows the plan, which leaves them out) and cannot
  be clicked; one selected in the layer list shows as a faint dashed accent outline with a small
  "Hidden" tag, and can be moved and resized like any other. Painting on a hidden paint layer
  shows it again (a stroke nobody can see or hear would look like a broken brush).
- New text and pictures ("Add text", "Add image...") go where no object is yet: the mid band
  across the length (4 % to 96 %), else the high or low band, else half the length in one of
  them; if all of that is taken, never exactly on top of another object (the start moves along).
- **Brush / Erase**: paint with the toolbar's Size (2..64 px, screen), Softness (0..100 %),
  Strength (0..100 %). A circular cursor preview shows the brush. One stroke = one undo step.
- Drop an image file onto the canvas to add it as a picture where it was dropped (in the band
  under the cursor). Pictures: PNG, JPEG, GIF, and on a Mac also HEIC, WebP, TIFF and BMP (macOS
  decodes them). A file that cannot be read shows a short message over the bottom of the canvas
  for a few seconds.
- Empty state (no objects, nothing painted): centred hint "Add text, drop an image, or paint
  with the brush".

### Live view

A scrolling spectrogram of ArtFreq's own output (`LiveAnalyzer`), right to left, same scale,
range, slope and colour map as the canvas. It shows one pass of the drawing and a quarter more
(whole seconds, 4 to 20 s; it follows the length and the tempo), so the whole drawing is on screen
for a while each pass; seconds are labelled every 1, 2 or 5 s, as room allows. A small "LIVE" badge
in accent2.
Shown instead of the canvas when "Live" is selected in the header. Turns the analyser on only
while visible.

### Inspector (right panel)

- **Layers** (always with nothing selected): title "Layers" with a summary ("5 objects, 1 hidden");
  buttons "Add text", "Add image..."; the layer list; a one-line hint ("Double-click to edit,
  drag to reorder"); then the Keys (shortcut caps) while they fit under the list, or the brush
  settings mirrored when Brush/Erase is active; at the foot, always in the same place, **Lows**: a
  caps label and a segmented Smooth | Stripes (168 px, 28 tall, right-aligned, 13 px under a
  hairline; its tooltip says what each does). Lows belongs to the whole drawing, neither to an
  object nor to the analyser match, so it lives with the list rather than in Settings, and makes
  way for an object's settings like the list does. The list (`ui/LayerList.h`) is a dark well with
  one 30 px row per object, front first: type icon (T or picture), name (the text's first line
  with words, or the picture's file name; ellipsised), and on the right a delete button (only
  while the mouse is over the row; danger colour on hover), an eye (open and quiet when shown,
  struck through and brighter when hidden; the name goes faint) and a chevron to the object's
  settings. Below a hairline, the Paint row: brush icon, "Paint" ("empty" when nothing is
  painted), "Clear" (paint only) and its eye. The selected row is filled and outlined in accent
  (brighter while the list has the keyboard focus, as is the well's outline); hovered rows are
  raised. The list takes the height its rows need, the whole room once the keys no longer fit,
  and scrolls (wheel, thin scrollbar) when the rows do not fit.
  - Click a row: selects its object (Select tool, handles on the canvas); the list stays, so a
    selection can be walked through it. Double-click, Return or the chevron: its settings.
  - Drag a row (4 px threshold): the row follows the mouse, an accent line shows where it will
    land, the list scrolls near its edges; the order is what a canvas click picks first.
  - Keyboard while the list has the focus: Up/Down move the selection (stopping at the ends),
    Return opens the settings, Delete removes the object and selects its neighbour, Esc
    deselects.
  - Right-click a row: the object's menu, as on the canvas.
  - Hiding, showing, reordering, deleting and clearing the paint are one undo step each;
    selecting (in the list or anywhere) is none.
- An object's settings start with a breadcrumb, "< Layers / Text": "< Layers" (dim, raised on
  hover) goes back to the list with the object still selected. New objects (Add text, Add
  image, a dropped picture) go in front and are selected, with their settings shown.
- Text selected: title "Text"; multi-line text field (live updates, one undo step per typing
  session); Font (combo of art fonts); Style (segmented Solid | Outline); **Band** (segmented
  Low | Mid | High | Full, highlights the matching band if the object sits exactly on it);
  Position: "Fill length" button (sets time 4 %..96 %); Intensity slider (0..100 %; a drag, or a
  run of mouse-wheel steps without a 1 s pause, is one undo step); Delete button.
- Image selected: title "Image"; thumbnail of the processed result; Mode (segmented Auto |
  Original | Negative | Outline, with Auto showing what it picked, e.g. "Auto (Negative)");
  Clean-up slider (0..90 %); Band; Fill length; Intensity; "Replace..."; Delete.
- Frequency read-out of the selected object ("3.5 kHz - 16 kHz") in mono.

### Footer

- Three rotary knobs with value read-outs: Density ("256 lines"), Contrast ("40 dB"),
  Level ("-6.0 dB"). Drag, Shift = fine, double-click = default, double-click value = type.
- Length combo; Trigger segmented (Transport | MIDI) with a Repeat switch beside it (a
  one-segment control with a repeat icon, lit like a chosen segment when on); Tempo segmented
  (Host | Manual) with a BPM field (editable when Manual; shows the host tempo greyed when Host).
  Columns at 100 %: Length 96, Trigger 124 + 6 + Repeat 78, Tempo 108 + 8 + BPM 56, 16 apart,
  ending 24 before the Preview column.
- The hint line under each column says what it does: "8 s per pass"; under Trigger "Plays once
  from bar [1]", "Repeats every 4 bars from bar 1", "Each note plays it once" or "Loops while a
  note is held"; "From the DAW" / "Typed in". In "Plays once from bar [1]" the number is the
  start bar field (shown only for Transport without Repeat): the BPM field's look at 20 px tall
  with 12 px mono figures, as the knobs' values in the same line; drag up/down (Shift = fine),
  click to type, Enter commits, Esc cancels, out-of-range values clamp to 1..999.
- Preview: a round play/stop button (accent2 when on). Plays the drawing from its start, on a
  loop with Repeat, else once (then it turns itself off).
- Export: "Bounce WAV" renders one pass in the background ("Rendering..." on the button); then a
  draggable chip names the file on two lines ("ArtFreq HELLO" over "14-03-27.wav"), plus "Save..."
  (async FileChooser).
  - **Names.** Every bounce is a new file in <temp>/ArtFreq, "ArtFreq <first text, or Drawing>
    <HH-MM-SS>.wav" (the text cut to letters, digits, spaces and `-_()'&+!`, at most 20
    characters; " 2", " 3" ... in the same second). The file is created when its render starts,
    which reserves the name, so no two bounces (of this instance or another) ever share one. DAWs
    (Logic, Live, FL Studio) know dropped audio by its path and name and keep what they read the
    first time: a file rewritten under the same name (0.1.0 wrote ArtFreq.wav every time) plays
    the old audio after the second drag.
  - **Always fresh.** From the first click until the window closes, the file follows the drawing.
    A change to the drawing, length, tempo, level (or the sample rate) turns the chip into
    "Updating..." at once: it cannot be dragged, Save... is off, a thin line shows the render's
    progress. That includes the moment between an edit and the processor's timer rebuilding the
    plan (`isPlanPending`), and a drag asks again when it starts, so the chip never hands out an
    older drawing. A render that is no longer wanted is stopped; once nothing has changed for
    400 ms the new file renders in the background and takes the chip, with its new name. Renders
    start by themselves only while the export can be seen; nothing renders before the first click.
    An empty drawing shows "Nothing drawn". A failed render brings the button back under the error
    (it is not retried by itself until something changes).
  - **Save...** saves the current file; chosen while an update is on its way, it saves the update
    once it is in.
  - **Clean-up.** A file is deleted when the next one takes over and when the window closes,
    unless it was dragged out (a DAW may read it where it is); ArtFreq*.wav files a day old are
    deleted when an editor opens. Closing the window stops running renders (the render checks once
    per block) instead of waiting for them.

### Header

The emblem (24 px, `ui/Emblem.h`, image in `docs/artfreq-emblem.png`): a capital A and F as
one monogram (the A's right leg is the F's stem, its apex runs into the F's top arm, its
crossbar carries on as the F's middle arm), solid white on the left and dissolving to the right
into horizontal lines in the spectrogram's colours with growing gaps, as a drawing looks once it
has become sound. Vector paths, no fonts; the number of lines follows the size in physical
pixels (whole-pixel lines and gaps at header size, about 30 lines when large).
`ArtFreqEmblemLab <dir> [--docs file.png]` renders it to PNGs. Then the wordmark "ArtFreq"
(Inter SemiBold 20; "ArtFreq FX" in the effect, FX in accent2) and "by AnyMeans" (11 px dim). Segmented Canvas | Live (in the effect beside the wordmark, with Mode and
Depth after it: see [ArtFreq FX](#artfreq-fx)).
Undo / Redo icon buttons (also Cmd+Z / Cmd+Shift+Z). "?" opens the help panel; gear opens
settings. Icons are drawn with juce::Path (no image assets).

### Settings panel (overlay card, closes with Esc / outside click)

"Match your analyser": Scale (Log | Linear | Mel), Low Hz, High Hz (editable numbers), Slope
(0 | 3 | 4.5 | 6 dB/oct) with a note "MiniMeters: Log, 20 Hz - 20 kHz; set Slope to match its
Slope setting". Window size (75 % | 100 % | 125 % | 150 % | 200 %). Reset drawing (asks inline).

### Help panel (overlay)

Short numbered steps: put ArtFreq on an instrument track; open a spectrogram (MiniMeters) on the
output; match scale/range in Settings; add text/image/paint; choose a band and density; press
play in the DAW (or Preview, once or on a loop); Trigger modes and Repeat explained (nothing
plays by itself); bounce to WAV. Layers: the list (select, settings, reorder, hide) and
overlapping objects (Alt-click or click again, the right-click menu). Plus tips: highs show the
sharpest detail; the lowest octaves are coarse in every analyser; Lows (Smooth fills shapes
below about 2 kHz, Stripes plays separate lines there); raise Density for solid shapes, lower for
a scan-line look.

## Tests

`ArtFreqTests` (no host): scale maths; document round trip; image modes (auto-invert on a white
background, transparency, outline); text rendering coverage; compositor placement; plan
(frequencies, spacing, normalisation, contrast, slope); Smooth lows, the rising comb (the lines it
replaces, its loudness grid and softened edges, thin strokes played steady on the replaced lines
and drawn as Stripes draws them, the fade-in at the bottom without clicks, splashes or extra rumble
at low ends of 10, 20 and 30 Hz, a long-FFT analyser seeing the band filled, its power staying in
the drawn band, leakage, determinism, crossfades, speed, the canvas drawing the band filled and as
bright as the lines); exchange (versions, garbage); engine
(single line frequency, silence, crossfade continuity, jump fades, Nyquist guard, real-time
speed); tracker (transport positions, tempo changes and ramps, bar lines, the single pass from a
start bar: its start and end, starting the DAW inside it, relocations before, after and into it,
a host loop over it, the host's meter and tempo, time-signature changes before, at, inside and
after it and odd bars, a typed-in tempo starting on the DAW's bar, its fades through the engine;
MIDI one-shots and their end, hold-to-loop with overlapping notes and releases, all-offs and stop;
preview once and looping; jumps); end-to-end: a canvas rendered to audio and analysed back must
correlate with the canvas, and at FFT 8192 and 16384 Smooth must fill a block and the text "LOW"
where Stripes leaves gaps, correlate better with the drawing, and keep the band's level steady (no
regular pulse); processor (state round trip, defaults, Lows on Smooth for new instances and on
Stripes for states from before it, switching Lows back and forth while playing without a click,
states from before Repeat and the Free-loop migration, a single pass and a held MIDI loop through the processor, a single pass
through time-signature changes and at a typed tempo, preview following Repeat, process with MIDI,
panic, bypass fades, time signature, the canvas's song bars, level/clipper bound); WAV export (cancelling it, its
progress, bounce file names);
the editor, driven with synthetic events (bounce files; the export following the drawing:
after each change a new file that matches a render of the new plan, the chip's "Updating...",
stopped renders, saving the current file; undo steps of wheel and arrow gestures,
clicking and placing objects, the layer list: selecting, the keyboard, hiding, reordering,
deleting, the paint row; clicking again, Alt-click and the right-click menu on the canvas; the
live view's span, unreadable pictures; under a virtual display on Linux). Hidden layers: the
document round trip (order, visibility, file names, old documents shown) and the compositor
leaving them out. Carving: the mask plan (rows to Hz, columns, band averages, clipping, the
halo), mask plans through the exchange, and the baseline engine (bit-exact with nothing to
carve, outside the pass and with nothing drawn; the input back to 1e-5 through the transforms
where the mask is 0; a sine in a drawn band carved by the depth within 3 dB, one outside it
untouched; Stencil; the latency, also through the transforms and at 96 kHz; the same output for
any block sizes; the gate opening with the pass), and the tuned engine (the same, at 48 and
44.1 kHz and for a window pair that needs the overlap normalised; half an octave at 70 Hz
carved by nearly all of Depth and a stroke thinner than a bin by its share; the gate at the
output: exact before and after a pass, the carve complete from its start to its end; a stroke
between two frames' centres carved at a one-beat length; columns read alike at their edges
whatever the host's rounding; the halo per mode and holdLoudest; the wide dilation against a
direct maximum). ArtFreq FX (`EffectTests.cpp`, through the real processor): its name, buses and
parameters, the latency at 44.1 - 96 kHz in every mode, the audio bit for bit outside the pass,
with the DAW stopped and with nothing drawn or everything hidden, Carve and Stencil on a sine in
a drawn band at -80 and -40 dB, Carve + Draw's wider hole, its sines and Level, Draw adding
exactly the instrument's output one latency late, bypass keeping the latency (and no clicks into
and out of it), every mode switch and Depth jumps without clicks, states between the two
variants, mono and stereo, 44.1 and 96 kHz, block sizes from 1 to 4096 (the carve exact, the
sines as the instrument's), MIDI and Preview with the DAW stopped. Its editor
(`EffectUiTests.cpp`): Mode and Depth in the header and not overlapping, automation moving
them, what each mode greys, the export's hint, both help panels fitting the window, and the
canvas in each mode (black where Carve carves at -80 dB, Stencil the other way round, Carve +
Draw's halo dark and its sines bright, nothing drawn leaving the stand-in).

`ArtFreqSnapshot <dir>` renders the editor in its main states to PNG (ArtFreq FX's as
`artfreq-fx-*.png`: each mode, a shallow Depth typed in, the Mode tooltip, the live view of a
stand-in song carved by the real processor, the help, 75 / 100 / 200 %, with checks of the
header's layout, Mode and Depth driven with the mouse, what they grey, the export's hint, Preview
carving the audio coming in; and the instrument's: Repeat, MIDI with Repeat,
a start bar and typing one among them, "layers": five objects, one hidden, one selected from the
list, and the window at 75, 100, 150 and 200 %), and checks the editor's controls by driving
them: the canvas interactions, the layer list, Lows (at the foot of the list, clear of the keys
and the brush settings; Stripes and back changing the plan and the canvas; hidden with an
object's settings), and the footer's Trigger, Repeat, the start bar field (drag, type, Enter,
Esc, clamping), the hint line, Preview stopping by itself without Repeat, and the layout.

`ArtFreqLab <dir> [--lows stripes|smooth]` (tools/LowsLab.cpp) is the bench the lows were chosen
on: it renders four drawings through the real plan and engine, analyses them like a spectrogram
(Hann and Blackman-Harris, 4096 to 32768 points, interpolated and nearest-bin) and measures, over
30 Hz - 2 kHz, fidelity, fill, gap depth, flicker, regular pulsing and leakage, with PNG sheets
and a comparison against an earlier run (`--baseline`).
