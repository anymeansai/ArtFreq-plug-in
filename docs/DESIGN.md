# ArtFreq: design

ArtFreq is an AnyMeans instrument plug-in (VST3, macOS and Windows) that turns text, pictures and
brush strokes into sound whose **spectrogram shows them**. Open a spectrogram (MiniMeters, or
any analyser with a scrolling spectrogram) on the output and the drawing scrolls past. The same
code is also built as **ArtFreq FX**, an audio effect that carves the drawing into a song (see
[ArtFreq FX](#artfreq-fx)).

## How it works

1. **The canvas is a picture of the spectrogram.** x is time across the drawing's *length*
   (1 beat to 32 bars, synced to the tempo); y is frequency on the *same scale as the user's
   analyser* (logarithmic by default, like MiniMeters; linear and mel are offered), between
   20 Hz and 20 kHz by default. What the user sees on the canvas is what the analyser will show:
   not an ideal picture of the drawing, but its real sound, rendered and analysed as that
   analyser does (see [Measured preview](#measured-preview)).
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
   by the standing comb, fine steady lines, instead (see [Smooth lows](#smooth-lows)), and every
   line's loudness is smoothed along time over a few cycles of its own sine, so low lines come
   and go without clicking.
5. **The engine** (`dsp/ArtEngine.h`) plays the plan on the audio thread: one sine per line
   (the comb's lines locked in phase to its sample clock), amplitudes interpolated across columns, click-free fades on
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
- **The measured preview** renders and analyses one pass on a background thread of the canvas's
  own (low priority, started the first time a render is wanted, stopped with the editor), from
  the same shared plans and masks; a newer request stops it within a block. Only its finished
  picture comes to the message thread (a row-by-row copy into an image).

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
| startAt | Start at | On play, At bar | On play (see below for states from before it) | no |
| startBar | Start bar | 1..999 | 1 | yes |
| tempoSource | Tempo | Host, Manual | Host | yes |
| bpm | BPM | 20..300 | 120 | yes |
| scale | Analyser scale | Log, Linear, Mel | Log | no |
| rangeLow | Analyser low | 10..1000 Hz | 20 Hz | no |
| rangeHigh | Analyser high | 2000..24000 Hz | 20000 Hz | no |
| slope | Analyser slope | 0, 3, 4.5, 6 dB/oct | 0 dB/oct | no |
| analyserFft | Analyser FFT | 4096, 8192, 16384 | 8192 | no |
| analyserFloor | Analyser floor | -140..-40 dBFS | -90 dBFS | no |
| analyserCeiling | Analyser top | -60..+12 dBFS | 0 dBFS | no |
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

Analyser FFT, floor and top were added after 0.5 (version hint 6): how the user's analyser draws
(its FFT size, and the levels its darkest and brightest colours stand for), so the canvas and the
live view draw the sound as it does. They change nothing in the sound, and like the analyser
match they are settings, not automation targets. A state without them loads the defaults (an
older project looks as a new one does). The view the canvas uses spans at least 20 dB
(`AnalyserView::sanitised`: a floor typed above the top keeps the floor).

Lows was added after 0.2 (version hint 3). A saved tree without it was made with the stripes,
the only look there was, and loads with Stripes rather than the default Smooth, so an old project
looks and sounds as it did (`prepareLoadedParameters`, before the defaults for other missing
parameters are filled in).

Start at was added after 0.4 (version hint 5). Until then a single Transport pass always
started at its start bar, and bar 1 was the default: press play anywhere later in the song and
nothing sounded. A saved tree without it loads by its start bar: bar 1, the default nobody
chose, as On play; any other bar was chosen, and loads as At bar, so that project plays where it
did (`prepareLoadedParameters`, with the Lows rule, before the defaults for other missing
parameters are filled in; a tree with neither parameter, 0.1's, is On play). It is how the
plug-in is used, not something to perform, and switching it while the DAW plays begins a pass:
not an automation target.

State = APVTS tree + document tree + editor size, saved as XML in binary (`copyXmlToBinary`).
Neither the drawing nor the editor size is a parameter, so edits, undo/redo and resizes tell the
host the project changed (`updateHostDisplay` with `nonParameterStateChanged`); loading state
does not. Each picture is PNG-encoded once and its text kept while any document or undo step
still holds the picture (a loaded picture keeps the text it was loaded from), and a load keeps
the loaded tree as the state to save, so saving after an edit or a load encodes nothing again.

## Trigger

When the drawing plays is the Trigger (Transport | MIDI note) and Repeat (off by default). The
producer's rule: nothing plays by itself, and loops only while something is playing. And, after
0.4: press play and it plays. The defaults (Transport, Repeat off, On play) make sound, or carve,
from the moment the DAW starts, wherever the playhead is.

| | Repeat off | Repeat on |
| --- | --- | --- |
| **Transport** | Once, from where the pass starts to *Length* later, following the DAW's timeline. **On play** (the default): it starts where the DAW starts playing. **At bar**: it starts at the start of bar *Start bar*; start the DAW inside it and it plays from there, before and after it is silent. Either way a DAW loop over it plays it each time round. | Loops every *Length*, on the host's bar lines (bar 1, 1 + Length, ...), while the DAW plays. |
| **MIDI note** | Each note-on plays it once from the start (a new one restarts it); note-offs are ignored. | A note-on starts it (a new one restarts it) and it loops while any key is down; letting go of the last fades it out. |

**On play** (`StartAt::play`; the rules are in `PlayheadTracker.h`, the tests in
`trackerOnPlay...`):

- A pass begins at the first sample of the block in which the DAW starts playing, wherever the
  song is. Also in the first block the plug-in sees while the DAW is already playing: put on the
  track during playback, after `reset()` (which is how a host that suspends a plug-in says so:
  JUCE calls it from VST3's `setProcessing (false)`), coming out of bypass, and when Repeat, At
  bar or the MIDI trigger is switched back to it while the DAW plays (switching to it counts as a
  start). It lasts Length in the time signature of that block.
- From there it follows the song exactly as the pass at a start bar does: the host's ppq at the
  host's tempo (changes and ramps bend it, they do not move it); with a typed-in tempo it starts
  at the DAW's place and plays at the typed tempo, counted on; without a ppq it is counted from
  the host's time. It plays once, with the same gate fades, and then it is silent.
- The DAW moving while it plays (a relocation, or its loop wrapping round: a position that does
  not follow on from the last block) **forwards** to a place inside the pass carries on at that
  place in the same pass. Moving **back** begins a new pass right there, unless it lands on the
  pass's own start (give or take the jump tolerance, 5 ms: hosts round), where it carries on; so
  does moving to **before its start** or **from its gate's close on** (where it can no longer
  sound: its end). So every round of a DAW loop draws the drawing from its start at the loop's
  start, wherever play was pressed: the same first part every round under a loop shorter than the
  pass, the whole drawing and then silence under a longer one. (Until 0.4's review, a wrap that
  landed inside the first pass carried on in it: with play pressed before the loop, as Cubase,
  Reaper, Studio One and Bitwig do from the cursor, every round drew only the drawing's second
  half, and the picture under a fixed loop depended on where play had been pressed.) The first
  round starts where play was pressed; it is only from the second that rounds are alike.
- ArtFreq FX carving a song (Carve and Stencil: `TrackerSettings::waitForSound`) has nothing to
  show over silence, so a pass that is due begins at the first sample, from that place on, where
  the input sounds (-60 dBFS on any channel: quieter than that, nothing of it shows on an
  analyser, and carving it would show nothing either; digital silence, dither and a quiet noise
  floor lie below it). The customer: "If I put it on a track and press play, a lot of times it
  doesn't do anything": pressed at bar 1 on a vocal that comes in at bar 9, the pass ran out over
  the silence. Until the audio comes in nothing plays, the processor says so
  (`isWaitingForSound`) and the bottom bar reads "Waiting for audio to carve"; a relocation
  meanwhile changes nothing, and stopping forgets it. Once the pass has begun, silences in the
  input change nothing. The Draw modes, which make sound of their own, and the instrument begin
  at once. The level is read from the input before the carver delays it, and the carver's
  segments are on the input's timeline, so the carve begins with the audio.
- The DAW stopping forgets the pass: every start is a new pass, also from the same place.
- It never begins before the song does. At a negative position (a count-in before bar 1) it
  begins at 0 and the count-in is silent. A pre-roll inside the song cannot be told from
  playback (VST3 has no flag for it): the pass begins where the DAW starts playing. So does a
  DAW's export, freeze or bounce in place, at its first block (in Carve and Stencil, where the
  input first sounds after it): a render is a playback from where it starts, not from where the
  drawing was heard while playing from bar 9. Nothing is guessed (a render looks like any other
  playback, and placing it elsewhere would make a bounce differ from pressing play there): the
  README, both help panels and the Start choice say to choose At bar before exporting.
- A time-signature change while it plays moves and stretches nothing (it lines up with no bars).
  A new Length changes its length at once, its start staying put: the position jumps (with the
  jump fade), or the pass is over.
- Under the preview the song is still followed, unheard, so the pass keeps its place and the
  preview ending does not start it again.
- A host that gives neither ppq nor time: it begins at the first playing block and runs on at the
  tempo, once; relocations cannot be seen (stopping still forgets it).
- What it cannot know: a host that stops calling the plug-in while it is stopped, without telling
  it (`setProcessing`, `setActive`), and starts again exactly where it stopped looks like playback
  that never stopped: a pass that was over stays over. No clock is consulted to guess (a slow
  offline bounce would look the same, and would draw the picture twice). A host that stops
  calling an effect while its input is silent (Cubase's "Suspend VST3 plug-in processing when no
  audio signals are received", Reaper's auto-bypass on silence) and resumes at the next audio
  would look like a relocation past the pass, and a second pass would begin there: so ArtFreq FX
  reports an infinite tail (`getTailLengthSeconds`, VST3 `kInfiniteTail`), and such hosts never
  suspend it (it makes sound of its own over silence, too: the Draw modes' sines). The instrument
  needs no input and is never suspended for its silence.
- The canvas numbers the drawing's own bars, 1 to N, as with Repeat.

**At bar** (`StartAt::bar`): what 0.2 to 0.4 always did.

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
- Its place belongs to the song, so `reset()` and stopping keep it; the canvas numbers the song's
  bars.

Both:

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
- Changing Trigger, Repeat, Start at or the start bar while playing moves to wherever the new
  setting puts the position, with a jump fade when it does not follow on: nothing clicks (turning
  Repeat off with At bar inside the single pass, at the same place as the loop, carries on
  seamlessly; with On play it begins a pass, from the drawing's start).
- ArtFreq FX uses the same tracker: Carve starts on play too.

## Smooth lows

The customer, after 0.1: the stripes in the lows are a nice effect, but they lose a lot of a
picture. On a log scale the lows hold few lines (merged to 25 Hz apart, as closer lines beat and
pulse), and an analyser with a long FFT (MiniMeters at 8192 and up) resolves them into stripes
with dark gaps: low-band fill 0.38 at 8192, 0.21 at 16384. From 0.3 to 0.6 **Smooth** filled the
gaps with a *rising comb*: sines 20 Hz apart gliding upwards at 200 Hz/s, each taking its
loudness from the drawing at the height it had reached. It filled, and it dripped. The customer,
on 0.5: "Lower frequencies are all distorted. Lower it gets worse. There's like a 'drip' from the
image or text that drips into the lower frequencies, even if the canvas doesn't have anything
drawn down there." Measured (`ArtFreqLab --drip`, below): under a block drawn from 60 Hz the
gliding comb left 33 dB below the block's energy all the way down to 20 Hz, a dot at 40 - 65 Hz
came out as an upright streak from 20 to 400 Hz, and a thin wavy stroke as a staircase with a
streak under every step. Four causes, and what replaced each:

- **Gliding sines have to be switched.** A sine that glides through a shape is turned on as it
  enters and off as it leaves, and that switching is amplitude modulation: it sprays energy far
  outside the shape, a pulse train at the comb's pass rate. **The standing comb**: the lows are
  played by steady lines on a fixed grid instead, every multiple of 5 Hz up to 500 Hz and every
  multiple of 10 Hz above (232 lines at Density 256 where the gliding comb had 93 sines; the band
  is the same: the crowded lows, about 20 Hz - 1.8 kHz at Density 256, up to 2 kHz at 1024, none
  on a linear scale). A line that is not inside the drawing is silent, so nothing is played
  below the lowest drawn frequency or above the highest, by construction.
- **Why steady lines that close do not pulse.** Lines 5 Hz apart beat against each other; with
  unrelated phases a band of them is a noise that throbs. With the right phases a band of them
  is, exactly, a comb of sines gliding upwards (a sum of chirps shifted in time is a line
  spectrum: Poisson's formula), which an analyser's window of any usual length sees as an even
  level. Line k of a grid (its frequency over the spacing) starts at -pi k^2 / N, with N = 2.618
  (the golden ratio squared) the lines the sweep passes in one beat period. Whole and half
  numbers (2, 3.5, 4) make all the beats line up into a train of clicks, 5 to 10 dB taller (crest
  factor of a block: 15 - 20 dB against 9); with the golden number no two beats ever line up.
  Measured against other N, random phases and the gliding comb (same fill and flicker at 4096
  and 8192, steadier at 16384 and 32768), below. The grid is 5 Hz where an analyser of 16384
  or 32768 points could otherwise tell the lines apart on a display's rows (below 500 Hz) and 10
  Hz above, which keeps the worst-case CPU where it was.
- **The phases need a clock.** The pattern only works if every line keeps its place in it, but
  the engine lets silent oscillators stand still. So the comb's lines are locked to the samples
  rendered since `reset()`: whenever one starts to sound (always from an amplitude of 0), its
  oscillator is set to the phase it would have had if it had never stopped
  (`ArtPlan::combPhases`, `ArtEngine::gather`; a cosine and a sine per line that comes on, no
  more). The gliding oscillators, their SIMD kernel and their buffers are gone.
- **Every line was switched in a few ms.** A sine turned on between two columns (7.8 ms at the
  default length) is for that moment a click as wide as one over the time it took, and a little
  of it much wider: an octave of "legs" under every edge at 100 Hz, faint streaks to the bottom
  of the picture under text in the mids, in Stripes as in Smooth. **Fades in time**: each line's
  loudness is smoothed along time in the plan (message thread; no cost on the audio thread) with
  a raised cosine as long as 12 cycles of its own sine, at least 40 ms (five columns at most, so
  a short drawing keeps its edges) and at most 0.25 s and a sixteenth of the pass. 8 cycles leave
  legs 15 dB stronger; 20 gain 5 dB more on blocks and nothing on text, and soften low letters.
  The 40 ms take the streaks under mid text down by 4 dB below it and 11 dB above (25 ms: 1 and
  7 dB); they are shorter than any analyser's window, so the edges look as they did. With Repeat
  the smoothing runs round from the last column into the first; without, the pass is surrounded
  by silence and the lines also reach silence at its two ends. Long passes have long columns (31
  ms at 16 bars and 120 BPM), and a fade two or three columns long is drawn with as many straight
  strokes, whose corners spread a low line's energy again: a line whose 12 cycles are longer than
  one column fades over four columns at least (`coarsestFadeColumns`; the low text at 16 bars:
  2 dB less below the letters, 5 dB less above), and a line whose 12 cycles fit into one column
  is left to the engine's glide from column to column, so the highs keep every column as drawn.
  The plan therefore knows how long
  a pass takes (`PlanSettings::passSeconds`, from Length and the tempo to the nearest 3 %, so a
  tempo ramp rebuilds it now and then) and whether it repeats.
- **Rows were played by the nearest line, wherever it was.** In the lows the nearest line can be
  a third of an octave below the drawing (Stripes: a shape drawn from 60 Hz also sounded at 47).
  **Never below the drawing**, in both modes: a low line sounds only if the drawing covers its
  own frequency. Looked at between each pair of neighbouring lines: where one line's own row is
  drawn and the other's is not, and the drawing runs on unbroken from the drawn line to where
  their spans meet, it is a shape's edge: the drawn line plays all of it and the other stays
  silent. A stroke that touches neither line's row is played by one line, so nothing drawn
  disappears: the nearer one, and where the stroke straddles the place where the two lines'
  spans meet, the line that has more of its rows. (Played half by each, as the spans would have
  it, the two lines beat against each other at full depth: five times a second in the comb, a
  stroke like a row of beads with a tremolo to it, which was one in six of all strokes 2 to 6
  rows thick between 40 and 300 Hz. `ArtFreqLab --skeptic` builds a stroke at every height from
  40 Hz to 1.7 kHz, 1 to 24 rows thick, and counts none on two lines and none lost.)
- **Thin strokes.** Two of the comb's lines on their own would beat as one pulse, five or ten
  times a second (14 dB on an analyser of 4096 points). Where drawn rows join two lines and no
  third, the louder plays for both, steadily; three lines play with the outer two as if drawn at
  half the value (20 dB down at the default contrast: nearly one line, with a hint of the
  stroke's height; 4 dB of shimmer at 4096 points, 3 dB at 8192). The narrowest runs played as they are (four or
  five lines, 20 Hz) shimmer by 6 dB at 4096 and 3 dB at 8192, more than the gliding comb's did
  (4 and 2 dB: its sines were 20 Hz apart, exactly one in such a stroke at any time, which is
  also why it had to be switched); from six lines on, as any shape does.
- **Its brightness.** Lines closer than an analyser's bins add up in each bin, so each comb line
  is played at combGain (1.5) times the square root of its spacing over 20 Hz: the power per Hz
  the gliding comb had. As bright as the lines above at 8192 points, about 3 dB brighter at 4096,
  about 3 dB darker (but filled) at 16384 and 32768. The normalisation counts the comb's lines as
  any others.
- **Cost.** Density 256: 232 comb lines + 88 lines (the gliding comb: 93 sines + 88 lines;
  Stripes: 142 lines); Density 1024: 247 + 295 (101 + 295; 366). Worst case, everything drawn,
  one core of the Linux build machine, x real time (gliding comb -> standing comb): Density 256,
  8 s pass 252 - 260 -> 236 - 242; 0.5 s pass 123 - 126 -> 126 - 142; Density 1024, 8 s 139 - 145
  -> 140 - 147; 0.5 s 69 - 73 -> 80 - 86 (Stripes: 480 - 510, 300 - 330, 200 - 210, 120 - 127).
  A drawing that fills the lows and nothing else costs about a tenth more than it did (the lab's
  block from 30 Hz to 2 kHz: 2.2 -> 2.5 ms per second of sound, a quarter of a percent of a core;
  its text 1.5 -> 1.4, its disc 0.8 -> 0.8).
  buildPlan in its worst case (Density 1024, noise on the whole canvas) 7.5 -> 10.5 ms; typical
  drawings 5 - 9 ms as before.
- **What a project saved with 0.5 or 0.6 does.** It opens as it was (Lows is a saved choice, the
  parameter unchanged) and sounds cleaner: the same shapes, as bright, filled as before, with
  nothing under them.

**Measured: the drip** (`ArtFreqLab <dir> --drip`, tools/DripLab.h: eight drawings with
something low in them at Density 256, played looped by the real engine; a 65536-point
Blackman-Harris analysis, which adds nothing of its own; the share of the sound's energy more
than a sixth of an octave below the lowest frequency drawn under each window, in dB, and in
brackets the loudest cell there against the loudest cell of the drawing):

| drawing | Smooth 0.6 | Smooth now | Stripes 0.6 | Stripes now |
| --- | --- | --- | --- | --- |
| one row at 100 Hz (a single tone) | -30.7 (-30.5) | -50.1 (-42.3) | -30.6 (-30.4) | -49.8 (-42.2) |
| block 60 - 250 Hz | -32.8 (-19.0) | -77.3 (-64.2) | -42.6 (-33.4) | -75.7 (-70.1) |
| LOW in the Low band | -24.2 (-12.7) | -57.6 (-39.9) | -26.7 (-20.9) | -40.5 (-28.6) |
| thin wavy stroke 130 - 350 Hz | -28.5 (-27.6) | -45.6 (-43.3) | -30.0 (-29.9) | -46.0 (-35.4) |
| dot 40 - 65 Hz | -15.6 (-18.7) | -67.0 (-63.0) | -22.8 (-25.9) | -56.9 (-59.0) |
| frame, bottom bar 80 - 90 Hz | -40.9 (-28.7) | -69.6 (-60.8) | -9.9 (0.0) | -71.8 (-64.0) |
| MID in the Mid band | -52.0 (-36.4) | -66.3 (-55.2) | -49.7 (-39.0) | -66.1 (-58.4) |
| portrait 40 Hz - 2 kHz | -43.8 (-33.5) | -74.3 (-60.6) | -39.0 (-30.9) | -73.7 (-64.2) |

What is left, and why. A tone that starts and stops is as wide as one over the time it takes to:
with fades of 12 cycles, a line's energy lies within a sixth of its frequency, part of it just
past the sixth-of-an-octave mark (the single tone's -50 dB; the lowest lines of the text, where
the fade is capped at 0.25 s). Above the drawing the same eight read -44 (the wavy stroke) to
-91 dB (-19 to -79 with the gliding comb). A thin stroke that moves in pitch is played by one fixed line
after another, each for a moment, and two fixed sines cannot hand over as one gliding sine would:
faint arcs run off the stroke where it bends (-46 dB; the gliding comb played the same stroke as
a staircase on Stripes' lines, with a streak under every step, -28 dB). Stripes' frame: its
bottom bar, 80 - 90 Hz, lies between two of its lines (72 and 97 Hz) and is played by the upper,
which has more of it (0.6 played it half on each, and the lower read as -10 dB "below the
drawing"); Smooth has lines inside it.

**Measured: the fill** (ArtFreqLab, Hann, interpolated, mean over four drawings and both
densities; low = 30 - 250 Hz / mid = 250 Hz - 2 kHz):

| FFT | | fidelity | fill | gap dB | flicker dB | leak dB |
| --- | --- | --- | --- | --- | --- | --- |
| 4096 | Stripes | .84/.93 | .93/.86 | 4.2/6.9 | 1.2/1.1 | -9.7/-16.8 |
| | gliding comb | .89/.96 | .93/.92 | 4.6/4.7 | 2.9/2.8 | -15.4/-22.7 |
| | standing comb | .90/.96 | .93/.93 | 4.5/4.2 | 2.9/2.6 | -15.9/-22.7 |
| 8192 | Stripes | .69/.76 | .38/.53 | 17.9/24.7 | 0.9/0.8 | -9.7/-17.1 |
| | gliding comb | .90/.95 | .93/.93 | 4.4/3.7 | 2.7/2.3 | -17.3/-21.3 |
| | standing comb | .92/.96 | .93/.94 | 4.3/3.6 | 2.9/2.0 | -18.6/-21.7 |
| 16384 | Stripes | .43/.57 | .21/.21 | 18.5/28.0 | 1.3/0.6 | -10.7/-16.4 |
| | gliding comb | .88/.93 | .88/.91 | 5.8/4.1 | 3.7/2.1 | -17.8/-18.3 |
| | standing comb | .91/.94 | .91/.93 | 4.7/3.4 | 2.8/0.9 | -19.3/-19.0 |
| 32768 | Stripes | .31/.49 | .30/.21 | 12.5/19.8 | 1.7/0.8 | -11.1/-14.8 |
| | gliding comb | .84/.90 | .81/.90 | 7.1/3.9 | 3.8/2.0 | -17.4/-15.4 |
| | standing comb | .85/.90 | .79/.90 | 7.5/3.2 | 1.7/0.1 | -18.8/-17.0 |

(The Stripes rows are 0.6's; Stripes now, with its lines kept inside the drawing and the fades:
fidelity .87/.92, .70/.72, .45/.54, .32/.46, fill .89/.81, .33/.38, .18/.16, .28/.21, leak -12.6
to -18.4 in the lows. The lab's leak is measured with Hann at each size and is mostly the
analyser's own blur around shapes, which is why it moves little: the drip table is the measure
of what the sound leaves outside the drawing.) At 32768 points the comb's own lines,
5 Hz apart, begin to show below 500 Hz as fine stripes (a gap of 7 - 8 dB, as the gliding comb's
shimmer left); they do not move.

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
  Length, Trigger, Repeat, where the pass starts and tempo exactly as the sines do; the gate fades over
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
  same class and asks the processor which it is: it puts the instrument's shell or the effect's
  own (see "The editor" below) around the same canvas, live view, inspector and panels. Each
  CMake target (`artfreq_add_plugin`) adds only its own `createPluginFilter` (`src/plugin/`).
  The effect has stereo or mono in and out (as many out as in), takes MIDI (hosts that send
  effects MIDI can trigger it), and reports one carver frame of latency.
- **Parameters.** Every instrument parameter, plus **Mode** (Carve, Carve + Draw, Stencil, Draw;
  default Carve) and **Depth** (-120..0 dB, default -80), both automatable and both acting on
  the audio thread at once: the exact and the halo masks are built together (`MaskPlanSet`) and
  Depth is read per frame. -80 dB: a master's bass and kick sit around -15 dB in an analyser's
  cells, so it takes 75 - 80 dB to reach a spectrogram's floor near -95 dB (the judge's sweep:
  visibility 58 / 60 at FFT 4096 / 8192 against 46 at -60; the cost, frequency edges 2.3 display
  rows wider and a little more kick smear).
- **What it does per block.** The tracker makes the segments (Length, Trigger, Repeat, On play or
  At bar, tempo, MIDI, Preview: exactly the instrument's; so with the defaults a carve starts when
  the DAW starts playing), the carver (engine 1) carves the block in
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
- **Tail: infinite**, so a host never suspends it over silent input (see [Trigger](#trigger),
  "What it cannot know"; `effectKeepsRunningOverSilence` plays a host that does). It was the
  latency plus 50 ms: a suspending host skipped a gap in the track and resumed at the next audio,
  where a second pass on play began.
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
- **Preview** (the processor's `setPreview`) runs the pass from the start on whatever audio comes
  in, also while the DAW is stopped (most hosts keep feeding effects), once or on a loop with
  Repeat, and plays the sines in the Draw modes: the same as the instrument's Preview, on the
  song. The effect's window has no button for it (see below): with the DAW stopped there is
  mostly silence to carve. The processor keeps it, and its tests.
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
  the `FreqArt*` tags of earlier versions are read too. The installers remove old `FreqArt.vst3`
  and `FreqArt FX.vst3` bundles from the system VST3 folder (the macOS one once their
  replacements are in, the Windows one just before it copies them), so a DAW does not list each
  plug-in twice.
- **The editor** is a small window of its own (`ui/EffectShell.h`, and the compact layout of
  `Content` in `ui/Editor.cpp`). Until 0.4 it was the instrument's 1120 x 720 window with Mode
  and Depth added and whatever did not apply greyed. The customer: "ArtFreq FX looks just like
  regular ArtFreq but has a lot of controls greyed out and you can't change. You should make
  ArtFreq FX be a lot more compact and easy to use since it doesn't need all the extra controls.
  Almost like it's own stand-alone smaller plug-in." So: **720 x 440** at 100 % (540 x 330 to
  1440 x 880: the same 75 to 200 %, fixed aspect ratio, corner resizer and Settings choice as
  the instrument), two bars with the canvas between them, and one rule: **nothing is greyed;
  what does not apply is not shown.** (The snapshot tool walks the window's component tree in
  every state and fails on a visible component that is disabled. The rule reaches the one
  menu with items that can have nothing to do: an object's right-click menu lists Bring to
  front and Send to back only where they would change something, so with one object it is
  Hide and Delete.)

  ```
  +--------------------------------------------------------------------------------+
  | Top bar 44: emblem ArtFreq FX [Carve|Carve + Draw|Stencil|Draw] DEPTH [-80 dB]  |
  |                                          (undo) (redo) (Live) | ? gear         |
  +--------------------------------------------------------------------------------+
  | Sound strip 32, Carve + Draw and Draw only:                                    |
  |   SOUND  LEVEL [-6.0 dB]  DENSITY [256 lines]  CONTRAST [40 dB]  LOWS [Smooth|Stripes] |
  +--------------------------------------------------------------+-----------------+
  | Hz axis | CANVAS, or the live view                           | Drawer 248,     |
  |         |                                                    | over the canvas |
  |         |                                                    | (a wide object: |
  |         |                                                    | beside it):     |
  |         | time axis, and over it, Brush and Erase only:      | layers / object |
  +---------+----------------------------------------------------+-----------------+
  | Brush strip 32: BRUSH  SIZE --o--  SOFT --o--  STRENGTH --o--                  |
  +--------------------------------------------------------------------------------+
  | Bottom bar 40: [Select|Brush|Erase] [+ Text] [+ Image] [Layers]                |
  |                          Carves once when you press play  [4 bars v] [Repeat]  |
  +--------------------------------------------------------------------------------+
  ```

  - **Top bar** (`EffectTopBar`, 44 px): the emblem (22 px) and "ArtFreq FX" (Inter SemiBold 16,
    FX in accent2; no byline, the window is small). Mode: a segmented Carve | Carve + Draw |
    Stencil | Draw (252 px; it needs no caption, its four choices name it; its tooltip says what
    each does). DEPTH and a number field (62 px, "-80 dB": drag, click to type), shown in the
    modes that carve and not in Draw. From the right: settings, help, a hairline, **Live**, redo,
    undo. Live is one toggle instead of the instrument's Canvas | Live switch: a pill with a
    dot, lit in accent2 (the playing colour, as the live view's own badge) while the live view
    is shown. Undo and redo are each shown only while there is something to undo or redo, each
    in its own place at the open end of the row, so nothing else moves when they come and go.
  - **Sound strip** (`SoundStrip`, 32 px, under the top bar): Level, Density, Contrast (number
    fields, 24 px tall: drag, click to type) and Lows (Smooth | Stripes), the four settings of
    the drawing's sines, in the two modes that play them (Carve + Draw and Draw). In Carve and
    Stencil it is not there and the canvas has its room. A strip rather than a popover: these
    are turned while watching the canvas, which a popover would cover, and a strip shows at a
    glance that the mode has a sound to set. It is the one thing that changes the canvas's
    height, with the mode, which redraws the whole canvas anyway (carved shapes become drawn
    ones); reserving its room in Carve would leave an empty band in the mode most use.
  - **Canvas**: the effect's real output on the user's own song: the measured preview (see
    [Measured preview](#measured-preview)) runs the real carver (the mode's masks, Carve + Draw's
    halo, Depth) and in the Draw modes the sines at Level over the song, takes the latency out, and
    analyses the result as the user's analyser will. **The song** is what came in during the last
    pass that played (`dsp/SongCapture.h`): once the editor has been on screen a moment the
    processor keeps the input (the channels' mean, before it is carved) while a pass plays, a take
    from where the pass starts to where it ends, stops or jumps, at most 2^22 samples (87 s at
    48 kHz; a longer pass uses its first 87 s, repeated); when a take ends, covers enough of the
    pass (a second at least, and not less than a fuller take already kept) and is not the same
    audio again (a DAW looping the same bars: a fingerprint of 16 slots' energy below and above
    250 Hz, within 1.5 dB), two preallocated 16-bit buffers swap (a lock-free hand-over: an atomic
    word holds which one is published and whether a reader is copying it; the audio thread
    neither allocates nor waits, and keeps its take if a reader is busy). The preview copies it
    off the audio thread, plays it at its place in the pass and loops it (a 10 ms crossfaded
    seam) around that. Before any pass has played, a stand-in (`dsp/StandInSong.h`, below). The
    line under the canvas says which: "Real output, on your track's last pass" or "Real output,
    on a stand-in song". Why: the review of the measured preview played ArtFreq FX on songs the
    stand-in was not tuned to; on a bass track the canvas showed carved words in the mids and
    highs that the real output did not have at all (56 dB more contrast), on a mix 20 dB quieter
    20 dB more. Nothing drawn: the song as it is, as the effect leaves it.
    - **The stand-in** is a loud master's shape on an analyser (-10 dBFS RMS): its noise falls 5 dB
      an octave in an analyser's cells (-27 dB a cell at 70 Hz, -46 at 1 kHz, -61 at 8 kHz at FFT
      4096), rolled off below 30 Hz and above 16 kHz, with sixteen beats across the length (a hat
      in the highs, a kick), held chords, grain, and in the sound a kick (a tone falling from 168
      to 48 Hz) and a bass line (a note a half beat, six harmonics) over a noise 14 dB quieter in
      the lows. Before the measured preview it was a texture falling 1.8 dB an octave with a noise kick: against the
      truth lab's dense test master (`tools/TruthLab.cpp`, a bass-heavy mix like much current
      music) its mids sat 13 dB and its highs 25 dB too bright, so carves there looked that much
      deeper against it than against a song, and its lows had none of a mix's bright bass lines,
      which fill small holes back in (lows carved 8 dB deeper than on the master). Now its medians
      are within 1 - 4 dB of the master's in every band and its brightest lows (95th percentile)
      within 1 dB, and a carve stands out against it as against the master (truth lab: excess
      contrast 0.4 - 2.6 dB in the mids and highs, 1.4 - 5 dB in the lows). It is noise made from
      its level picture (frames of random phase overlap-added, the same for the same place on the
      timeline, so a preview does not shimmer from one edit to the next) plus the tones; one song,
      not every song (the help and the README say so).
    - **While dragging** the fast preview (`ui/EffectPreview.h`) draws the same: the song as the
      last measurement analysed it (the stand-in's own level picture before the first), carved as
      the carver and the analyser carve it: per carver bin (11.7 Hz) the drawing's mean over its
      band (Stencil: its maximum, the rest taken), widened by the mode's halo bins, Depth to the
      power of it; in the lows and low mids (below 1.5 kHz, where a row is narrower than two of the
      analyser's bins) each analyser bin reads the song's power around it through the window's
      lobe, carved, so a hole narrower than the lobe fills in from the song's own loud lines
      (before, the carve's power was averaged over a fixed few Hz: low letters came out thin and
      sharp where the real carve is a blurred blob, r 0.64 against r 0.94 now); higher up a row
      reads its least carved bin; in time about half of each window, so thin strokes in time come
      out faint; the sines (the instrument's fast preview) added as powers. Where the drawing did
      not change near since the last measurement, it shows that measurement (see [Canvas](#canvas)). The old calibration of the sines against the stand-in
      (`sinesCalibrationDb`, +10 dB) is gone: both are now in the analyser's own dBFS.
  - **Live** shows what comes out (the song, carved): the processor pushes its output (the
    channels' mean) to the analyser.
  - **Bottom bar** (`EffectBottomBar`, 40 px): the three tools in one raised box (not shown in
    the live view, where they have nothing to act on; V, B and E still choose one and go back to
    the canvas), "+ Text", "+ Image", and "Layers" (lit while the layer list is open). On the
    right Repeat (clear of the corner resizer) and Length, and before them **the line that says
    what will happen** (`effectHint`), right-aligned, in accent2 while the pass runs: "Carves
    once when you press play", "Carves once from bar 9", "Repeats every 4 bars", "Each note
    carves it once", "Loops while a note is held", with the mode's verb (Carve carves, Stencil
    stencils, Carve + Draw and Draw draw). Two states of the playing DAW come first: a pass on
    play that waits for the track's audio, "Waiting for audio to carve" (or stencil); and a DAW
    loop shorter than the drawing, on play or with Repeat, where every round shows the same part
    of it: "Your loop shows 2 of 4 bars" (counted as Length counts, in bars or beats, the loop
    cut to a tenth; from the host's loop points, while it says it loops). It is never clipped:
    where the sentence does not fit it is said in fewer words ("Carves once on play", "Once per
    note", "Loops with a held note", "Waiting for audio", "Loop shows 2 of 4 bars"). It is words
    only; where a single pass starts is chosen in Settings.
  - **Brush strip** (`BrushStrip`, 32 px, on the bottom bar): Size, Soft and Strength, while
    the Brush or the Eraser is the tool and the canvas is shown. Its title names the tool. It
    lies over the canvas's foot, its time axis: the canvas keeps that room under the drawing
    always (and 6 px more, for the lowest Hz label, which is centred on the drawing's edge), so
    B, E and V never resize the canvas. (When the strip took its room from the canvas, pressing B
    squashed the drawing by 32 px and moved it under the brush just as painting began, and
    redrew the canvas at the new size: 48 - 209 ms on a Retina display.)
  - **Drawer**: the inspector (`ui/Inspector.h`, its compact layout: 248 px wide, captions beside
    their controls, the selected object's frequencies under its Band, a bin for Delete, a close
    button) laid over one side of the canvas, between the bars and strips, with a soft shadow on
    the canvas beside it. It is open while an object is selected (its settings) or the Layers
    button is on (the list), and gone otherwise. What the bars and strips hold is left out of
    it: Add text, Add image, the brush, Lows, the Keys.
    - It opens on the right. An object's settings open on the left instead if on the right they
      would cover any of the object (its handles included) or the spot that was just clicked on
      the canvas: the object stays in view while its settings change it, its handles stay free,
      and the drawer never opens under the mouse, where the second click of a double-click
      would land on one of its controls. After a drag it moves aside if the object went under
      it (never to under the mouse). The layer list stays where it is while it is used.
    - **An object too wide for either side** (more than about two thirds of the canvas: a new
      text, which runs from 4 to 96 % of the length, Fill length, a new window's "ArtFreq") gets
      its settings beside the canvas instead of over it: the canvas moves aside, narrower, on the
      side away from the click, so all of the object and its handles stay in view (on the right
      the drawer covered 35 % of the canvas: the end of the text being typed, and the handles
      that resize it, were under it until it was closed). Not while the canvas is pressed (a
      drag would lose the object under the mouse): on the release. The canvas stays beside the
      drawer while that object's settings stay open, and an object that grows too wide while
      they are (Fill length) moves it aside too; it comes back with the list, another object or
      the drawer closing. Narrower objects keep the drawer over the canvas: the drawing does not
      move for them. The snapshot tool checks, in every state with an object's settings open,
      that the object and its handles are clear of the drawer.
    - Closing: the close button puts it away and keeps the selection (a click on the selected
      object brings its settings back); Esc or a click on empty canvas deselects, and the
      settings go with the selection; with nothing selected, Esc or the Layers button closes
      the list. A row clicked in the list selects its object and the list stays; its chevron
      (or a double-click, or Return) shows its settings, and "< Layers" goes back.
    - With both strips shown (Draw, painting) the drawer still has 292 px, and a text object's
      settings, its tallest content, need 288 (`Inspector::compactMinimumHeight`).
  - **Settings** (672 px, two columns, centred under the top bar): on the left the analyser
    match (Scale, Range, Slope, FFT, Levels and a two-line note); the window size in the title
    row (there is no room for a third section in the small window); on the right what the
    instrument keeps in its footer and is rarely changed, under "Playing": **Trigger** (Transport | MIDI, with the note "MIDI notes
    reach an effect in Reaper, Bitwig, Cubase, Studio One and FL Studio; not in Ableton Live"),
    **Start** (On play | At bar, and the bar's field while At bar is chosen) and **Tempo** (Host |
    Manual, and the BPM field while Manual is chosen; with Host the row reads "DAW: 120 BPM").
    Start is there for the one way of playing that has a single pass to place (Transport
    without Repeat); otherwise the row says where passes start ("When a note is played", "On
    the DAW's bars (Repeat is on)"). Then Reset drawing. Across the foot, under a hairline, the
    Licence card as one row (see [Licensing](#licensing)); to make room for it in the small
    window the rows are 6 px apart (were 10). The panel stays under 372 px tall as it was, and
    in every state of the card (a three-line question, a failure's message) it ends at least
    6 px above the window's edge.
  - **Left out**: Preview and Export. Carving needs the song, which only the DAW has; the help
    says so once ("To keep it, bounce the track in your DAW, after choosing At bar in
    Settings"). Also the instrument's Clear (the paint row's Clear and Reset drawing remain) and
    the byline.
  - **Help** (688 px): short and its own. Six steps (where it goes, the analyser, drawing, Mode
    and Depth, play and where Carve and Stencil begin, bouncing in the DAW with At bar), the
    four modes and Sound in a line each, three notes: what Settings holds, the latency, the
    keys; and across the foot the activation note (see the instrument's help).
  - **The saved size.** The state holds the window's size in pixels, not a scale, and an
    ArtFreq FX saved by 0.4 or earlier has the large window's. The two windows have different
    shapes (1120 : 720 and 720 : 440), so the shape tells which one a size was saved with
    (`ArtFreqEditor::scaleForSavedSize`): a size with the large window's shape opens at the same
    scale of the small one (100 % stays 100 %: 720 x 440, not 1120 x 720 or its 156 %), and
    the new size is saved once the window is resized.
  - **Every control says what it is**: a caption or its own words, and a tooltip. Controls are
    28 px tall in the bars (as in the instrument), 26 in the drawer and 24 in the strips, with
    13 px control text (12 in the strips' fields) and 11 px captions, as in the instrument.
    The layout is one design scaled as a whole, so what holds at one size holds at all; the
    snapshot tool still checks, in every state and at 75, 100, 150 and 200 %, that the bars,
    strips and canvas tile the window, that nothing inside a bar, a strip or the drawer
    overlaps or leaves it, and that the bottom bar's line fits.

## Measured preview

The customer, on the screenshots: "Don't want to false advertise", then, after the accuracy
check (`docs/ACCURACY.md`: the canvas was a perfect analyser's picture of the drawing, right
above about 250 Hz, far too clean below it, solid where the highs show scan lines, in colours of
its own): "make the plug-in show correctly what the output will be". So the canvas shows the
real output as the user's analyser will show it (`ui/MeasuredPreview.h`):

- **What is rendered.** One pass of the real output, with the code that plays it: ArtFreq's
  `PassRenderer` (the engine, Level, the safety clipper: what Bounce WAV writes); ArtFreq FX's
  `TunedCarver` with the mode's masks and `ArtFreqProcessor::carveSettingsFor` (the audio
  thread's settings), the gate open over the pass and closing 5 ms before its end, over the
  user's song as it came in during the last pass (`dsp/SongCapture.h`; before any pass, the
  stand-in `dsp/StandInSong.h`; see [ArtFreq FX](#artfreq-fx)), the sines of the Draw
  modes added, the latency taken out as a host's delay compensation does. Silence (ArtFreq) or
  the song uncarved (ArtFreq FX) half a window either side, as an analyser sees around a pass.
- **How it is analysed** (`dsp/SpectrumAnalysis.h`, shared with the live view): a four-term
  Blackman-Harris window, the FFT size from Settings, a full-scale sine reads 0 dBFS; each pixel
  row reads the spectrum between the bins around its centre, or the loudest bin inside it, plus
  the analyser's slope; colours: the canvas's colour map over the levels from Settings (-90 to 0
  dBFS by default). One analysis per pixel column at most and none closer than a sixteenth of the
  window (the window blurs time over far more; the columns between are interpolated): a 4-bar
  pass at 8192 points is 750 analyses whatever the window size.
- **Why that window and that range** (they were Hann and -100 dBFS in 0.6). Hann's side lobes
  start 31 dB under a tone and fall slowly (60 dB down seven bins away). On a log scale, with a
  floor 80 or 100 dB under the sound, that draws every low tone, however clean, with a skirt
  hanging under it to the bottom of the picture: at 8192 points a 50 Hz tone 60 dB above the
  darkest colour shows from 10 to 90 Hz. The customer read exactly that as sound "dripping" below
  the drawing, and could not tell it from the drip that was in the sound. Blackman-Harris (side
  lobes 92 dB down) shows a tone as its main lobe and nothing else (the same tone: 27 to 73 Hz
  at the very faintest), at the price of a main lobe a third wider near its top (2.7 bins at half
  height against 2). What the canvas and the live view show under a shape is now in the sound.
  The floor at -90 dBFS is the deep end of what spectrograms usually show, and just above the
  window's own side lobes under a full-scale sine; at -100 the picture also showed what is left
  of the clicks where lines in the mids and highs start and stop, 70 to 90 dB under the letters.
  Both stay the user's to set (Settings, FFT size and levels);
  a project saved with -100 keeps it. An analyser that uses Hann (MiniMeters' window could not be
  looked up) draws the lows a little thinner and adds that skirt, nothing else differs. Renders
  of one tone, a block and text through both: `ArtFreqLab --drip` writes them
  (`*-hann8192.png`, `*-hann8192-floor80.png`, `*-bh8192.png`, `*-bh8192-floor80.png`).
- **When.** A request (the plan and masks, mode, Depth and Level to the quarter dB, the length to
  the millisecond, the rate, the scale and slope, FFT and levels, the picture's size, the
  document's revision) is made on the canvas's timer; a new one stops a render of an older one at
  once (the render asks once per block); once one has stayed the same for 0.2 s it is rendered on
  the canvas's own low-priority thread, never before the plan is rebuilt for it, only while the
  canvas is on screen and has been for 0.5 s (an editor opened and closed at once, as host scans
  and validators do, starts no thread and renders nothing). A change of levels only recolours the
  last analysis, at once; a change of the instrument's Level alone moves it by as many dB, at once,
  where the clipper is out of it (the pass's peak is kept with the analysis). Nothing runs while
  the editor is closed (the thread belongs to the canvas), nothing on the audio thread, and the
  message thread only copies the finished picture into an image (a few ms at the largest window).
  The processor keeps the last measurement (`PreviewMemory`) while no editor is open: a window
  opened again on the same project shows it at once (before, every opening rendered the
  unchanged project again: 1.4 - 2.8 s of the fast preview each time).
- **What is shown**: see [Canvas](#canvas). Cross-fades: 0.3 s to the measured picture, 0.12 s to
  the fast one (it follows the mouse). The selection, handles, outlines, the hidden layer's tag
  and the brush cursor are drawn on top as before.
- **Cost** (`ArtFreqTruthLab --bench`, one core of the Linux build machine, JUCE's own FFT as on
  Windows without IPP; vDSP on a Mac is several times faster; two runs): the default drawing, 4
  bars at 120 BPM, Retina at 100 %: 0.12 - 0.15 s (ArtFreq), 0.37 - 0.40 s (ArtFreq FX, the carver
  and the stand-in); the worst, everything drawn at Density 1024, 32 bars at 20 BPM (a 384 s
  pass): 2.6 - 3.3 s (ArtFreq, at 8192 and 16384, at 100 and 200 %), 5 - 8 s (ArtFreq FX, Carve
  and Carve + Draw); meanwhile the canvas shows the fast preview and "Updating…". Memory: the
  analysis in hundredths of a dB (1.4 MB for the default, at most 12.5 MB at 200 % on Retina)
  and the picture (5.6 MB at 100 % on Retina, 22.6 MB at 200 %); ArtFreq FX's kept song, 16 MB
  once an editor has been open. The fast preview is made on a thread of its own
  (`ui/FastPreview.h`): asking for it costs the message thread nothing (the last picture is
  shown until the new one is in, a frame or two later; the outlines and handles are drawn on top
  as the mouse moves). Made, on a dense project (nine words, a picture, a stroke, Density 1024;
  `ArtFreqTruthLab --drag`): ArtFreq 17 / 22 / 38 ms at 100 % / Retina / 200 % on Retina, ArtFreq
  FX's Carve + Draw 29 / 54 / 94 ms. (Before, it was made while painting: 19 - 39 ms and 22 - 52
  ms on the message thread per drag step, half the updates a second of 0.5.)
- **Verified** by the truth lab against the real processor's output, analysed independently
  (`ArtFreqTruthLab`, see [docs/ACCURACY.md](ACCURACY.md)): ArtFreq's canvas matches its real
  output in every band (correlation 0.96 - 1.00, mean difference within 0.2 dB at the same
  FFT size); ArtFreq FX's, once a pass of the song has played, matches the real output on that
  song (the dense test master; with `--scenarios` a bass track, a quiet mix and a bass-heavy
  master too): correlation 0.97 - 1.00, mean within 0.2 dB, the carve's contrast within 1.1 dB.
  Before a pass, carving the stand-in, against a different song: correlation 0.65 - 0.98.

## Interface

Window 1120 x 720 at 100 %, resizable from 75 % to 200 % with a fixed aspect ratio (corner
resizer + window-size choice in Settings), remembered per instance. This section describes the
instrument's window. ArtFreq FX's is its own, 720 x 440 (see [ArtFreq FX](#artfreq-fx), "The
editor"), built from the same parts: the canvas, the live view, the inspector (as a drawer),
the panels, the look, and the keys.

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

- Shows the real sound as the user's analyser will (see [Measured preview](#measured-preview)):
  once the drawing and the settings have stayed the same for 0.2 s, one pass of the real output
  is rendered and analysed in the background and the canvas cross-fades to it (0.3 s). Until then,
  and while the drawing or the sound changes (painting, dragging, typing, undo, a load, a mode, a
  knob, whether from the mouse, the wheel, the keyboard or the host), the fast preview shows the
  last measurement where the drawing did not change near since (the columns further than the
  analyser's window, the measurement's spacing and in ArtFreq FX the carver's frame from any
  change; the plan's loudness as a whole, which a change elsewhere can scale, is followed; a
  blend over as many columns again), so the picture changes only where the drawing does and the
  swap to the new measurement is gentle (truth lab, a word moved: correlation with the new
  measurement 0.93 -> 0.97 in the lows, mean difference 5.6 -> 1.9 dB). Where it changed, and
  before the first measurement, a model (`ui/CanvasPlanImage.h`): each line drawn as the analyser draws a
  sine (its window's main lobe, as many Hz wide at every height: a scan line one pixel tall in
  the highs at low Density, blurred and thick in the lows), each line's loudness blurred in time
  by the analyser's window, the standing comb's lines as much brighter as the lines sharing a bin
  add up to, in absolute levels: dBFS at Level with the analyser's slope, coloured over the
  analyser's range from Settings, as the measured preview and the live view are. (The gliding
  comb needed a model of its haze, skirt and rising stripes here; a steady line needs none.) It
  still leaves out what only the sound has (energy spilled where shapes start and stop, lines
  beating, the comb's fine texture): see docs/ACCURACY.md for its
  numbers. A change of the picture's size, the colours, the take of the user's song or (where it
  is one shift) the instrument's Level keeps the measured picture until the new one is in (a
  moment); any other change goes to the fast preview at once (before, a mode, Density or Depth
  changed with the mouse up kept the stale measured picture, the mode before's carve, for up to
  the whole render). Under the canvas's right end, a quiet line: "Preview of the real sound"
  (ArtFreq FX: "Real output, on your track's last pass" or "Real output, on a stand-in song") or
  "Updating…", in fewer words where the bar numbers need the room. Background = colour map at the floor.
  Faint frequency grid lines (from `gridFrequencies`) and bar/beat lines.
- Hz labels on the left axis; bar/beat labels along the bottom (e.g. "1", "2", "3", "4" for
  4 bars; beats for lengths under a bar), in the host's time signature (its beats: three to a
  bar in 3/4, six eighths in 6/8). They count the drawing's own bars (On play, Repeat, MIDI: the
  drawing is not at one place in the song). Only when it plays once at a bar of the song
  (Transport, Repeat off, At bar) are they the song's bars: "9", "10", "11", "12" for 4 bars from
  bar 9 (under a bar, the beats of that bar: "9", "9.2").
- Playhead: a 2 px accent2 vertical line at the play position while sounding.
- **Select tool**: click an object to select it (the selected object if the point is inside it,
  so it can be moved even where another lies over it; else the front-most shown object whose
  rectangle contains the point); selected object shows its rectangle (1 px accent) and 8
  handles; drag inside to move, drag a handle to resize, click empty canvas to deselect.
  Alt/Option-click, or clicking again on the same spot without moving the mouse in between,
  picks the next object down under the mouse (round to the front again; on release, so a press
  and drag still moves the selection). Right-click on an object selects it and opens its menu:
  Bring to front, Send to back (greyed where they would change nothing; left out there in
  ArtFreq FX, which greys nothing), Hide (Show for a hidden
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

A scrolling spectrogram of ArtFreq's own output (`LiveAnalyzer`), right to left, analysed as the
canvas's measured preview is (the same window, the FFT size, slope and colour range from Settings,
the same rows: `dsp/SpectrumAnalysis.h`; see [Measured preview](#measured-preview) for why that
window), so the same sound looks the same in both. A column every
sixteenth of the window (5 to 21 ms at 48 kHz); its history kept in hundredths of a dB, under
16 MB at any FFT size. (Until the measured preview it had its own analysis, Blackman-Harris 4096
points, and its own colours, -89 to -29 dBFS, tuned to look like the canvas's relative ones; 0.6
used Hann over -100 to 0 dBFS, which drew a skirt under every low tone.) It shows one pass of the drawing and a quarter more
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
  session; emoji are kept, and drawn from the bundled monochrome Noto Emoji Bold wherever the
  art font has no glyph: see TextRaster.cpp); Font (combo of art fonts); Style (segmented Solid | Outline); **Band** (segmented
  Low | Mid | High | Full, highlights the matching band if the object sits exactly on it);
  Position: "Fill length" button (sets time 4 %..96 %); Intensity slider (0..100 %; a drag, or a
  run of mouse-wheel steps without a 1 s pause, is one undo step); Delete button.
- Image selected: title "Image"; thumbnail of the processed result; Mode (segmented Auto |
  Original | Negative | Outline | B&W, short labels in ArtFreq FX's narrow drawer, with Auto
  showing what it picked, e.g. "Auto (Negative)"; B&W thresholds the Auto-resolved picture at
  Otsu's level into loud or silent, see ImageProcessing.h);
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
- The hint line under each column says what it does, in plain words: "8 s per pass"; under
  Trigger "Plays once when you press play", "Plays once from bar [9]", "Repeats every 4 bars",
  "Each note plays it once" or "Loops while a note is held"; "From the DAW" / "Typed in".
- For a single Transport pass the words after "Plays once" are the **start choice**
  (`StartChoice`): "when you press play" or "from bar", in the hint's font on the hint's
  baseline, a shade brighter than "Plays once" and followed by a small chevron, so the pair reads
  as one sentence with a part that can be changed (raised on hover, pointing-hand cursor, a
  tooltip). A click opens a menu inside the window with the two, the current one ticked: "When
  you press play", "From a bar of the song". With "from bar" the **start bar field** follows the
  words 3 px on: the BPM field's look at 20 px tall with 12 px mono figures, as the knobs' values
  in the same line; drag up/down (Shift = fine), click to type, Enter commits, Esc cancels,
  out-of-range values clamp to 1..999. The bar is kept while On play is chosen. Both end inside
  the Trigger column's width at every window size (the snapshot tool checks 75, 100, 150 and
  200 %). With Repeat or MIDI there is nothing to choose, and neither is shown.
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

### Keys and the keyboard focus

A plug-in cannot press the DAW's play button; it can only leave the key alone. On macOS JUCE
passes a key on to the host's window only if nothing in the plug-in used it
(`juce_NSViewComponentPeer_mac.mm`: `keyDown` -> `sendEventToInputContextOrComponent`, else
`[super keyDown:]`). "Used" means: a text box that is being typed in took it (the peer's text
input target gets all typing first); or `ComponentPeer::handleKeyUpOrDown` / `handleKeyPress`
said so (the focused component, then its parents and their key listeners; an unused Tab moves
the focus and counts as used); or a component is modal (JUCE then swallows every key). So
(`ui/KeyFocus.h`, the editor's `Content`):

- **The editor's own keys**, and nothing else, are used: V / B / E (tools), Cmd+Z and
  Shift+Cmd+Z (Ctrl+Y elsewhere), Delete / Backspace and the arrows with a selection (also in the
  live view, where the selection is still shown in the inspector or ArtFreq FX's drawer: passed
  on, Delete could delete whatever is selected in the DAW), Esc with a panel open or a
  selection, and in the layer list Up / Down, Return, Delete, Esc. Everything
  else comes back unused in every focus state, also Delete, the arrows and Return when there is
  nothing for them to do: the space bar plays the DAW, and the DAW's other shortcuts work.
- **Where the focus rests**: the canvas; the layer list while a row was clicked; a panel while
  it is open (then the canvas again). A click on anything else (a button, a chip, a segmented
  control, a knob, a combo box, a background) is followed by handing the focus back to the
  canvas (a mouse listener on the content, after the clicked component has dealt with the
  click): JUCE gives a clicked button the focus, where Return would press it again. Combo boxes
  take no focus at all (JUCE focuses a box when its menu closes, and the arrows would be its own).
- **The object's text box is the one place where space types.** A number being typed (a number
  field, a knob's or a bar's value: `shell::NumberTextBox`) has no use for a space: there the
  space bar commits what was typed, as Return does, and is left unused, so the DAW plays. In
  ArtFreq FX one click on Depth, Level or a settings field starts typing, and "a click on a
  value, then the space bar" is exactly the customer's "press the normal play button ... it
  should play instead of me having to click out". For the key to get out on macOS the number's
  text box takes its keys as key presses, not through the system's text input (which takes every
  printable key before anything else sees it; a number needs no input method), and does not
  call a key going down used; a knob's value is a modal label while it is typed, and stops being
  modal before the key is passed on. A text box has the focus only while it is typed in. Return
  commits and Esc cancels a number or a value (the object's text is in the drawing as it is
  typed: both keep it, and undo takes the typing back), Tab commits; each hands the focus back
  to the canvas (to the panel, for a field in the settings). So does a press anywhere outside
  it. In the object's text box Shift+Return (or
  Alt+Return) is a new line, and while it has the focus it shows it: the accent outline, the
  caret, and "Return: done  Shift+Return: new line" beside its caption.
- **Selecting is not typing**: a click on a text object, or opening its settings from the layer
  list, leaves the focus with the canvas. A double-click on it, and Add text, go to typing (on
  the mouse's release, with the text selected so that typing replaces it).
- **Nothing stays modal or a text target**: a value being typed into a label (modal while it is
  edited) is set when the window loses the keyboard or its control is hidden; when a text box
  goes while it has the focus (its object deleted, a value set), the focus goes to the canvas
  rather than to the first control JUCE finds. Menus are modal only while open.
- **Tab** is the DAW's as well: for the moment of the key press the focus is let go (so JUCE has
  nothing to move it from), and taken up again right after.
- **ArtFreq FX** follows the same rules in its own window. Its drawer is the inspector, so the
  layer list in it keeps the focus while a row was clicked and the text box while it is typed
  in; when the drawer goes while it has the focus (its close button, Esc, deselecting), the
  focus goes back to the canvas. Esc there: a panel, else the selection, else the layer list.
  The fields of its strips and of its settings (Level, Density, Contrast, Depth, the start bar,
  the BPM) are number fields like the instrument's BPM: typed in, then handed back.
- What is not in the plug-in's hands, and cannot be tested on the Linux build machine: real
  DAWs on macOS. A host decides what it does with a key that comes back to it (the usual thing
  is to act as if its own window had the keyboard), and a host that keeps the keyboard to itself,
  or eats the space bar before the plug-in's window sees it, never asks.
- **Windows** is not the Mac with another installer. There JUCE posts a key nothing used to the
  host's parent window (`juce_Windowing_windows.cpp`, `forwardMessageToParent`, a `PostMessage`
  of WM_KEYDOWN / WM_CHAR) rather than handing it to the superclass in the same call: whether a
  host acts on a posted key is the host's business, and nothing here tests it. Before a Windows
  build is said to behave as the Mac's, the space bar must be tried in the DAWs it is for
  (Ableton Live, FL Studio, Cubase, Reaper). Other differences: pictures (PNG, JPEG and GIF only;
  macOS decodes the rest), the carver's FFT (JUCE's portable one without IPP, several times
  slower than vDSP: see "Work per audio callback"), and the packaging. The Windows branch
  (`win`) was cut before On play, these keys and ArtFreq FX's own window, and has none of them.

`KeyFocusTests.cpp` and the snapshot tool drive this through the real path: the editor in a
window on a (virtual) display, clicks through `ComponentPeer::handleMouseEvent`, keys through
`handleKeyUpOrDown` and `handleKeyPress`, in every focus state and for both plug-ins.

### Header

The emblem (24 px, `ui/Emblem.h`, image in `docs/artfreq-emblem.png`): a capital A and F as
one monogram (the A's right leg is the F's stem, its apex runs into the F's top arm, its
crossbar carries on as the F's middle arm), solid white on the left and dissolving to the right
into horizontal lines in the spectrogram's colours with growing gaps, as a drawing looks once it
has become sound. Vector paths, no fonts; the number of lines follows the size in physical
pixels (whole-pixel lines and gaps at header size, about 30 lines when large).
`ArtFreqEmblemLab <dir> [--docs file.png]` renders it to PNGs. Then the wordmark "ArtFreq"
(Inter SemiBold 20) and "by AnyMeans" (11 px dim). Segmented Canvas | Live, centred.
Undo / Redo icon buttons (also Cmd+Z / Cmd+Shift+Z). "?" opens the help panel; gear opens
settings. Icons are drawn with juce::Path (no image assets).

### Settings panel (overlay card, closes with Esc / outside click)

Two columns, 672 px, as ArtFreq FX's (the analyser match grew two rows, which a single column of
the old 376 px no longer fitted under the window's height with the licence card). Left, "Match
your analyser": Scale (Log | Linear | Mel), Range (Low and High Hz, editable numbers), Slope
(0 | 3 | 4.5 | 6 dB/oct), FFT (4096 | 8192 | 16384) and Levels (the floor and the top, dBFS,
editable numbers), with a note "As your analyser is set. MiniMeters: Log, 20 Hz – 20 kHz." Right:
Window size (75 % | 100 % | 125 % | 150 % | 200 %), Reset drawing (asks inline; not there while
the plug-in is locked). Across the foot, the Licence card (see [Licensing](#licensing)).

### Help panel (overlay)

Short numbered steps: put ArtFreq on an instrument track; open a spectrogram (MiniMeters) on the
output; match scale, range, slope, FFT size and levels in Settings; add text/image/paint (a moment
after each change the canvas shows the real sound, as the analyser will); choose a band and density; press
play in the DAW (it plays once from wherever you start; Preview plays it with the DAW stopped);
Trigger modes and Repeat explained (once on play or from a bar, loops with Repeat; stop the DAW
and it stops); bounce to WAV. Layers: the list (select, settings, reorder, hide) and
overlapping objects (Alt-click or click again, the right-click menu). Plus tips: highs show the
sharpest detail; the lows are blurry in every analyser (the canvas shows it); Lows (Smooth fills shapes
below about 2 kHz, Stripes plays separate lines there); raise Density for solid shapes, lower for
a scan-line look. Shortcuts, with "Space: plays your DAW (a space while you type)". Across the
foot, under a hairline, **Activate**: where the key is, that an ArtFreq Pro key activates ArtFreq
FX too, two computers per key and deactivating in Settings before moving, offline once activated,
the support address. Both panels must fit the window with room around them (a test): for the
note, the instrument's panel is 1040 px wide (was 920) and both are set a little tighter (2 px
between lines, was 3; smaller gaps between steps and points).

## Licensing

The customer: "when they purchase they can use an activation code and put it into the plug-in to
activate it. Otherwise the plug-in will be locked." The licence code (`src/license/`, with its own
README; shared with Crème and Vocalyst, finished and reviewed before it was wired in) does the
work: the key's checksum, the request on a thread of its own, the Ed25519-signed licence, the
shared `Licenses.json` and its watch, and every message. The plug-ins only wire it in.

- **Wiring.** Each processor makes one `LicenseManager` before anything else (the instrument with
  `artFreqInstrumentLicensing`: ArtFreq or ArtFreq Pro keys; the effect with
  `artFreqEffectLicensing`: ArtFreq Pro only) and keeps it to the end; the editor reaches it
  through `getLicense()`. Making one reads the shared file and the machine ID, nothing more: host
  scans and pluginval never touch the network. Only Activate and Deactivate do.
- **One file for the computer.** The server counts computers, so the activation is kept where
  every user account of the computer finds it: `/Users/Shared/AnyMeans` on macOS,
  `%ProgramData%\AnyMeans` on Windows, a folder the first writer opens to every account
  (`FolderAccess`: mode 0777, or Users: Modify). A second account is never asked for the key, and
  deactivating in any account locks all of them (with a file per account, as before, a second
  account was asked again, and its deactivation freed the computer's place while the first stayed
  unlocked). The account's own folder, where the file used to be, is still read and cleaned, and
  written if the computer's folder can't be.
- **The flag.** The audio thread reads the core's lock-free atomic (`isLicensed()`, which is
  `LicenseManager::isActivated`) at the top of every block. Activating anywhere (this window,
  another instance, the other plug-in, another AnyMeans plug-in) changes the shared file; every
  manager sees it within a second (its file watch), or at once (the one that activated), and sets
  its flag on the message thread. The next block follows.
- **Locked, the instrument** fades its output out over 10 ms (`licenseFadeSeconds`: a gain after
  the safety clipper, so it is a plain fade of whatever played; with nothing sounding there is
  nothing to fade, and it is locked at once, so a note during the fade plays nothing either),
  then `processLocked`: silence,
  MIDI ignored, the engine and tracker reset once (new plans are still taken up, so the exchange
  can free the old ones). Preview is refused (`setPreview`), and one that was on stops.
  **Activated**, it starts afresh: the gain fades in over 10 ms and the tracker begins as when the
  plug-in is first put on a playing track (On play: a pass at once, with its gate's fade-in; At
  bar and Repeat: wherever the song is). No click either way (tested).
- **Locked, ArtFreq FX** lets the song through **unchanged**: it must never mute a master. It runs
  the bypass path (`processEffect` with `bypassed`): Depth 0 and the gate closed, so the output is
  the input one latency late, bit for bit, the sines faded out and then not rendered, a carve in
  progress faded out over a frame. The latency is the same as ever, so nothing moves when it is
  activated; activated, it comes back as from a bypass (the carve fades in with the gate, the
  sines over 20 ms). Preview needs an activated plug-in here too.
- **Locked, everything else works**: state saves and loads, parameters and automation move,
  offline renders run (silent, or the song untouched), the editor opens. Bounce WAV is not
  available (`ExportPanel`: no render starts, none is handed out, a running one stops).
- **The activation screen** (`ui::ActivationScreen`, `ui/Licensing.h`) lies over the window below
  its top bar, which then shows only the name, help and settings (the instrument's Canvas | Live
  and undo / redo, ArtFreq FX's Mode, Depth, Live and undo / redo are hidden: nothing is greyed).
  Behind it the window is dimmed and out of reach: the screen takes every click, and the drawing's
  keys are not the editor's while it is shown. Nothing modal, no window of its own; help and
  settings open over it. A card of 400 px, which fits ArtFreq FX's 720 x 396 under the top bar at
  every size with a message of up to six lines: the emblem, "Activate ArtFreq" / "Activate ArtFreq
  FX", where the key is ("Enter the ArtFreq Pro key from your purchase email." in ArtFreq FX), the
  key field, Activate, the message, and "Where's my key? · Buy ArtFreq (Pro)", links to the store
  (`STORE_URL`, in `LicenseConfig.h` beside the support address). Before anything is tried, the
  message's place says "Once activated, ArtFreq works offline. A key activates two computers."
  - **The key field** (`KeyTextBox`) shows the key in groups of five as it is typed, whatever is
    typed or pasted (any case, dashes, spaces, a mail program's typographic dashes, the key among
    other words of the email: `formatTypedKey`; a mistyped key among them is kept as typed, so the
    typo shows), at most 20 characters, the caret after the same character of the key. A paste is
    taken as a whole key: pasted over a key that was refused (whatever the caret or selection),
    it replaces it, rather than the old key staying or the two mixing; only a piece that completes
    the key typed so far, or one too short to be a key, goes in at the caret.
  - **Busy**: Activate shows a spinner and the core's "Checking your key..."; the field is
    read-only and a second press sends nothing. Closing the window stops the request
    (`cancelRequest`, from the editor's destructor): its answer would go nowhere, and what the
    server did meanwhile stands (activating again later uses no other computer).
  - **The result** is the core's ready-made text: a typo, caught before anything is sent; the
    server's own words for 403 and 409 (ArtFreq FX's own for a key of ArtFreq alone), each with the
    support address (a genuine key can be refused too, and "check for typos" is then no help); "Couldn't
    reach the server — check your connection and try again."; an answer without a signed licence
    ("couldn't be confirmed"); a server error with its code. Trying again is always possible. It
    is selectable text (`SelectableText`, read-only): drag to select, Cmd+C to copy, and a
    double-click on the support address selects all of it (the text box alone stops at '@' and
    '.').
  - **Success**, in the window that activated: "ArtFreq is activated", the core's "Activated.
    This computer is now unlocked for ArtFreq and ArtFreq FX.", and Continue (or Return, or Esc)
    puts it away. Every other open instance simply unlocks: its screen goes.
  - **Keys** (as in "Keys and the keyboard focus"): the screen is the focus's home while it is
    shown; the key field takes typing only after a click into it, as key presses (no system text
    input: a key needs no input method). Return in it activates and hands the focus back, Esc and
    Tab hand it back, and the space bar, which no key holds, ends the typing and goes on to the
    DAW. On the screen, Return activates what is typed (or continues); everything else, Space,
    V / B / E, Cmd+Z and Delete included, comes back unused: the DAW gets it.
- **The Licence card** (`ui::LicenceCard`), in the settings of both plug-ins (the instrument's
  under a "Licence" caption; ArtFreq FX's as a row across the foot of its two columns): the
  activation ("ArtFreq Pro: ArtFreq and ArtFreq FX"), the masked key (`8K3QX-•••••-•••••-NV6ZH`),
  "Activated on this computer", and Deactivate..., which first asks in the core's words
  (`getDeactivationWarning`: what will be locked here, and that the key can then be used on
  another computer), with Cancel and Deactivate. While it runs: a spinner and "Deactivating...".
  If it fails nothing changes, and the card says why in place of the key (the core keeps the
  activation unless the server confirms). If it goes through, the plug-in is locked: the panel
  closes and the activation screen shows the result. With two keys on the computer (an ArtFreq
  key, then an ArtFreq Pro key for ArtFreq FX) the newer is the one in use: the question says what
  it locks and what the other key keeps ("ArtFreq FX will be locked ...; ArtFreq stays activated
  with your ArtFreq key"), and if this plug-in stays activated the panel stays open and the card
  says what went and what stays; the other key is deactivated in turn. Closing the window does not
  stop a deactivation, unlike an activation: the processor's manager finishes it (stopped after
  the server had freed this computer's place, it would leave the computer activated with its
  place free on the server, so the key could go to a third). Closing the project or quitting the
  DAW does stop it, so the core marks the activation in the file before sending: a request cut off
  leaves the mark, and once the request could have ended (two timeouts) the activation no longer
  counts in any instance. The activation screen then says deactivating didn't finish, and the card
  offers Deactivate (to finish it) beside Activate... (activating again replaces it). Not
  activated, the card says so and offers Activate..., which puts the panel away and the typing in
  the key field. The card asks the panel to lay itself out again when its height changes; a
  message longer than its room (a long folder path) scrolls in place.
- **The settings while locked** keep everything that is not the drawing (the analyser, the window
  size, the Licence card), but not Drawing / Reset drawing: the drawing is out of reach and undo
  is hidden, so a reset could not be taken back. The row is not there at all (nothing is greyed),
  and comes back once activated (`SettingsPanel::setDrawingResettable`, from the editor's
  `updateLicensing`).
- **Help, README, installers**: an Activate note across the foot of both help panels (see "Help
  panel"), an Activate section in the README, and the step on both installers' last pages and in
  the Windows zip's `How to install.txt`. The Windows uninstaller leaves `Licenses.json` alone (see
  "Installers"), so a reinstall is still activated.

**Tests.** Every test and snapshot runs against the fake server of `tests/LicenseTestSupport.h`,
never the real one. The test runner (`tests/Main.cpp`) and the snapshot tool make the default
environment an activated one (an ArtFreq Pro activation in a folder of their own:
`installRunnerLicensing`), so everything else sees the plug-ins as a buyer does once activated;
`ScopedLockedLicensing` gives a test (or the snapshot tool's licensing part) instances that are
not, and writes and removes activations as another instance would. Covered: a real licence from
the live server, checked with the built-in public key, and through a whole activation; the
instrument silent and deaf to MIDI and Preview while locked, then fading in when activated, and
no click when it is locked and activated while playing; ArtFreq FX passing the audio through bit
for bit (every mode, mono, stereo, odd blocks) with its latency, carving once activated and
passing through again once locked, without clicks; an ArtFreq key unlocking the instrument and
not the effect; instances following each other (a Pro key entered in the instrument unlocks an
open ArtFreq FX through the shared file; a deactivation in ArtFreq FX locks both); the key
field's formatting; the screen covering a locked window (clicks, keys, the top bar); activating
through it (the requests sent, the success, Continue, a new instance activated without the
network); every failure (a typo, an unknown key, ArtFreq FX's wrong product, 409 with the support
address selected by a double-click, no connection, an unsigned answer, a 502) leaving it locked,
with retry possible, and every refusal naming the support address; busy, and closing the window
during an activation (stopped) and during a deactivation (finished); the Licence card (the
question, Cancel, failures keeping the activation, a deactivation locking it, Activate...), with
the settings fitting the window in every state; two keys (deactivating the newer keeps the
instrument activated, the panel open, and says so; ArtFreq FX locks); a project closed during a
deactivation (locked once the request could have ended, finished from the card); no Reset drawing
while locked; one file for every account of a computer (an activation in one counts in another,
deactivating in either locks both; the account's own folder read, cleaned, and written when the
computer's can't be; the folder opened to every account whatever the umask); state, parameters and
latency while locked; and the keys through a real window (`uiKeysTheActivationScreen`, and
`uiKeysPastingAKeyReplacesWhatTheFieldHolds`: Cmd+V over a refused key, into a partial one, with
the email's words). The snapshot tool renders the screen (empty, typing, busy, an error, the
success, after a deactivation, 75 to 200 %, after a deactivation that didn't finish) and the card
(activated, asking, a failure, not activated, 75 and 200 %, two keys asking and done, a
deactivation that didn't finish) for both plug-ins, checking their layout in each. CI and
pluginval run with no network and no activation: they see the locked plug-ins, which pass
pluginval at strictness 10.

**What the computer can't enforce.** Licences never expire and are checked offline, so a place
freed on the server locks a computer only if its file says so. A backup of the file put back after
a deactivation, or `/api/deactivate` called by hand with the key and machine ID from the file,
frees the place and leaves the computer activated; no record kept on the computer can stop that
(it can be put back too, and the source is public). The limit has to be the server's: a few
deactivations per key in a period (say three in 30 days), then support.

## Tests

`ArtFreqTests` (no host): scale maths; document round trip; image modes (auto-invert on a white
background, transparency, outline); text rendering coverage; compositor placement; plan
(frequencies, spacing, normalisation, contrast, slope); Smooth lows, the standing comb (the lines it
replaces, its grid, gains and phases; low lines never below the drawing, in Stripes too; thin
strokes; the fades in time, with and without Repeat; the band filled at 16384 and 32768 points
where Stripes shows gaps; nothing outside the drawn band; lines back in their pattern after a
silence; determinism, crest factor, crossfades, speed; nothing below the canvas's low end); exchange (versions, garbage); engine
(single line frequency, silence, crossfade continuity, jump fades, Nyquist guard, real-time
speed); tracker (transport positions, tempo changes and ramps, bar lines, the single pass on
play: starting anywhere in the song at any block sizes, in the first block the tracker sees and
after reset, stop and start, a DAW loop longer and shorter than the pass and one beginning
inside it, play pressed before a loop that begins inside the first pass, relocations forwards
and back inside it, before and after it, waiting for sound (at any block sizes, a typed tempo, no
position), tempo changes and ramps, a typed tempo, no ppq and no time, a count-in and a pre-roll, Length and the meter changing while it plays,
switching Repeat, Start at and the trigger while it plays, the preview over it; the single pass
from a start bar: its start and end, starting the DAW inside it, relocations before, after and into it,
a host loop over it, the host's meter and tempo, time-signature changes before, at, inside and
after it and odd bars, a typed-in tempo starting on the DAW's bar, its fades through the engine;
MIDI one-shots and their end, hold-to-loop with overlapping notes and releases, all-offs and stop;
preview once and looping; jumps); end-to-end: a canvas rendered to audio and analysed back must
correlate with the canvas, and at FFT 8192 and 16384 Smooth must fill a block and the text "LOW"
where Stripes leaves gaps, correlate better with the drawing, and keep the band's level steady (no
regular pulse); processor (state round trip, defaults, Lows on Smooth for new instances and on
Stripes for states from before it, switching Lows back and forth while playing without a click,
states from before Repeat and the Free-loop migration, states from before Start at (bar 1 as On
play, any other bar as At bar) and its round trip, sound within 10 ms of the DAW starting in bar
33 with nothing touched (any block size, also first prepared while the DAW plays), once, and
again after stop and start, a DAW loop and coming out of bypass, a single pass and a held MIDI loop through the processor, a single pass
through time-signature changes and at a typed tempo, preview following Repeat, process with MIDI,
panic, bypass fades, time signature, the canvas's song bars, level/clipper bound); WAV export (cancelling it, its
progress, bounce file names); the measured preview (`MeasuredPreviewTests.cpp`: an analyser
column reading a sine at its level and the slope tilting it, the view sanitised, the stand-in's
levels as its picture says and the same noise from any start, a band's level on the canvas as
an analyser reads its loudest line, nothing rendered while the request changes or the canvas is
not on screen and one render once it stays, a render no longer wanted stopping within a block,
a change of levels recoloured at once without rendering, ArtFreq FX's carve on time (no latency
left in), FFT size and levels saved and loaded in both plug-ins and their defaults for older
states, and the canvas on screen: the fast preview first, the cross-fade to the measured one,
Level moving the measured picture at once without a render, Density, an edit and a loaded
project going to the fast preview and back); what a review of it found
(`PreviewHonestyTests.cpp`: ArtFreq FX keeping the pass that played as it came in, the same audio
again publishing nothing new, a moment of a pass not replacing a whole one, the loop's seam
without a click, the canvas carving a bass track that played rather than the stand-in, a mode
or Depth from the host going to the fast preview at once, a window opened again showing the last
measurement at once, the fast preview made on its own thread with the message thread asking for
it in under 5 ms on a dense project at 2x and 4x, and showing the measurement where the drawing
did not change); the fast preview (the comb's band filled and Stripes' gaps, the comb as
bright as the lines, a thin stroke as one line blurred by the window; ArtFreq FX's
modes over the stand-in, the halo, the sines' level, the measured stand-in taking the texture's
place); the live analyser at the FFT size from Settings;
the editor, driven with synthetic events (bounce files; the export following the drawing:
after each change a new file that matches a render of the new plan, the chip's "Updating...",
stopped renders, saving the current file; undo steps of wheel and arrow gestures,
clicking and placing objects, the layer list: selecting, the keyboard, hiding, reordering,
deleting, the paint row; clicking again, Alt-click and the right-click menu on the canvas; the
live view's span, unreadable pictures; under a virtual display on Linux); the keyboard focus
(`KeyFocusTests.cpp`, the editor in a real window, Linux only: the space bar unused after a
click on anything, in both plug-ins; selecting text not typing; the text box, number fields and
typed values giving the focus back on Return, Esc, Tab and a click elsewhere; one click on a
number field and the space bar playing the DAW, the space bar setting a typed number or a
knob's value; Tab; the panels; the live view, and Delete and the arrows acting there on the
selection, in both plug-ins). Hidden layers: the
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
sines as the instrument's), MIDI and Preview with the DAW stopped, and, as it comes, carving
from where the DAW starts playing (bar 33) for one pass, Carve and Stencil waiting for a track's
audio that comes in after play (and the Draw modes not), and its infinite tail, played through a
host that suspends effects over silent input: one pass, not one per audio region. Its editor
(`EffectUiTests.cpp`): the small window and what it leaves out, Mode and Depth and automation
moving them, what each mode and tool shows and that nothing shown is greyed, the bottom bar's
line in every way of playing (and its shorter form fitting; waiting for audio; a DAW loop
shorter than the drawing), the drawer (objects, the list, the side it opens on, closing it; a
wide object's settings beside the canvas, all of it in view, and Fill length moving the canvas
aside), the Brush strip over the canvas's time axis, the timing in Settings, both panels fitting the window, the
window sizes and a size saved with the old large window, and the canvas in each mode (black
where Carve carves at -80 dB, Stencil the other way round, Carve + Draw's halo dark and its
sines bright, nothing drawn leaving the stand-in) and following Level.

`ArtFreqSnapshot <dir>` renders the editor in its main states to PNG (ArtFreq FX's as
`artfreq-fx-*.png`: a new window, its "ArtFreq" selected and "+ Text" typed into (the canvas
beside the drawer), each mode, a shallow Depth typed in, the Mode tooltip, a text's settings in
the drawer on the right, its right-click menu, a picture's settings on the
left, the layer list, the brush, Draw while painting with the list open (both strips and the
drawer: the least room there is), Repeat, the Length menu, the settings with On play and with
At bar and a typed tempo, the live view of a stand-in song carved by the real processor, the
help, an empty drawing, 75 / 100 / 150 / 200 %, with checks in each state that the bars,
strips and canvas share the window (the Brush strip over the canvas's time axis, clear of the
drawing), that nothing overlaps, leaves its bar or is greyed, that the bottom bar's line fits,
and that an object whose settings are open is in view with its handles, clear of the drawer; Mode, Depth, the Sound strip, the drawer, the list, the brush
and the settings driven with the mouse; a size saved with the old large window; and the
instrument's: the default "Plays once when you press play", the start choice's menu, Repeat, MIDI with Repeat, a start bar and typing one, the footer's
timing columns alone for On play and At bar at 75, 100, 150 and 200 % among them, "layers": five objects, one hidden, one selected from the
list, and the window at 75, 100, 150 and 200 %), and checks the editor's controls by driving
them: the canvas interactions, the layer list, Lows (at the foot of the list, clear of the keys
and the brush settings; Stripes and back changing the plan and the canvas; hidden with an
object's settings), and the footer's Trigger, Repeat, the start choice (its words, its menu,
what each item does to the hint, the field and the canvas's bar numbers, in line at every
window size), the start bar field (drag, type, Enter, Esc, clamping), the hint line, Preview
stopping by itself without Repeat, and the layout. Last, for both plug-ins (on Linux, where the
virtual display gives a window the keyboard), with the window on the display and the keyboard in
it: the space bar through the peer in every focus state (see
"Keys and the keyboard focus"), typing where text is typed, and the hand-back.

`ArtFreqLab <dir> [--lows stripes|smooth]` (tools/LowsLab.cpp) is the bench the lows were chosen
on: it renders four drawings through the real plan and engine, analyses them like a spectrogram
(Hann and Blackman-Harris, 4096 to 32768 points, interpolated and nearest-bin) and measures, over
30 Hz - 2 kHz, fidelity, fill, gap depth, flicker, regular pulsing and leakage, with PNG sheets
and a comparison against an earlier run (`--baseline`). `ArtFreqLab <dir> --drip`
(tools/DripLab.h) is the drip check: what sounds below, above and beside eight drawings with
something low in them, with pictures as analysers show them (see [Smooth lows](#smooth-lows)).
`ArtFreqLab <dir> --skeptic` (tools/SkepticLab.h) is the second opinion on that: it plays the
drawings people make (the default text in the Low band, small and large low text, soft
gradients, brush strokes, a full-height picture, text in the highs) at Density 64, 256 and 1024,
over 1, 4 and 16 bars, looped and once, on the log and the linear scale, and writes each one's
real output as raw floats with what was drawn where, for an analysis from outside; and it builds
thin horizontal strokes at every height and counts the lines that play each (docs/ACCURACY.md
has what it found).

`ArtFreqTruthLab <snapshot-dir> <out>` (tools/TruthLab.cpp) checks the screenshots against the
sound. The snapshot tool, run with `ARTFREQ_SNAPSHOT_STATES=1`, saves each state beside its PNG
(the processor's state and where the canvas lies in the picture); the lab loads each into a fresh
processor of its kind, activated against the test licensing environment, plays one pass with a
transport that starts after 0.77 s stopped (ArtFreq FX on a dense synthetic master at -10 dBFS RMS,
its output moved back by the latency), analyses the output as an FFT spectrogram such as
MiniMeters' does (its settings assumed; the canvas's window, Blackman-Harris, 4096 / 8192 / 16384
points, one analysis per canvas column, the state's scale, rows interpolated between bins
or the loudest bin inside them, the canvas's colour map over -100 to 0 dBFS) and writes the
screenshot's canvas beside the real display with numbers per band (lows < 250 Hz, low mids, highs
> 2 kHz): correlation, levels inside and around the drawing, contrast against the canvas's, and
the share of the drawing within 12 dB of the canvas (the instrument) or carved 30 dB under the
song (ArtFreq FX). `--marketing` renders real-output images for the website. The findings are in
[ACCURACY.md](ACCURACY.md): accurate from about 250 Hz up; blurred, thicker and hazy below
(analyser resolution, which the canvas does not draw); scan lines in the highs at Density
256 - 384 where the canvas draws solid; colours depending on the analyser's dB range.

## Installers

A Windows tester, given the zip: "tried extracting to install the plug in nothing would
happen... No pop up like normal to install the program". So each system has an installer of the
kind people expect there, and the zip stays for installing by hand.

- **macOS** (`installer/macos/build-pkg.sh`): a product archive (pkgbuild, productbuild) with a
  welcome page, the licence, one choice per plug-in (both on) and a last page with the first
  steps. The bundles are non-relocatable and not version-checked, so they always land in
  `/Library/Audio/Plug-Ins/VST3`; a postinstall script removes the FreqArt bundles and offers to
  move the installer to the Trash. Not notarised.
- **Windows** (`installer/windows/ArtFreq.iss`, Inno Setup 6.3 or later; ISCC gets the version
  as `/DAppVersion`, which CI reads from CMakeLists.txt): 64-bit only (`x64compatible`: x64
  Windows, and Windows 11 on Arm, whose x64 hosts load x64 plug-ins), Windows 10 or later, run
  as administrator. Its pages: welcome (in the macOS welcome's words), the licence (LICENSE, its
  100-column lines joined into paragraphs when the page opens, so the page wraps them), the
  plug-ins (two components, both in the default type; at least one must stay chosen; an upgrade
  starts from the previous choice), Ready (it says where the plug-ins go, since the only folder
  Setup itself knows is the uninstaller's) and the last page (rescan, where each plug-in is
  listed, Ableton Live's Use VST3 Plug-In System Folders, activating). No folder page: the VST3
  standard fixes `C:\Program Files\Common Files\VST3` (`{commoncf64}\VST3`).
  - **Upgrades.** Before anything is copied, `[InstallDelete]` removes each chosen plug-in's
    bundle folder whole, and the FreqArt bundle it replaces, so no file of an older version
    stays inside a bundle. Files are copied without version checks. The AppId (a GUID in the
    script) must never change: by it Windows and Setup find the earlier install, so there stays
    one entry under Installed apps and one uninstaller, whose log is appended to.
  - **Uninstalling.** The uninstaller (`unins000.exe`) lives in `C:\Program Files\AnyMeans\ArtFreq`,
    outside the VST3 folder. It removes the files it installed, then the two bundle folders
    whole (`[UninstallDelete]`, in case anything was added inside them), its own folder and its
    entry under Installed apps. It does not touch `%ProgramData%\AnyMeans\Licenses.json`: the
    activation belongs to the computer and to every AnyMeans plug-in, not to this installer, and
    keeping it means a reinstall is still activated (to free the computer's place, deactivate
    first).
  - **A DAW that has the plug-in loaded** holds its DLL, which is named `.vst3`: so
    `CloseApplicationsFilter` adds `*.vst3` to the files Setup checks, Setup finds the DAW and
    offers to close it, and does not start it again afterwards.
  - **Not code-signed**, so a browser may warn that the file is not commonly downloaded (Edge:
    Keep, then Keep anyway) and SmartScreen says "Windows protected your PC" until **More
    info**, **Run anyway**; the README and the release notes say so.
  - **Pictures** (`installer/windows/images`, made by `make-images.py` from
    `docs/artfreq-emblem.png`): the tall wizard picture (the AF letters with "ArtFreq" and "by
    AnyMeans" on the emblem's ink) at 164x314 to 534x1022 and the small one (the emblem on white)
    at 58 to 159 px, the sizes Inno Setup 6.0 to 6.7 choose from, as 24-bit BMPs; the icon, 16
    to 256 px.
  - **Silent**: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=<file>` for the installer and the
    uninstaller alike, as CI runs them.

## Builds and releases (CI)

GitHub Actions (`.github/workflows/build.yml`) builds macOS and Windows; Linux is the
development box only (its matrix entry is gone, its steps are kept).

- **Plan.** A first, small job writes the build matrix and whether to publish (a job-level `if`
  cannot read the matrix). Pushes, pull requests and manual runs with platform **all** build
  both; a manual run of **macOS** or **Windows** builds that one and never publishes (its
  artifacts stay on the run), which saves macOS minutes (billed ten times) while working on one
  platform. Only a push to a branch or a manual run of **all** publishes.
- **macOS** (macos-15): universal (arm64 + x86_64); tests natively and under Rosetta; the
  interface snapshots; ad hoc signing again after JUCE writes moduleinfo.json; pluginval at
  strictness 10; the zip; the pkg installer, test-installed over stand-ins for the old FreqArt
  bundles, its copies validated at strictness 5.
- **Windows** (windows-2022, Visual Studio 2022, x64): the plug-ins and the tests only
  (`ARTFREQ_BUILD_TOOLS=OFF`, one project at a time: a private repository's Windows runner has
  2 cores and 7 GB, and every target compiles all of JUCE with link-time code generation),
  with the C runtime linked statically (`CMAKE_MSVC_RUNTIME_LIBRARY`, set before any target)
  and `/utf-8`; ctest (stopped after 15 minutes); a check of the bundle layout
  (`<name>.vst3/Contents/x86_64-win/<name>.vst3`, moduleinfo.json) and of the DLLs' imports
  (x64, no VCRUNTIME / MSVCP / api-ms-win-crt, with dumpbin); pluginval at strictness 10.
  `ArtFreq-VST3-Windows.zip` holds both bundles and `How to install.txt` (installer/windows,
  CRLF). The zip is unzipped into a stand-in for `C:\Program Files\Common Files\VST3` (then
  into the real one when the runner may write there), its layout and text checked, its DLLs
  compared with the validated ones and validated again at strictness 5. Then the installer: ISCC
  (on the image; Chocolatey installs Inno Setup only if it is missing) builds
  `ArtFreq-<version>-Windows.exe`, uploaded at once (artifact ArtFreq-Installer-Windows).
  Stand-ins for `FreqArt.vst3`, `FreqArt FX.vst3` and a stale file inside `ArtFreq.vst3` go
  into the real VST3 folder; the silent install must exit with 0, remove all three, and leave
  the bundles identical to the built ones (`diff -r`: every file, nothing more), which pass
  pluginval at strictness 5 there, and an entry under Installed apps (name, version, publisher)
  naming `C:\Program Files\AnyMeans\ArtFreq\unins000.exe`. The silent uninstall must exit with
  0 and leave none of the bundles, no uninstaller folder (waited for up to a minute: the
  uninstaller's copy in TEMP deletes it after the original returns) and no entry, and a
  stand-in `Licenses.json` unchanged. The installer's and uninstaller's logs are printed when a
  step fails.
- **Release.** Needs every build; checks that every platform's files are present (the macOS
  pkg, the Windows exe and both zips: its clean-up takes off the release whatever this build did
  not make), uploads them under temporary names, then swaps each in, and updates `v<version>`
  and its notes.
- **Platform differences in the code.** Pictures: macOS decodes HEIC, WebP, TIFF and BMP too
  (`ARTFREQ_READS_PICTURES_WITH_MACOS` in Actions.cpp sets the chooser's and the drop's
  filters); Windows and Linux read PNG, JPEG and GIF. Shortcuts read Cmd / Option on a Mac, Ctrl /
  Alt elsewhere, and Ctrl+Y also redoes off the Mac. The bounce folder is JUCE's temporary
  folder (`%TEMP%\ArtFreq` on Windows); the WAV chip is dragged out with JUCE's external
  file drag (OLE on Windows, on a thread of its own). JUCE's FFT is vDSP on the Mac and JUCE's
  portable one on Windows and Linux, so ArtFreq FX costs more there (see Carving).
