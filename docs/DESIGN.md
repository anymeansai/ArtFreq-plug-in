# FreqArt: design

FreqArt is an AnyMeans instrument plug-in (VST3, macOS first) that turns text, pictures and
brush strokes into sound whose **spectrogram shows them**. Open a spectrogram (MiniMeters, or
any analyser with a scrolling spectrogram) on the output and the drawing scrolls past.

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
   RMS before the output level.
5. **The engine** (`dsp/ArtEngine.h`) plays the plan on the audio thread: one sine per line,
   amplitudes interpolated across columns, click-free fades on start/stop/retrigger/bypass,
   crossfades when the plan changes.
6. **The tracker** (`dsp/PlayheadTracker.h`) decides the position in the drawing (see
   [Trigger](#trigger)): follow the DAW transport (once, from a bar of the song, or with Repeat
   looping every length on the host's bar lines; tempo changes and ramps bend the position, only
   relocations jump), or play per MIDI note (once, or with Repeat looping while a note is held);
   tempo from the host or typed in. Nothing plays by itself: without the DAW, only Preview does.

Why sound and not MIDI: a spectrogram shows the frequencies present at each moment. MIDI notes
can only make the few pitches of a synth patch, on a semitone grid, with harmonics drawn on top;
pictures need hundreds of exactly placed frequencies. So FreqArt *is* the instrument: MIDI is
used as a trigger, and the drawing can also be bounced to a WAV file.

## Threads

- **Message thread**: the document, undo history, compositor and plan building (debounced by a
  timer, ~25 Hz), the editor, the live view analysis.
- **Audio thread**: tracker + engine + level + safety clipper; pushes output to the live
  analyser only while the live view is visible. No locks, no allocation.
- **Plans** travel through `PlanExchange` (wait-free acquire on the audio thread, garbage
  collected on the message thread).
- **Export** renders on a background thread from a shared (immutable) plan; a change that makes
  the render unwanted stops it (the message thread never waits for it).

## Parameters

| id | name | range / choices | default | automatable |
| --- | --- | --- | --- | --- |
| level | Level | -60..0 dB | -6 dB | yes |
| density | Density | 16..1024 lines | 256 | no |
| contrast | Contrast | 12..72 dB | 40 dB | no |
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

Density and Contrast reshape the plan, which the message thread rebuilds on its timer (by
wall-clock time, not in step with the audio): automated, they would land late, and somewhere
different on every faster-than-real-time bounce. So, like the analyser settings, they are not
automation targets.

Repeat and Start bar were added after 0.1 (parameter version hint 2). A saved parameter tree
that lacks a parameter loads it at its default (Repeat off, bar 1), not at whatever the instance
had; FreqArt 0.1's third trigger, "Free loop" (index 2, or its name), loads as Transport with
Repeat on, the nearest thing left (a plain clamp to the two choices would make it MIDI note).
Host automation lanes recorded on the old three-choice Trigger cannot be rewritten: there,
Free loop now reads as MIDI note.

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
| Header 52: FreqArt wordmark + "by AnyMeans" | [Canvas | Live] | undo redo | ? | gear  |
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
  as visible scan lines and high density looks solid. Background = colour map at silence.
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

A scrolling spectrogram of FreqArt's own output (`LiveAnalyzer`), right to left, same scale,
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
  settings mirrored when Brush/Erase is active. The list (`ui/LayerList.h`) is a dark well with
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
  draggable chip names the file on two lines ("FreqArt HELLO" over "14-03-27.wav"), plus "Save..."
  (async FileChooser).
  - **Names.** Every bounce is a new file in <temp>/FreqArt, "FreqArt <first text, or Drawing>
    <HH-MM-SS>.wav" (the text cut to letters, digits, spaces and `-_()'&+!`, at most 20
    characters; " 2", " 3" ... in the same second). The file is created when its render starts,
    which reserves the name, so no two bounces (of this instance or another) ever share one. DAWs
    (Logic, Live, FL Studio) know dropped audio by its path and name and keep what they read the
    first time: a file rewritten under the same name (0.1.0 wrote FreqArt.wav every time) plays
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
    unless it was dragged out (a DAW may read it where it is); FreqArt*.wav files a day old are
    deleted when an editor opens. Closing the window stops running renders (the render checks once
    per block) instead of waiting for them.

### Header

Wordmark "FreqArt" (Inter SemiBold 20) and "by AnyMeans" (11 px dim). Segmented Canvas | Live.
Undo / Redo icon buttons (also Cmd+Z / Cmd+Shift+Z). "?" opens the help panel; gear opens
settings. Icons are drawn with juce::Path (no image assets).

### Settings panel (overlay card, closes with Esc / outside click)

"Match your analyser": Scale (Log | Linear | Mel), Low Hz, High Hz (editable numbers), Slope
(0 | 3 | 4.5 | 6 dB/oct) with a note "MiniMeters: Log, 20 Hz - 20 kHz; set Slope to match its
Slope setting". Window size (75 % | 100 % | 125 % | 150 % | 200 %). Reset drawing (asks inline).

### Help panel (overlay)

Short numbered steps: put FreqArt on an instrument track; open a spectrogram (MiniMeters) on the
output; match scale/range in Settings; add text/image/paint; choose a band and density; press
play in the DAW (or Preview, once or on a loop); Trigger modes and Repeat explained (nothing
plays by itself); bounce to WAV. Layers: the list (select, settings, reorder, hide) and
overlapping objects (Alt-click or click again, the right-click menu). Plus tips: highs show the
sharpest detail; the lowest octaves are coarse in every analyser; raise Density for solid
shapes, lower for a scan-line look.

## Tests

`FreqArtTests` (no host): scale maths; document round trip; image modes (auto-invert on a white
background, transparency, outline); text rendering coverage; compositor placement; plan
(frequencies, spacing, normalisation, contrast, slope); the rising comb (`PlanSettings::lowMethod` 3, a
prototype the processor does not use yet: the lines it replaces, its loudness grid, a long-FFT
analyser seeing the band filled, leakage, determinism, crossfades, speed); exchange (versions, garbage); engine
(single line frequency, silence, crossfade continuity, jump fades, Nyquist guard, real-time
speed); tracker (transport positions, tempo changes and ramps, bar lines, the single pass from a
start bar: its start and end, starting the DAW inside it, relocations before, after and into it,
a host loop over it, the host's meter and tempo, time-signature changes before, at, inside and
after it and odd bars, a typed-in tempo starting on the DAW's bar, its fades through the engine;
MIDI one-shots and their end, hold-to-loop with overlapping notes and releases, all-offs and stop;
preview once and looping; jumps); end-to-end: a canvas rendered to audio and analysed back must
correlate with the canvas; processor (state round trip, defaults, states from before Repeat and
the Free-loop migration, a single pass and a held MIDI loop through the processor, a single pass
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
leaving them out.

`FreqArtSnapshot <dir>` renders the editor in its main states to PNG (Repeat, MIDI with Repeat,
a start bar and typing one among them, "layers": five objects, one hidden, one selected from the
list, and the window at 75, 100, 150 and 200 %), and checks the editor's controls by driving
them: the canvas interactions, the layer list, and the footer's Trigger, Repeat, the start bar
field (drag, type, Enter, Esc, clamping), the hint line, Preview stopping by itself without
Repeat, and the layout.
