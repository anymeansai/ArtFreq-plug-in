# ArtFreq

**by AnyMeans**

A plug-in (VST3, for macOS and Windows) that turns text, pictures and brush strokes into sound whose **spectrogram shows them**. Put it on an instrument track, open a spectrogram such as MiniMeters on its output, press play, and your drawing scrolls past.

**Renamed in 0.4:** ArtFreq and ArtFreq FX were called FreqArt and FreqArt FX before. Old projects keep working (they open with the renamed plug-ins); after updating, rescan plug-ins in your DAW.

It comes as two plug-ins from the same code, installed together:

- **ArtFreq**, an instrument for MIDI and instrument tracks: it plays the drawing.
- **ArtFreq FX**, an audio effect for audio tracks, buses and the master, in a small window of its own: it carves the drawing into the song passing through, so it shows as dark shapes even in a full mix, or keeps only the drawing, or plays it into a hole carved for it, or on top. See [ArtFreq FX](#artfreq-fx-the-drawing-in-a-song).

Both are activated once per computer with the key from your purchase email: an **ArtFreq** key unlocks the instrument, an **ArtFreq Pro** key both plug-ins (ArtFreq FX comes only with ArtFreq Pro). Until then they are locked. See [Activate](#activate).

Why two? A DAW decides from a VST3's category where it may go: Ableton Live, FL Studio and Cubase offer an instrument only on MIDI / instrument tracks and an effect only on audio tracks, buses and the master. No single plug-in can be both, so ArtFreq is registered once as each. What is underneath is shared: the canvas and the drawing tools, the timing, even the format of the saved state. Your DAW keeps presets and projects per plug-in, though, so a drawing does not move from one to the other by itself (see [Moving between the two](#moving-between-the-two)).

The canvas is a picture of your spectrogram: left to right is time across the drawing's length (1 beat to 32 bars, in time with the song), bottom to top is frequency on the same scale as your analyser (logarithmic, 20 Hz to 20 kHz, by default). What you see on the canvas is what the analyser will show.

## How to use it with MiniMeters

1. **Put ArtFreq on an instrument track.** It is an instrument: it makes the sound itself and needs no audio input. Create an instrument (MIDI) track and load ArtFreq on it. The first time, its window asks for your key: paste it and press **Activate** (see [Activate](#activate)). A fresh instance already has something to show: the word "ArtFreq" in the mid band.
2. **Open the spectrogram.** In MiniMeters, choose the **Spectrogram** with a **logarithmic** frequency scale from **20 Hz to 20 kHz**, listening to your DAW's output. Any analyser with a scrolling spectrogram works.
3. **Match the settings.** Open ArtFreq's **Settings** (the gear) and set **Scale**, **Low**, **High** and **Slope** to what your analyser uses. The defaults (Log, 20 Hz, 20 kHz) match the MiniMeters setup above; if your analyser tilts its display (MiniMeters has a Slope setting), give ArtFreq the same Slope.
4. **Press play in your DAW**, anywhere in the song. As it comes (Transport, Repeat off, "when you press play"), ArtFreq plays the drawing once, starting the moment the DAW starts; stop, press play again, and it plays again. (A DAW's export or freeze starts it where the render starts: to keep it at a bar of the song, see "from bar" in step 10.) The drawing scrolls through the spectrogram; ArtFreq's own **Live** view (the Canvas | Live switch at the top of the window) shows the same thing. The space bar (or whatever key plays your DAW) also works while ArtFreq's window has the focus: you do not have to click back into the project first.
5. **Make it yours.**
   - **+ Text** adds a text box in the mid band across the whole length (or, if something is already there, in a free band), ready for typing. Pick a font and Solid or Outline letters in the inspector on the right.
   - **+ Image** adds a picture (PNG, JPEG or GIF; on a Mac also HEIC, WebP, TIFF or BMP, which macOS reads), placed like new text, or drop an image file straight onto the canvas: it lands in the band under the cursor. A file that cannot be read is reported at the bottom of the canvas. **Mode** decides what in the picture becomes sound; **Auto** usually gets it right (on a white background it draws the dark parts).
   - **Brush** and **Erase** paint on the canvas, with Size, Soft and Strength in the toolbar.
   - **Layers.** With nothing selected, the panel on the right lists every text and picture, front first, and the paint layer last. Click a row to select it (handy when a picture covers some text), double-click it (or click its **›**) for its settings, drag rows to reorder, and click the eye to hide one: a hidden layer stays in the drawing, and in your saved session, but makes no sound. **‹ Layers** at the top of an object's settings goes back to the list.
6. **Place it with the bands.** Select an object and choose a **Band** in the inspector: **Low** (40–250 Hz), **Mid** (300 Hz–3 kHz), **High** (3.5–16 kHz) or **Full** (the whole visible range). Or drag it to move it, and drag its handles to resize it. **Fill length** stretches it across the drawing.
7. **Set the density and the lows.** **Density** is the number of sine lines across the height of the canvas. Few lines give a scan-line look; many lines merge into solid shapes. **Lows**, at the foot of the Layers list, chooses how the low end plays (below about 2 kHz): **Smooth** (the default) keeps shapes there filled at every FFT size of your analyser; **Stripes** plays separate lines 25 Hz apart, which an analyser with a long FFT (8192 points and up) shows as stripes with dark gaps, an effect of its own (and the only look in ArtFreq 0.1 and 0.2).
8. **Set the contrast.** **Contrast** is how many decibels separate the brightest parts of the drawing from the faintest. To reproduce greys faithfully, set it near the range your spectrogram shows from dark to full colour; lower it to bring out faint detail, raise it for deeper blacks.
9. **Set the level.** **Level** is ArtFreq's output volume. ArtFreq sets the loudest moment of the drawing to −12 dBFS RMS before Level, so at the default −6 dB it peaks at about −18 dBFS RMS.
10. **Choose the length, tempo and trigger.**
    - **Length** is how long one pass of the drawing takes: 1 beat, 2 beats, 1 bar, 2 bars, 4 bars (default), 8, 16 or 32 bars, at the song's tempo and time signature.
    - **Tempo**: **Host** follows the DAW's tempo; **Manual** uses the **BPM** you type (also used when a host gives no tempo).
    - **Trigger** and **Repeat** decide when the drawing plays. Nothing plays by itself: it plays while the DAW plays or while notes play it (or with Preview).
      - **Transport** follows the DAW's timeline. With **Repeat** off (the default) it plays **once**, for one Length, and the line under Trigger says from where; click its words to choose:
        - **when you press play** (the default): it starts the moment the DAW starts playing, wherever the playhead is, also when you put ArtFreq on the track while the DAW is already playing. Stop and play again and it starts again. While it plays it follows the song: jump forwards to a place inside the pass and it carries on there; jump back, or past its end, and a new pass starts right there. So a DAW loop draws the drawing from its start every round, wherever you pressed play (from the second round on, if you pressed it before the loop): the whole drawing under a loop at least as long, its first part under a shorter one. A count-in before bar 1 is silent: the drawing starts with the song.
          **Exporting, freezing, bouncing in place:** your DAW renders the song from where the render starts, and to ArtFreq that is pressing play there, so the drawing lands at the start of the render (usually bar 1), not where you heard it while you played from bar 9. Before you export, choose **from bar** and the bar you want it at.
        - **from bar** and a bar number (1 to 999): it plays only there, from the start of that bar of the song. Start the DAW in the middle of it and it plays from there; before and after it is silent, and looping the DAW over it plays it each time round. The bar is the DAW's bar, also with a **Manual** tempo (which then only sets how fast the drawing plays from there), and the pass lasts Length in that bar's time signature. Plug-ins are not told bar numbers, so ArtFreq counts the bars as the DAW plays: a time-signature change before the bar (a 2/4 bar, a new section) is counted when the DAW plays through it, and a change at the bar or later moves nothing. Started after such a change without having played through it, ArtFreq counts as if the whole song were in the current time signature and starts on the nearest bar line; play through the change once and it finds the bar.

        Either way the pass has short fades at its ends, so nothing clicks. With **Repeat** on it loops every Length from bar 1 (a 4-bar drawing starts again on bars 5, 9, 13 and so on), starting on the DAW's bar lines also after a time-signature change.
      - **MIDI** plays the drawing from the start on every note-on (any key; a new note restarts it), sample-accurately. With **Repeat** off it plays once and note-offs are ignored; with **Repeat** on it loops while any key is held and fades out when you let go of the last. Stopping the DAW, All Notes Off or All Sound Off ends it.
      - The line under Trigger says what will happen: "Plays once when you press play", "Plays once from bar 9", "Repeats every 4 bars", "Each note plays it once" or "Loops while a note is held".
11. **Preview.** The round play button plays the drawing from its start, whatever the trigger, even while the DAW is stopped: once (the button then turns itself off), or on a loop with **Repeat** on until you press it again.
12. **Bounce WAV.** **Bounce WAV** renders one pass of the drawing to a WAV file in the background, with your Level. A chip appears with the file's name, such as **ArtFreq HELLO 14-03-27.wav** (the drawing's first text, or "Drawing", and the time): drag it into your DAW, or click **Save...** to keep a copy where you want it. From then on the file keeps up with you: change the drawing, the length, the tempo or the level and the chip says **Updating...** (it cannot be dragged then) until a new file, under a new name, is ready a moment later. So whatever you drag is what the canvas shows, and your DAW never mistakes it for an earlier bounce (DAWs remember audio files by name). Earlier bounces you dragged out stay where the DAW found them; ArtFreq clears its bounce files away after a day, so use **Save...** for anything you want to keep.

The **?** button shows these steps inside the plug-in.

## ArtFreq FX: the drawing in a song

On a full song the drawing's own sound can get lost in everything else going on. ArtFreq FX works the other way round: it **mutes the drawing's frequencies in the song** (Carve), so the drawing shows as dark shapes in the song's spectrogram.

ArtFreq FX has a small window of its own (720 × 440 at 100 %): a bar at the top (Mode, Depth, undo and redo, Live, help and settings), the canvas, and a bar at the bottom (the tools, + Text, + Image, Layers, Length and Repeat). It shows only what you can use right now: a setting that does nothing in the current mode is not there, and nothing is greyed out.

1. **Put ArtFreq FX on an audio track, a bus or the master** (it is listed under audio effects, as "ArtFreq FX" by AnyMeans, category Fx / Filter). It takes mono or stereo. It needs an ArtFreq Pro key (see [Activate](#activate)); a computer already activated with one in ArtFreq is activated for ArtFreq FX too.
2. **Open a spectrogram after it**: MiniMeters on the master, as for ArtFreq. Match the analyser in **Settings** (the gear) as usual.
3. **Draw.** In the bottom bar, **+ Text** and **+ Image** add a text box or a picture (a picture can also be dropped onto the canvas), and the **Brush** and the **Eraser** paint; while one of them is the tool, its **Size**, **Soft** and **Strength** sit in a strip just above the bar, over the canvas's bar numbers (the canvas itself stays put). Click a text or a picture and its settings (the words, font and style, or the picture's mode and clean-up; band, intensity, fill length, delete) slide in over the side of the canvas, on the side away from the object so you can watch it change; for an object too wide for that (a new text, which runs across the whole length, or one you stretched with Fill length) the canvas moves aside, so all of it stays in view while you type or change it. Esc, a click on empty canvas or the **✕** puts them away. **Layers** opens the list of every text and picture in the same place: select, hide, reorder and delete them there as in ArtFreq. The canvas shows the drawing over a stand-in for a loud song (a master at about −10 dBFS RMS), as the analyser will show it in the chosen mode, in the Draw modes with the sines at the Level you set; **Live**, at the top right, shows what actually comes out instead (your song, carved).
4. **Choose the Mode**, and with it the **Depth**, at the top of the window:
   - **Carve** (the default): the drawing's frequencies are muted: dark shapes in the song.
   - **Carve + Draw**: carved a little wider than the drawing (4 canvas rows and about 35 Hz above and below it, about 60 ms before and after it; a sixteenth of the length for drawings under a second), and ArtFreq's sines played in the hole: the drawing shows bright with a dark edge, even in a dense mix.
   - **Stencil**: the opposite of Carve: only the drawing's frequencies pass, everything else is muted.
   - **Draw**: the drawing's sines on top of the audio, which is left as it is. On a loud master they stand only a few dB above the song at the default Level (the canvas shows how far): raise **Level** for more (mind the peaks, below), or use Carve + Draw.
   - **Depth** (−120 to 0 dB, default −80 dB) is how far carved frequencies drop. −80 dB looks black in MiniMeters even over a master's bass and kick; −20 to −40 dB darkens without silencing. Click it to type a value, or drag it. Draw carves nothing, so there Depth is not shown.
   - **Sound**: in Carve + Draw and Draw, which play the drawing's sines, a strip under the top bar holds their settings: **Level** (the sines' level: the song keeps its own), **Density**, **Contrast** and **Lows**, as in ArtFreq. Carve and Stencil play no sines and show no strip.
   - Mode and Depth can be automated, and switching modes or moving Depth while playing never clicks (the carve crossfades from frame to frame, the sines fade in and out).
5. **Press play in your DAW**, anywhere in the song (the space bar works while ArtFreq FX's window has the focus). The drawing is carved once, for one **Length**, starting the moment the DAW starts; stop, press play again, and it is carved again. In **Carve** and **Stencil**, which have nothing to show over silence, it waits for the track's audio: press play at bar 1 on a vocal that comes in at bar 9 and the carve starts at bar 9 (the line beside Length says "Waiting for audio to carve" meanwhile). The Draw modes start at once. **Repeat** loops it every Length instead. A DAW loop shorter than the drawing shows the same first part of it every round, and the line says how much ("Your loop shows 2 of 4 bars"): make the loop longer, or choose a shorter Length. The line beside Length says in words what will happen: "Carves once when you press play", "Draws once from bar 9", "Repeats every 4 bars", "Each note stencils it once" or "Loops while a note is held". **Outside the pass the song passes through untouched**: before and after a single pass, while the DAW is stopped and with nothing drawn (also in Stencil), with 5 ms fades into and out of the carving.
6. **Bounce the track in your DAW.** Carving changes your song, which only the DAW has: export the mix, freeze the track or bounce it in place there. **First choose where it goes**: a render is a playback that starts where the render starts, so with **On play** the drawing lands at the start of the export or freeze (or, in Carve and Stencil, where the track's audio first comes in after it), not necessarily where you heard it. Open **Settings**, choose **Start: At bar** and type the bar, then export. For the same reason ArtFreq FX has no Bounce WAV and no Preview button: there is nothing to render or to play without the song.

**Settings** (the gear) holds what is set once and then left alone. Beside the analyser match, the window size and Reset drawing, that is how the drawing is played:

- **Trigger**: **Transport** (the default: the DAW's play button) or **MIDI** notes, where the DAW sends them to effects (see MIDI, below).
- **Start**, for a single Transport pass: **On play** (the default: wherever the DAW starts playing; in Carve and Stencil, where the track's audio comes in after that) or **At bar** and a bar of the song (1 to 999), which is what you want for an export. These are ArtFreq's "when you press play" and "from bar".
- **Tempo**: **Host** (the DAW's, shown beside it) or **Manual** and a BPM.

They work exactly as in ArtFreq (step 10 above).

**Latency.** ArtFreq FX analyses the audio in frames of 85 ms, so its output comes 85.3 ms (4096 samples at 48 kHz) after its input, in every mode, at every sample rate and also when the host bypasses it. It reports this to the DAW, which delays everything else to match (plug-in delay compensation), so the song stays in time; when you play live through it you hear it 85 ms late. The sines of the Draw modes are delayed as much, so they land in their holes. ArtFreq FX also asks the DAW never to put it to sleep over silent audio (Cubase's "Suspend VST3 plug-in processing when no audio signals are received", Reaper's auto-bypass on silence): asleep, it could not tell a gap in the track from you moving the playhead, and would carve the drawing again at the next audio after the gap. It costs the CPU of a running effect over silence.

**Peaks.** ArtFreq FX does not limit the song: outside the pass it is passed on exactly as it came in. Carving changes the phases a limiter set the peaks with, and the Draw modes add their sines, so on a master limited to −0.3 dBFS the output can go over 0 dBFS (measured on pink noise at −11 dBFS RMS, default drawing and Level: Carve +0.8 dBFS, Carve + Draw +1.8, Draw +2.2; Stencil only takes away). Put ArtFreq FX **before the final limiter** on the master (with MiniMeters after both), or leave that much headroom; otherwise a bounce clips during the pass.

**MIDI.** ArtFreq FX accepts MIDI, so Trigger = MIDI (in Settings) works where the DAW sends MIDI to an audio effect: **Reaper** (MIDI on the track reaches its effects), **Bitwig**, **Cubase** (pick ArtFreq FX as a MIDI track's output) and **Studio One** (route a note track to it). **FL Studio** can too (in the effect's wrapper, set its MIDI input port, and send a MIDI Out channel or controller to that port). **Ableton Live does not send MIDI to audio effects**: use Transport there, which needs no MIDI.

### Moving between the two

ArtFreq and ArtFreq FX save their state in the same format, with ArtFreq FX adding Mode and Depth. But a DAW keeps presets and saved states per plug-in (the two have different plug-in IDs), and none of the DAWs named here (Ableton Live, Cubase, Studio One, Bitwig, Reaper, FL Studio) loads one's preset into the other, so a drawing made in ArtFreq does not open in ArtFreq FX: draw it again there, or add the same pictures and text. (Should a host load one's state into the other, the drawing and the shared settings come along, with Mode and Depth at Carve and −80 dB in ArtFreq FX.)

## Tips

- **The highs show the most detail.** An analyser's frequency bins are evenly spaced in hertz, so on a log scale the top octaves get many bins each and stay sharp. Text and fine pictures read best in the **High** band.
- **The lowest octaves are coarse in every analyser.** The bottom octaves get only a few bins each, so shapes there come out blurred whatever made them. Keep low-band art big and simple. With **Lows** on **Smooth** the shapes stay filled; on **Stripes** ArtFreq keeps its lines there at least 25 Hz apart (closer lines beat against each other and pulse on the analyser), which a long FFT shows as stripes.
- **Keep Level modest if you mix ArtFreq under music.** A spectrogram shows quiet sounds clearly, and a drawing full of highs is easy to hear. Turn Level down until it sits where you want it in the mix, then check that the drawing still shows.
- **Fit the length to the screen.** If your spectrogram shows, say, 8 seconds across its width, a drawing that lasts about 8 seconds fits on screen in one piece.
- **Big solid shapes are loud.** Outline text, or an image in Outline mode, draws the same shape with much less sound.
- **Density:** raise it for solid shapes, lower it for a scan-line look.
- **Clean-up** (images) silences faint backgrounds, grain and JPEG noise; raise it if a photo turns into a grey haze.

## Controls

These tables describe ArtFreq's window. ArtFreq FX has the same canvas, tools, layers and object settings in a window of its own: see [ArtFreq FX's window](#artfreq-fxs-window) below for where they are there.

**Header and toolbar**

| Control | What it does |
| --- | --- |
| **Canvas / Live** | **Canvas** shows the drawing as your analyser will show it, with frequency labels on the left and bars and beats (in the song's time signature) along the bottom: the drawing's own bars, from 1, or the song's bar numbers when it plays once from a bar of the song (Transport, Repeat off, "from bar"). **Live** shows a scrolling spectrogram of ArtFreq's own output, on the same scale, wide enough for a whole pass of the drawing (a quarter more than one pass, from 4 to 20 seconds). |
| **Undo / Redo** | Also Cmd+Z and Cmd+Shift+Z (on Windows Ctrl+Z, and Ctrl+Shift+Z or Ctrl+Y). A whole brush stroke, a drag, a scroll over a bar, holding down an arrow key, or one session of typing in a text box is one step. |
| **?** | The how-to, inside the window. |
| **Gear** | Settings: match your analyser (Scale, Low, High, Slope), window size, and Reset drawing. |
| **Select** | Click an object to select it (a click inside the selected object keeps it, even where another lies over it). Where objects overlap, Option-click (Alt-click), or click again on the same spot, to pick the one underneath. Right-click an object for **Bring to front**, **Send to back**, **Hide** and **Delete**. Drag inside it to move it, drag a handle to resize it. Delete or Backspace removes it; the arrow keys nudge it (Shift: 10 times as far). Click empty canvas to deselect. |
| **Brush / Erase** | Paint or erase. **Size** 2–64 px (on screen), **Soft** 0–100 % (a hard edge to fully feathered), **Strength** 0–100 % (how loud the brush paints). |
| **+ Text / + Image** | Add a text box or a picture. Pictures can also be dropped onto the canvas. |
| **Clear** | Removes all objects and paint (one undo step). |

**Layers** (right-hand panel, with nothing selected)

| Control | What it does |
| --- | --- |
| **Rows** | One per text or picture, the front-most at the top; the paint layer is the last row. Click to select (the list stays), double-click or **›** for the settings, drag to reorder. The order decides which object a click on the canvas picks where they overlap; it never changes the sound. |
| **Eye** | Hide or show. A hidden layer is silent and not drawn, but kept and saved; selected from the list it shows as a dashed outline you can still move. |
| **Bin** (on hover) | Delete the object. |
| **Clear** (paint row) | Clear the paint, keeping text and pictures. |
| **Keys** | With the list focused: Up and Down move the selection, Return opens its settings, Delete removes it, Esc deselects. |
| **Lows** (at the foot) | How the low end plays (below about 2 kHz on a log scale; how far up depends on Density). **Smooth** (the default): sines gliding upwards fill the shapes at every FFT size, with a fine shimmer; the canvas shows the band filled. **Stripes**: separate lines 25 Hz apart, which long FFTs show as stripes (the canvas shows them as scan lines). Switching crossfades, so it can be changed while playing. Not automatable. |

Hiding, reordering and deleting are one undo step each; selecting is none (nor is Lows, which is a setting like Density). New text and pictures go in front, selected.

**Inspector** (right-hand panel, for the selected object; **‹ Layers** goes back to the list)

| Control | What it does |
| --- | --- |
| **Text** | The words; several lines are allowed. |
| **Font** | Inter Bold, Space Grotesk, Jost, Fraunces or DM Mono. |
| **Style** | **Solid** letters, or **Outline** (only the edges of the letters sound). |
| **Mode** (images) | **Auto** (picks one of the next three and says which), **Original** (bright is loud), **Negative** (dark is loud), **Outline** (only the edges). Under Auto, a picture with a transparent background draws its shape. |
| **Clean-up** (images) | 0–90 %. Silences faint backgrounds, grain and noise. |
| **Band** | **Low** 40–250 Hz, **Mid** 300 Hz–3 kHz, **High** 3.5–16 kHz, **Full** the whole visible range. Lit when the object sits exactly on a band. |
| **Fill length** | Stretches the object across the drawing (4 % to 96 % of the length). |
| **Intensity** | 0–100 %. How loud the object is. |
| **Replace... / Delete** | Swap the picture for another file / remove the object. |

**Footer**

| Control | Range | Default | What it does |
| --- | --- | --- | --- |
| **Density** | 16–1024 lines | 256 | Sine lines across the height of the canvas. |
| **Contrast** | 12–72 dB | 40 dB | Distance in level between the brightest and the faintest parts of the drawing. |
| **Level** | −60 to 0 dB | −6 dB | Output volume. A safety clipper keeps ArtFreq's output from going over 0 dBFS. In ArtFreq FX it is the sines' level (Carve + Draw and Draw) and the clipper acts on the sines only: the song keeps its own level and peaks, and the sum can go over 0 dBFS (see [Peaks](#artfreq-fx-the-drawing-in-a-song)). |
| **Length** | 1 beat to 32 bars | 4 bars | How long one pass of the drawing lasts. |
| **Trigger** | Transport, MIDI | Transport | When the drawing plays (see step 10 above). |
| **Repeat** | on/off | off | Off: it plays once (when you press play or from a bar of the song, or per note). On: it loops while the DAW plays, or while a note is held. |
| **when you press play / from bar** | | when you press play | Under Trigger, with Transport and Repeat off: where the single pass starts. Click the words to choose. **When you press play**: where the DAW starts playing, wherever that is. **From a bar of the song**: only at the bar you set. |
| **bar** (after "from bar") | 1–999 | 1 | The bar the single pass starts on. Drag it or click to type (Enter to set, Esc to cancel). |
| **Tempo** | Host, Manual | Host | Where the tempo comes from. |
| **BPM** | 20–300 | 120 | The manual tempo. With Host, it shows the host's tempo, greyed. |
| **Preview** | on/off | off | Plays the drawing from its start: once, or on a loop with Repeat. |
| **Bounce WAV** | | | Renders one pass to a WAV file to drag into the DAW or save; after that, every change renders a new file by itself (the chip says **Updating...** until it is ready). |

### ArtFreq FX's window

Two bars and the canvas; everything else comes when it has something to act on and goes when it does not. Nothing is ever greyed out.

| Where | Control | What it does |
| --- | --- | --- |
| Top bar | **Mode** | **Carve**, **Carve + Draw**, **Stencil** or **Draw**: what happens to the audio passing through (see [ArtFreq FX](#artfreq-fx-the-drawing-in-a-song)). Automatable. |
| | **Depth** | −120 to 0 dB, default −80 dB: how far carved frequencies drop. Click to type, drag to change. Automatable. Not shown in Draw, which carves nothing. |
| | **Undo / Redo** | Each appears once there is something to undo or redo. Also Cmd+Z and Cmd+Shift+Z. |
| | **Live** | Lit: a scrolling spectrogram of what comes out of ArtFreq FX takes the canvas's place (and the tools, which have nothing to act on there, make way). Click it again for the canvas. |
| | **?** and **Gear** | The how-to, and Settings. |
| Sound strip, under the top bar (Carve + Draw and Draw) | **Level**, **Density**, **Contrast**, **Lows** | The settings of the drawing's sines, with the ranges and defaults of the Footer and Layers tables above. Level is the sines' level: the song keeps its own. Drag a number, or click it to type. |
| Bottom bar | **Select**, **Brush**, **Erase** | The tools, as above (V, B and E). An object's right-click menu lists **Bring to front** and **Send to back** only where they would change something. |
| | **+ Text**, **+ Image** | Add a text box or a picture; its settings open in the drawer. Pictures can also be dropped onto the canvas. |
| | **Layers** | Opens the layer list in the drawer, and closes it again. Lit while the list is open. |
| | The line of words | What will happen when you press play, or when a note arrives: "Carves once when you press play", "Carves once from bar 9", "Repeats every 4 bars", "Each note carves it once", "Loops while a note is held" (with the mode's own verb: draws, stencils). In the playing colour while it happens. While the DAW plays over silence and the pass waits for the track's audio: "Waiting for audio to carve". Under a DAW loop shorter than the drawing (when you press play, or with Repeat): how much of it each round shows, "Your loop shows 2 of 4 bars". |
| | **Length**, **Repeat** | As in the Footer table above. |
| Brush strip, on the bottom bar (Brush or Erase is the tool) | **Size**, **Soft**, **Strength** | As above. It lies over the canvas's bar numbers, so choosing a tool never moves the drawing. |
| Drawer, over one side of the canvas | The layer list, or the selected object's settings | As the Layers and Inspector tables above (adding, the brush and Lows have their own places in this window). It opens on the right, or on the left if it would otherwise cover the selected object or the spot you just clicked; for an object wider than the room beside it, the canvas moves aside and the drawer sits next to it. **✕** puts it away and keeps the selection; Esc or a click on empty canvas deselects; with nothing selected, Esc or the Layers button closes the list. |
| Settings (the gear) | **Scale**, **Low**, **High**, **Slope**, **Window size**, **Reset drawing** | As below. |
| | **Trigger** | Transport or MIDI, as in the Footer table. Beside it: which DAWs send MIDI notes to an effect. |
| | **Start** | **On play** or **At bar** and the bar (1–999): where a single Transport pass starts. Not shown with MIDI or Repeat, which have no start to choose; the row says where they start instead. |
| | **Tempo** | **Host** (the row shows the DAW's tempo) or **Manual** and a BPM field (20–300). |

**Settings** (match your analyser; not automatable, since changing them mid-song would move the whole drawing)

| Control | Range | Default |
| --- | --- | --- |
| **Scale** | Log, Linear, Mel | Log |
| **Low** | 10–1000 Hz | 20 Hz |
| **High** | 2–24 kHz | 20 kHz |
| **Slope** | 0, 3, 4.5, 6 dB/oct | 0 dB/oct |
| **Window size** | 75 %, 100 %, 125 %, 150 %, 200 % | 100 % (1120 × 720; ArtFreq FX 720 × 440) |

Knobs: drag to turn, hold Shift for fine adjustment, double-click to reset, double-click the value to type one. The window can also be resized from its corner (75–200 %, fixed proportions); its size is saved with the session, as are the drawing (pictures included) and every setting.

Keys: **Space plays your DAW, also while this window has the focus; while you type in a text box it types a space.** A number has no spaces, so while you type one (in a field such as Depth, Level or the start bar, or a knob's value) Space sets what you typed, as Return does, and plays your DAW. ArtFreq uses only a few keys itself (V, B and E for the tools, Cmd+Z and Cmd+Shift+Z, Delete and the arrow keys on a selected object, also in the Live view, Esc, and the list's keys above) and hands every other key on to the DAW, so its shortcuts keep working while you draw. ArtFreq FX does the same. A text box has the keyboard only while you type in it: **Return** (or Esc, or a click anywhere else) finishes and gives the keys back; in an object's text, **Shift+Return** starts a new line. Clicking a text object selects it without starting to type; double-click it to edit its text.

Automation: Level, Length, Trigger, Repeat, Start bar, Tempo and BPM can be automated (and in ArtFreq FX Mode and Depth) (in the automation list Trigger reads Transport, MIDI note). Where a single pass starts (when you press play, or from a bar) is not in the list: it is how you use the plug-in, not something to perform. Density, Contrast and Lows cannot be automated either: the sound is rebuilt from them about 25 times a second by the clock, not in step with the song, so in a bounce faster than real time their changes would land in the wrong place.

Projects saved before Lows existed (ArtFreq 0.1 and 0.2) open with Lows on **Stripes**, so they look and sound as they did; new instances start on **Smooth**. Projects saved with ArtFreq 0.1 open with Repeat off and from bar 1, so a Transport drawing that looped through the song now plays once from bar 1: turn **Repeat** on to have it loop again. The old **Free loop** trigger, which played by itself, opens as Transport with Repeat on. Automation recorded on the old Trigger is not rewritten: there, Free loop now reads as MIDI note.

Projects saved with ArtFreq 0.4 or earlier always played a single Transport pass from a bar. Where that bar was still 1, the drawing now plays **when you press play**, wherever the playhead is (to have it only at bar 1 again, click the words under Trigger and choose **From a bar of the song**). Where you had set another bar, the project opens with **from bar** and that bar, and plays where it did.

ArtFreq FX had ArtFreq's large window until 0.4. A project saved with it opens ArtFreq FX at the same percentage of its new, smaller window: 100 % stays 100 % (now 720 × 440), 150 % stays 150 %.

## How it works

- **The drawing.** A paint layer plus any number of text and picture objects, each of which can be hidden. Objects are placed by time (a fraction of the length) and by frequency in hertz, so "in the highs" keeps meaning the highs if you change the analyser scale; the paint layer remembers the scale it was painted on, so brush strokes keep their frequencies too. Everything is flattened into a 1024 × 1024 picture of loudness values (the maximum of all the layers that are shown): the canvas.
- **Sine lines.** The canvas becomes a bank of sine waves, one per line, spaced evenly in canvas height (so evenly on your analyser's scale). A sine at a fixed frequency shows in a spectrogram as a horizontal line at that height, and its loudness over time draws the picture along it. Each line takes the brightest part of the canvas rows nearest to it (halfway to the lines either side), so thin strokes never fall between lines. Brightness becomes level through Contrast; the analyser's Slope is compensated (around 1 kHz); and the whole drawing is scaled so its loudest moment sits at −12 dBFS RMS before Level.
- **Smooth lows.** On a log scale the lines must crowd together in the lows, where they are merged until they stand at least 25 Hz apart (closer ones would beat). An analyser with a long FFT resolves lines that far apart, so a shape there shows as stripes with dark gaps. With Lows on **Smooth** those crowded lines are replaced by a rising comb: sines 20 Hz apart that all glide upwards at 200 Hz a second, each fading out at the top of the band and back in at the bottom. Every height is passed by a sine ten times a second, so short FFT windows blur neighbouring sines together and long ones see each sine smeared along its glide: the band looks filled at 4096 to 32768 points, with a fine rising shimmer rather than stripes or a pulse. Each sine takes its loudness from the drawing at the height it has reached (a 5 Hz grid of the canvas), fading in and out over about 15 Hz at a shape's edges so it splashes less energy outside it. A stroke thinner than the sines' spacing (20 Hz: a one-row line, an outline, thin lettering) would only be lit while a sine passed through it, blinking ten times a second, so it is played by the nearest of the lines the comb replaced instead, steady, as on Stripes. The comb is played 3.5 dB louder than a line, as its energy is spread along its glide, which makes it as bright as the lines above it on an analyser of 8192 points. The lines above the comb (about 2 kHz up) are unchanged.
- **Playback.** One oscillator per line (and the comb's gliding sines), its level following the drawing as playback moves across it. Starts, stops, jumps, retriggers and the host's bypass fade in a few milliseconds, and a change to the drawing crossfades in 20 ms, so nothing clicks. Lines too close to the top of the sample rate's range are skipped, so nothing aliases. A safety clipper (transparent below −1 dBFS) follows Level.
- **Timing.** The position in the drawing comes from the DAW's transport (a single pass at a bar of the song, or looping) or from MIDI notes (once each, or looping while held), at the host's tempo or yours.
- **Real time.** The canvas and the sine plan are rebuilt on the message thread within a few tens of milliseconds of each edit and handed to the audio thread without locks; the audio thread never allocates or waits.

**Why sound and not MIDI?** A spectrogram shows the frequencies present at each moment. MIDI notes can only make the few pitches of a synth patch, on a semitone grid, with harmonics drawn on top; pictures need hundreds of exactly placed frequencies. So ArtFreq *is* the instrument: MIDI is used as a trigger, and the drawing can also be bounced to a WAV file.

## Install

Downloads are on the repository's **Releases** page, under **Assets**. Every successful build replaces the release for the current version. Both plug-ins are VST3 only, for 64-bit DAWs.

### macOS: installer

Download `ArtFreq-<version>-macOS.pkg` and open it. It installs both plug-ins, `ArtFreq.vst3` (the instrument) and `ArtFreq FX.vst3` (the effect), into `/Library/Audio/Plug-Ins/VST3/` (**Customize** can leave one out). Then rescan plug-ins in your DAW. It runs on Apple Silicon and Intel Macs with macOS 10.15 or later.

The installer is not notarised by Apple, so macOS will not open it with a double-click the first time:

- **macOS 15 and later:** double-click it and close the warning, then go to **System Settings > Privacy & Security**, scroll down to the message about the ArtFreq installer and click **Open Anyway**, then confirm.
- **Earlier versions:** right-click (or Control-click) it, choose **Open**, then **Open** again.

At the end the installer offers to move itself to the Trash.

### macOS: by hand

Download `ArtFreq-VST3-macOS.zip`, unzip it, and copy `ArtFreq.vst3` and `ArtFreq FX.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (just for you) or `/Library/Audio/Plug-Ins/VST3/` (every user). Then rescan plug-ins in your DAW.

A manually copied plug-in keeps the download quarantine flag. If the DAW refuses to load them, clear the flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ArtFreq.vst3 "$HOME/Library/Audio/Plug-Ins/VST3/ArtFreq FX.vst3"
```

### Install on Windows

For Windows 10 or 11, 64-bit. There is no installer yet: download `ArtFreq-VST3-Windows.zip`, then

1. Close your DAW.
2. Unzip the zip and copy the folders `ArtFreq.vst3` and `ArtFreq FX.vst3`, whole, into `C:\Program Files\Common Files\VST3`. Windows asks for permission to copy there.
3. Open your DAW and rescan plug-ins. ArtFreq is listed under instruments, ArtFreq FX under audio effects.

`How to install.txt` in the zip says the same. To remove ArtFreq, delete the two folders again.

### Windows and macOS differences

- **Pictures:** on Windows ArtFreq reads PNG, JPEG and GIF. On a Mac, macOS also reads HEIC, WebP, TIFF and BMP for it. **+ Image** and dropping a file offer only what the computer can read.
- **Keys:** Ctrl where a Mac uses Cmd (undo Ctrl+Z, redo Ctrl+Shift+Z or Ctrl+Y), Alt where it uses Option.
- **Bounce WAV:** the files go to the system's temporary folder (on Windows `%TEMP%\ArtFreq`). Drag the chip into your DAW in the same way, or use **Save...**.
- **ArtFreq FX** uses more CPU on Windows: a Mac does its frequency analysis with Apple's fast built-in maths library, Windows with JUCE's own. If it crackles at a high sample rate, raise your DAW's buffer size.

## Activate

ArtFreq and ArtFreq FX are activated once per computer, with the key from your purchase email (it looks like `XXXXX-XXXXX-XXXXX-XXXXX`).

1. Open the plug-in's window. While the computer is not activated, the window shows **Activate ArtFreq** (or **Activate ArtFreq FX**) over a dimmed canvas.
2. Click into the key field and paste or type the key, in any form: lower case, with or without dashes or spaces, even with the words around it from the email. It shows in groups of five as you type. A key pasted over one that was refused replaces it.
3. Press **Activate** (or Return). It takes a moment; then the window says what is unlocked, and **Continue** takes you to the plug-in. Every open instance unlocks at once, and so does the other plug-in when the key covers it.

- **Which key unlocks what.** An **ArtFreq** key unlocks the ArtFreq instrument. An **ArtFreq Pro** key unlocks ArtFreq and ArtFreq FX: enter it in either and both are activated on that computer. ArtFreq FX comes only with ArtFreq Pro, so an ArtFreq key typed into ArtFreq FX is refused, and the window says so.
- **Two computers per key.** A third is refused, with the server's message and the support address. To move to a new computer, **deactivate on the old one first**: **Settings** (the gear) > **Licence** > **Deactivate...**, which says what will be locked before it does it. If the old computer is gone, write to support@anymeansaudio.com.
- **Two keys on one computer** (an ArtFreq key, and later an ArtFreq Pro key for ArtFreq FX): each is deactivated on its own. **Deactivate...** frees the newer one first and says what stays activated with the other; to move everything, deactivate again.
- **Every account on the computer.** The activation belongs to the computer: once it is activated, ArtFreq is activated for every user account on it, and deactivating in any account locks it in all of them.
- **Let a deactivation finish.** If the project or the DAW is closed while **Deactivate** is still waiting for the server, the computer is locked afterwards (the server may have freed it): **Settings** > **Licence** offers **Deactivate** to finish, or activate again with your key to keep using it there.
- **Offline.** Activating needs the internet once. After that ArtFreq never asks again on that computer and never goes online: the activation is checked on the computer itself, at every start, against a licence the server signed. Only **Activate** and **Deactivate** reach the server.
- **When something goes wrong** the window says what in plain words: a typo in the key (caught before anything is sent), a key the server does not know or that is for another plug-in, a key already on two computers, or "Couldn't reach the server — check your connection and try again." You can always try again. Every refusal names the support address, which can be selected and copied (a double-click selects it whole). Support: **support@anymeansaudio.com**.
- **While it is locked** ArtFreq makes no sound and ignores MIDI and Preview, and has no Bounce WAV; ArtFreq FX passes your audio through **unchanged**, with its usual latency, so it never silences a track or a master and nothing shifts once it is activated. Projects open and save as usual, automation keeps working, and the drawing is all there once the computer is activated (the settings offer no **Reset drawing** until then).
- **Where it is kept.** The activation is in a small file shared by all AnyMeans plug-ins and every account of the computer: `/Users/Shared/AnyMeans/Licenses.json` on a Mac, `%ProgramData%\AnyMeans\Licenses.json` on Windows. A copy of it on another computer unlocks nothing there.

## Build from source

Requirements: CMake 3.22 or newer and a C++17 compiler (Xcode on macOS, Visual Studio 2022 with the C++ desktop workload on Windows). JUCE 8.0.15 is downloaded automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The plug-ins are written to `build/ArtFreq_artefacts/Release/VST3/ArtFreq.vst3` (target `ArtFreq`) and `build/ArtFreqFX_artefacts/Release/VST3/ArtFreq FX.vst3` (target `ArtFreqFX`; mind the space when you script it). For a universal binary (Apple Silicon and Intel), as released, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring. On Windows the same two commands use Visual Studio's generator (add `-A x64` when configuring to be sure of a 64-bit build); each `.vst3` is a folder with the plug-in itself at `Contents/x86_64-win/<name>.vst3`, and the C runtime is linked in, so it needs no Visual C++ redistributable.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `ARTFREQ_BUILD_TESTS` | ON | Build `ArtFreqTests`. |
| `ARTFREQ_BUILD_TOOLS` | ON | Build the `ArtFreqSnapshot` interface snapshot tool, the `ArtFreqLab` lows lab, the `ArtFreqCarveLab` carving lab and the `ArtFreqTruthLab` accuracy check. |
| `ARTFREQ_COPY_PLUGIN_AFTER_BUILD` | OFF | Copy the plug-ins into your VST3 folder after each build (on Windows `C:\Program Files\Common Files\VST3`, which needs a build run as administrator). |
| `ARTFREQ_ENABLE_LTO` | ON | Link-time optimisation in Release builds (turn it off for faster development builds). |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | *(download)* | Use a local JUCE checkout instead of downloading it. |

To build the macOS installer from a build folder (macOS only):

```sh
installer/macos/build-pkg.sh build out        # writes out/ArtFreq-<version>-macOS.pkg, with both plug-ins
```

The version comes from `project(ArtFreq VERSION ...)` in `CMakeLists.txt` unless given as a third argument. It needs a Release build of both targets (`ArtFreq` and `ArtFreqFX`). JUCE signs a plug-in before it writes `moduleinfo.json` into it, which breaks the signature's seal; the script signs its copies again (ad hoc) when needed.

On Linux, install the JUCE dependencies first:

```sh
sudo apt-get install libasound2-dev libx11-dev libxext-dev libxrandr-dev \
    libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev
```

### Tests

```sh
ctest --test-dir build -C Release --output-on-failure
```

or run `build/ArtFreqTests_artefacts/Release/ArtFreqTests [filter]` to run only the tests whose names contain `filter`. `ArtFreqTests` needs no host. It checks the frequency-scale maths; the document's save and load; the image modes (auto-invert on a white background, transparency, outline); text rendering; compositing and placement; the plan (frequencies, spacing, normalisation, contrast, slope); Smooth lows (the lines the comb replaces, its loudness grid and softened edges, thin strokes held steady on lines, no clicks or rumble where the sines fade in at the bottom, its power staying in the drawn band, determinism, crossfades, speed, the canvas showing the band filled); the plan exchange between threads; the engine (frequency of a single line, silence, click-free crossfades and jumps, the aliasing guard, real-time speed); the playhead (transport positions, tempo changes and ramps, bar lines, the single pass that starts when the DAW starts playing (anywhere in the song, at any block size, when ArtFreq first runs while the DAW already plays, stop and start, DAW loops, relocations, tempo changes, a typed tempo, hosts that give no position, a count-in, settings switched while playing), the single pass from a start bar and how the DAW's position, relocations, loops, time-signature changes and a typed tempo play it, MIDI one-shots and how they end, MIDI loops held by keys, preview once and looping, jumps); end to end, a canvas rendered to audio and analysed back must match the canvas, and Smooth must fill the lows where Stripes leaves gaps at FFT sizes 8192 and 16384, read closer to the drawing and not pulse; the processor (state round trip, Lows on Smooth for new instances and on Stripes for projects saved before it, switching Lows while playing without a click, projects saved before Repeat and the Free loop trigger, projects saved before "when you press play" existed, sound at once with nothing touched when the DAW starts in bar 33, a single pass and a held loop, a single pass through time-signature changes and at a typed tempo, Preview following Repeat, MIDI and panic, bypass fades, the time signature, level and clipper bounds); the WAV export (and its file names); spectral carving (the mask plan and its halo, the carving engines: transparency, depth, Stencil, latency, block sizes, the gate, thin strokes, column edges, the halo per mode, the drawing read over a hop's callbacks and the plan a frame began with); ArtFreq FX through the real processor (its layout and parameters, the latency at every rate and in bypass, the audio untouched outside the pass and with nothing drawn, Carve and Stencil on a sine in a drawn band, Carve + Draw's wider hole and Level, Draw adding exactly the instrument's sound as late as the audio, mode and depth changes without clicks, states moving between the two plug-ins, mono and stereo, block sizes, MIDI and Preview, a host's flush, the masks a frame still reads kept alive, the peaks on a limited master, the worst callback at 48 to 192 kHz, Carve and Stencil waiting for a track's audio that comes in after play, and one pass, not one per audio region, in a host that suspends effects over silence); and the editor, driven with synthetic mouse, wheel and key events (bounce files and the export following the drawing, undo steps, selecting and placing objects, the layer list and hidden layers, picking overlapping objects, the live view's span, pictures that cannot be read; in ArtFreq FX its small window, what each mode and tool shows (and that nothing shown is ever greyed), the line that says what will happen, the drawer for the layer list and for an object's settings (a wide object's beside the canvas, all of it in view), the Brush strip leaving the canvas as it was, the timing in Settings, the panels fitting the window, the window sizes, a size saved with 0.4's large window, and the canvas in each mode and following Level). The keyboard is tested with the editor in a real window that has the keyboard, driven the way the system drives it (Linux only): the space bar comes back unused, so the DAW gets it, after a click on anything and in both plug-ins (also one click on a number field: the space bar sets what was typed and plays the DAW), text boxes give the keys back on Return, Esc, Tab or a click elsewhere, Delete and the arrows act on the selection in the Live view too, and on the activation screen the key field takes typing only while it is clicked into, and a key pasted over one that was refused replaces it. Licensing is tested against a fake server, never the real one: the licence code itself (`src/license/README.md`: also one file for every account of a computer, a deactivation cut off by a closed project leaving the computer locked, two keys on one computer), a real licence from the live server checked with the key built into the plug-ins, the locked sound (the instrument silent, then fading in when activated, with no clicks either way; ArtFreq FX passing the audio through bit for bit with its latency, then carving), instances following each other's activation, and the activation screen and the Licence card through every answer the server gives (a good key, a typo, 403 with the support address, 409, no connection, an unsigned answer, a request on its way, the window closed during one, a deactivation that fails, one that keeps a second key, one cut off by closing the project) and no Reset drawing while locked. Every other test runs with the plug-ins activated, in a folder of the test runner's own. The editor tests need a display: on Linux run it under `xvfb-run -a`.

### Snapshot tool

`ArtFreqSnapshot <folder> [pixel-scale]` renders the editor in its main states (canvas and live view, selected objects, the layer list, Lows on Stripes, the brush, the settings and help panels, "Plays once when you press play" and its menu, Repeat and the start bar, the footer at every window size, other window sizes; and ArtFreq FX's own window, as `artfreq-fx-*.png`: its "ArtFreq" selected and a new text typed into, beside the drawer, each mode, a text's and a picture's settings and the layer list in its drawer, an object's right-click menu, the brush, Repeat, the Length menu, its settings and help, its live view of a carved stand-in song, an empty drawing and 75, 100, 150 and 200 %, checking in every state that nothing overlaps, nothing is cut off and nothing shown is greyed; and, for both plug-ins, the activation screen (empty, typing, busy, an error, the success, after a deactivation, at 75 to 200 %, after a deactivation that didn't finish) and the Licence card in the settings (activated, asking, a failure, not activated, two keys, a deactivation that didn't finish), as `artfreq-activate-*.png`, `artfreq-licence-*.png` and their `artfreq-fx-` twins) to PNG files, without a host, with the plug-ins activated against a fake server, and checks what the controls do along the way. At the end (on Linux, under the virtual display) it puts each plug-in's window on the display, gives it the keyboard and sends the space bar through the window in every place the focus can be, checking that it comes back unused (so the DAW would get it) and that it is typed where text is being typed. On macOS it runs as it is; on Linux run it under a virtual display (`xvfb-run -a ArtFreqSnapshot out`). With `ARTFREQ_SNAPSHOT_STATES=1` in the environment it also saves, beside each PNG, the plug-in's state (`.state`) and where the canvas lies in the picture (`.json`).

### Accuracy check

The canvas is a preview. `ArtFreqTruthLab <snapshot-folder> <out>` plays each screenshot's saved state through the real (activated, test-licensed) plug-in, ArtFreq FX on a dense synthetic song, analyses the output as an FFT spectrogram such as MiniMeters' does (Hann, FFT 4096, 8192 and 16384, log scale; MiniMeters' own settings are assumed, not measured) and puts it beside the screenshot's canvas, with numbers per band (`metrics.txt`). `ArtFreqTruthLab --marketing <out>` renders real-output images for the website. The findings, and what they mean for the screenshots: [docs/ACCURACY.md](docs/ACCURACY.md).

### Continuous integration

Every push builds both platforms on GitHub Actions (`.github/workflows/build.yml`). Nothing there reaches the licence server or activates anything: the tests and snapshots use a fake server, and pluginval validates the plug-ins as they are on a computer that is not activated (the instrument silent, ArtFreq FX passing audio through):

- **macOS:** both universal plug-ins; the tests, natively and again on the Intel half under Rosetta; the interface snapshots of both (artifact **ArtFreq-Snapshots**); pluginval at strictness 10; the installer, test-installed.
- **Windows** (64-bit, Visual Studio 2022): both plug-ins, checked to need no Visual C++ redistributable; the tests; pluginval at strictness 10; the zip with `How to install.txt`, unzipped again and its copies validated.

When every build succeeds, all the downloads are published as the release `v<version>`. To build one platform only (macOS minutes cost ten times as much), run the workflow by hand (**Actions > Build > Run workflow**) and choose **platform** macOS or Windows: that run publishes no release, and its downloads stay on the run's page. A manual run of **all** builds both and publishes, like a push.

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | The drawing: document, text, pictures, compositing, the frequency scale, fonts, and the plan that turns the canvas into sine lines. No audio-thread code, no UI. |
| `src/dsp/` | Everything the audio thread runs (the engine, the playhead tracker, the plan exchange, the spectral carvers of ArtFreq FX), plus the WAV export and the live analyser. |
| `src/ui/` | The editor: header, toolbar, canvas, inspector, live view, footer, settings and help. |
| `src/Parameters.*` | Parameter ids, ranges and defaults, shared by the processor, the editor and the tests. |
| `src/PluginProcessor.*` | The host-facing plug-in, both variants (instrument and effect): parameters, state, undo, plan and mask building, MIDI and transport, carving, and what it does while locked. |
| `src/license/` | Activation and the signed licence, shared with the other AnyMeans plug-ins (see `src/license/README.md`). The activation screen and the Licence card are `src/ui/Licensing.*`. |
| `src/plugin/` | One entry point per plug-in target (`createPluginFilter`): the only code that differs between ArtFreq and ArtFreq FX. |
| `tests/` | `ArtFreqTests`. |
| `tools/` | The interface snapshot tool, the lows lab (`ArtFreqLab`: renders test drawings with Smooth or Stripes lows, analyses them like a spectrogram at several FFT sizes and measures fill, fidelity, flicker and leakage) the carving lab (`ArtFreqCarveLab`: plays songs through a carving engine with test drawings and measures what an analyser shows) and the accuracy check (`ArtFreqTruthLab`: the screenshots' states played through the real plug-in and compared with their canvas). |
| `assets/fonts/` | The embedded fonts and their licences. |
| `installer/macos/` | The macOS installer (both plug-ins): `build-pkg.sh`, its distribution file, pages and postinstall script. |
| `installer/windows/` | `How to install.txt`, which goes into the Windows zip (there is no Windows installer yet). |
| `docs/DESIGN.md` | The design. |

## Licences

- **ArtFreq** is © 2026 AnyMeans and released under the GNU AGPLv3; see `LICENSE`.
- **JUCE 8** is dual-licensed: AGPLv3, or a commercial licence from JUCE. Distributing ArtFreq without publishing its source under the AGPLv3 requires a JUCE licence.
- The **VST3 SDK** bundled with JUCE is used under its own licence. VST is a trademark of Steinberg Media Technologies GmbH.
- The embedded fonts (Inter, Space Grotesk, Jost, Fraunces, DM Mono) are used under the SIL Open Font License 1.1; see `assets/fonts/OFL-*.txt`.
