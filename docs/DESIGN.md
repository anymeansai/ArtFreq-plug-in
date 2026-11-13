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
   highs" keeps meaning the highs.
3. **The compositor** (`core/Compositor.h`) flattens the document into a 1024 x 1024 canvas of
   loudness values 0..1 (max of all layers).
4. **The plan** (`core/ArtPlan.h`) turns the canvas into a bank of sine "lines". Density =
   number of lines across the canvas height (few = scan lines, many = solid). Each line's
   amplitude per column comes from the canvas, mapped to decibels with the *contrast* (dB range),
   compensated for the analyser's slope, and normalised so the loudest moment sits at -12 dBFS
   RMS before the output level.
5. **The engine** (`dsp/ArtEngine.h`) plays the plan on the audio thread: one sine per line,
   amplitudes interpolated across columns, click-free fades on start/stop/retrigger, crossfades
   when the plan changes.
6. **The tracker** (`dsp/PlayheadTracker.h`) decides the position in the drawing: follow the
   DAW transport (loop every length, aligned to bar 1), play once per MIDI note, or free loop;
   tempo from the host or typed in.

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
- **Export** renders on a background thread from a shared (immutable) plan.

## Parameters

| id | name | range / choices | default | automatable |
| --- | --- | --- | --- | --- |
| level | Level | -60..0 dB | -6 dB | yes |
| density | Density | 16..1024 lines | 256 | no |
| contrast | Contrast | 12..72 dB | 40 dB | no |
| length | Length | 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars, 16 bars, 32 bars | 4 bars | yes |
| trigger | Trigger | Transport, MIDI note, Free loop | Transport | yes |
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

State = APVTS tree + document tree + editor size, saved as XML in binary (`copyXmlToBinary`).
Neither the drawing nor the editor size is a parameter, so edits, undo/redo and resizes tell the
host the project changed (`updateHostDisplay` with `nonParameterStateChanged`); loading state
does not. Each picture is PNG-encoded once and its text kept while any document or undo step
still holds the picture (a loaded picture keeps the text it was loaded from), and a load keeps
the loaded tree as the state to save, so saving after an edit or a load encodes nothing again.

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
| Footer 112: DENSITY  CONTRAST  LEVEL | LENGTH  TRIGGER  TEMPO [Host|Manual] BPM | PREVIEW | EXPORT |
+--------------------------------------------------------------------------------------+
```

### Canvas

- Shows the current plan as the analyser would: each line drawn at its height with the colour
  map of its amplitude in dB (relative to the loudest), lines 1-2 px tall so low density shows
  as visible scan lines and high density looks solid. Background = colour map at silence.
  Faint frequency grid lines (from `gridFrequencies`) and bar/beat lines.
- Hz labels on the left axis; bar/beat labels along the bottom (e.g. "1", "2", "3", "4" for
  4 bars; beats for lengths under a bar).
- Playhead: a 2 px accent2 vertical line at the play position while sounding.
- **Select tool**: click an object to select it (top-most whose rectangle contains the point);
  selected object shows its rectangle (1 px accent) and 8 handles; drag inside to move, drag a
  handle to resize, click empty canvas to deselect. Delete/Backspace removes the selected object.
  Arrow keys nudge (Shift = 10x). Objects' rectangles are drawn as faint dashed outlines when not
  selected, so they can be found even when quiet.
- **Brush / Erase**: paint with the toolbar's Size (2..64 px, screen), Softness (0..100 %),
  Strength (0..100 %). A circular cursor preview shows the brush. One stroke = one undo step.
- Drop an image file onto the canvas to add it as a picture where it was dropped (in the band
  under the cursor).
- Empty state (no objects, nothing painted): centred hint "Add text, drop an image, or paint
  with the brush".

### Live view

A scrolling spectrogram of FreqArt's own output (`LiveAnalyzer`), right to left, same scale,
range, slope and colour map as the canvas, ~6 seconds visible. A small "LIVE" badge in accent2.
Shown instead of the canvas when "Live" is selected in the header. Turns the analyser on only
while visible.

### Inspector (right panel)

- Nothing selected: title "Drawing"; buttons "Add text", "Add image..."; a short help line;
  brush settings mirrored when Brush/Erase is active.
- Text selected: title "Text"; multi-line text field (live updates, one undo step per typing
  session); Font (combo of art fonts); Style (segmented Solid | Outline); **Band** (segmented
  Low | Mid | High | Full, highlights the matching band if the object sits exactly on it);
  Position: "Fill length" button (sets time 4 %..96 %); Intensity slider (0..100 %);
  Delete button.
- Image selected: title "Image"; thumbnail of the processed result; Mode (segmented Auto |
  Original | Negative | Outline, with Auto showing what it picked, e.g. "Auto (Negative)");
  Clean-up slider (0..90 %); Band; Fill length; Intensity; "Replace..."; Delete.
- Frequency read-out of the selected object ("3.5 kHz - 16 kHz") in mono.

### Footer

- Three rotary knobs with value read-outs: Density ("256 lines"), Contrast ("40 dB"),
  Level ("-6.0 dB"). Drag, Shift = fine, double-click = default, double-click value = type.
- Length combo; Trigger segmented (Transport | MIDI | Loop); Tempo segmented (Host | Manual)
  with a BPM field (editable when Manual; shows the host tempo greyed when Host).
- Preview: a round play/stop button (accent2 when on). Plays the drawing on a loop.
- Export: "Bounce WAV" button renders one pass in the background; then a draggable chip
  "FreqArt.wav  (drag into your DAW)" plus "Save..." (async FileChooser). The file lives in a
  FreqArt folder in the temp directory.

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
play in the DAW (or Preview); Trigger modes explained; bounce to WAV. Plus tips: highs show the
sharpest detail; the lowest octaves are coarse in every analyser; raise Density for solid
shapes, lower for a scan-line look.

## Tests

`FreqArtTests` (no host): scale maths; document round trip; image modes (auto-invert on a white
background, transparency, outline); text rendering coverage; compositor placement; plan
(frequencies, spacing, normalisation, contrast, slope); exchange (versions, garbage); engine
(single line frequency, silence, crossfade continuity, jump fades, Nyquist guard, real-time
speed); tracker (transport positions, MIDI one-shots, free loop, preview, jumps); end-to-end:
a canvas rendered to audio and analysed back must correlate with the canvas; processor (state
round trip, process with MIDI, level/clipper bound); WAV export.

`FreqArtSnapshot <dir>` renders the editor in its main states to PNG.
