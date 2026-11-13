# FreqArt

**by AnyMeans**

An instrument plug-in (VST3, macOS) that turns text, pictures and brush strokes into sound whose **spectrogram shows them**. Put it on an instrument track, open a spectrogram such as MiniMeters on its output, press play, and your drawing scrolls past.

The canvas is a picture of your spectrogram: left to right is time across the drawing's length (1 beat to 32 bars, in time with the song), bottom to top is frequency on the same scale as your analyser (logarithmic, 20 Hz to 20 kHz, by default). What you see on the canvas is what the analyser will show.

## How to use it with MiniMeters

1. **Put FreqArt on an instrument track.** It is an instrument: it makes the sound itself and needs no audio input. Create an instrument (MIDI) track and load FreqArt on it. A fresh instance already has something to show: the word "FreqArt" in the mid band.
2. **Open the spectrogram.** In MiniMeters, choose the **Spectrogram** with a **logarithmic** frequency scale from **20 Hz to 20 kHz**, listening to your DAW's output. Any analyser with a scrolling spectrogram works.
3. **Match the settings.** Open FreqArt's **Settings** (the gear) and set **Scale**, **Low**, **High** and **Slope** to what your analyser uses. The defaults (Log, 20 Hz, 20 kHz) match the MiniMeters setup above; if your analyser tilts its display (MiniMeters has a Slope setting), give FreqArt the same Slope.
4. **Press play in your DAW.** With the default trigger (Transport), FreqArt plays while the DAW plays. The drawing scrolls through the spectrogram; FreqArt's own **Live** view (the Canvas | Live switch at the top of the window) shows the same thing.
5. **Make it yours.**
   - **+ Text** adds a text box in the mid band across the whole length (or, if something is already there, in a free band), ready for typing. Pick a font and Solid or Outline letters in the inspector on the right.
   - **+ Image** adds a picture (PNG, JPEG, GIF, HEIC, WebP, TIFF or BMP), placed like new text, or drop an image file straight onto the canvas: it lands in the band under the cursor. A file that cannot be read is reported at the bottom of the canvas. **Mode** decides what in the picture becomes sound; **Auto** usually gets it right (on a white background it draws the dark parts).
   - **Brush** and **Erase** paint on the canvas, with Size, Soft and Strength in the toolbar.
6. **Place it with the bands.** Select an object and choose a **Band** in the inspector: **Low** (40–250 Hz), **Mid** (300 Hz–3 kHz), **High** (3.5–16 kHz) or **Full** (the whole visible range). Or drag it to move it, and drag its handles to resize it. **Fill length** stretches it across the drawing.
7. **Set the density.** **Density** is the number of sine lines across the height of the canvas. Few lines give a scan-line look; many lines merge into solid shapes.
8. **Set the contrast.** **Contrast** is how many decibels separate the brightest parts of the drawing from the faintest. To reproduce greys faithfully, set it near the range your spectrogram shows from dark to full colour; lower it to bring out faint detail, raise it for deeper blacks.
9. **Set the level.** **Level** is FreqArt's output volume. FreqArt sets the loudest moment of the drawing to −12 dBFS RMS before Level, so at the default −6 dB it peaks at about −18 dBFS RMS.
10. **Choose the length, tempo and trigger.**
    - **Length** is how long one pass of the drawing takes: 1 beat, 2 beats, 1 bar, 2 bars, 4 bars (default), 8, 16 or 32 bars, at the song's tempo and time signature.
    - **Tempo**: **Host** follows the DAW's tempo; **Manual** uses the **BPM** you type (also used when a host gives no tempo).
    - **Trigger**: **Transport** follows the DAW's timeline and loops every length from bar 1 (a 4-bar drawing starts again on bars 5, 9, 13 and so on), starting on the DAW's bar lines also after a time-signature change, and plays only while the DAW plays. **MIDI** plays the drawing once from the start on every note-on (any key; a new note restarts it; note-offs are ignored), sample-accurately; stopping the DAW, All Notes Off or All Sound Off ends the pass. **Loop** plays all the time, looping.
11. **Preview.** The round play button plays the drawing on a loop from its start, whatever the trigger, even while the DAW is stopped.
12. **Bounce WAV.** **Bounce WAV** renders one pass of the drawing to a WAV file in the background, with your Level. A chip appears with the file's name, such as **FreqArt HELLO 14-03-27.wav** (the drawing's first text, or "Drawing", and the time): drag it into your DAW, or click **Save...** to keep a copy where you want it. From then on the file keeps up with you: change the drawing, the length, the tempo or the level and the chip says **Updating...** (it cannot be dragged then) until a new file, under a new name, is ready a moment later. So whatever you drag is what the canvas shows, and your DAW never mistakes it for an earlier bounce (DAWs remember audio files by name). Earlier bounces you dragged out stay where the DAW found them; FreqArt clears its bounce files away after a day, so use **Save...** for anything you want to keep.

The **?** button shows these steps inside the plug-in.

## Tips

- **The highs show the most detail.** An analyser's frequency bins are evenly spaced in hertz, so on a log scale the top octaves get many bins each and stay sharp. Text and fine pictures read best in the **High** band.
- **The lowest octaves are coarse in every analyser.** The bottom octaves get only a few bins each, so shapes there come out blurred whatever made them (FreqArt also keeps its lines at least 25 Hz apart, because closer lines beat against each other and pulse on the analyser). Keep low-band art big and simple.
- **Keep Level modest if you mix FreqArt under music.** A spectrogram shows quiet sounds clearly, and a drawing full of highs is easy to hear. Turn Level down until it sits where you want it in the mix, then check that the drawing still shows.
- **Fit the length to the screen.** If your spectrogram shows, say, 8 seconds across its width, a drawing that lasts about 8 seconds fits on screen in one piece.
- **Big solid shapes are loud.** Outline text, or an image in Outline mode, draws the same shape with much less sound.
- **Density:** raise it for solid shapes, lower it for a scan-line look.
- **Clean-up** (images) silences faint backgrounds, grain and JPEG noise; raise it if a photo turns into a grey haze.

## Controls

**Header and toolbar**

| Control | What it does |
| --- | --- |
| **Canvas / Live** | **Canvas** shows the drawing as your analyser will show it, with frequency labels on the left and bars and beats (in the song's time signature) along the bottom. **Live** shows a scrolling spectrogram of FreqArt's own output, on the same scale, wide enough for a whole pass of the drawing (a quarter more than one pass, from 4 to 20 seconds). |
| **Undo / Redo** | Also Cmd+Z and Cmd+Shift+Z. A whole brush stroke, a drag, a scroll over a bar, holding down an arrow key, or one session of typing in a text box is one step. |
| **?** | The how-to, inside the window. |
| **Gear** | Settings: match your analyser (Scale, Low, High, Slope), window size, and Reset drawing. |
| **Select** | Click an object to select it (a click inside the selected object keeps it, even where another lies over it). Drag inside it to move it, drag a handle to resize it. Delete or Backspace removes it; the arrow keys nudge it (Shift: 10 times as far). Click empty canvas to deselect. |
| **Brush / Erase** | Paint or erase. **Size** 2–64 px (on screen), **Soft** 0–100 % (a hard edge to fully feathered), **Strength** 0–100 % (how loud the brush paints). |
| **+ Text / + Image** | Add a text box or a picture. Pictures can also be dropped onto the canvas. |
| **Clear** | Removes all objects and paint (one undo step). |

**Inspector** (right-hand panel, for the selected object)

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
| **Level** | −60 to 0 dB | −6 dB | Output volume. A safety clipper keeps the output from going over 0 dBFS. |
| **Length** | 1 beat to 32 bars | 4 bars | How long one pass of the drawing lasts. |
| **Trigger** | Transport, MIDI, Loop | Transport | When the drawing plays (see step 10 above). |
| **Tempo** | Host, Manual | Host | Where the tempo comes from. |
| **BPM** | 20–300 | 120 | The manual tempo. With Host, it shows the host's tempo, greyed. |
| **Preview** | on/off | off | Plays the drawing on a loop from its start. |
| **Bounce WAV** | | | Renders one pass to a WAV file to drag into the DAW or save; after that, every change renders a new file by itself (the chip says **Updating...** until it is ready). |

**Settings** (match your analyser; not automatable, since changing them mid-song would move the whole drawing)

| Control | Range | Default |
| --- | --- | --- |
| **Scale** | Log, Linear, Mel | Log |
| **Low** | 10–1000 Hz | 20 Hz |
| **High** | 2–24 kHz | 20 kHz |
| **Slope** | 0, 3, 4.5, 6 dB/oct | 0 dB/oct |
| **Window size** | 75 %, 100 %, 125 %, 150 %, 200 % | 100 % (1120 × 720) |

Knobs: drag to turn, hold Shift for fine adjustment, double-click to reset, double-click the value to type one. The window can also be resized from its corner (75–200 %, fixed proportions); its size is saved with the session, as are the drawing (pictures included) and every setting.

Automation: Level, Length, Trigger, Tempo and BPM can be automated (in the automation list Trigger reads Transport, MIDI note, Free loop). Density and Contrast cannot: the sound is rebuilt from them about 25 times a second by the clock, not in step with the song, so in a bounce faster than real time their changes would land in the wrong place.

## How it works

- **The drawing.** A paint layer plus any number of text and picture objects. Objects are placed by time (a fraction of the length) and by frequency in hertz, so "in the highs" keeps meaning the highs if you change the analyser scale; the paint layer remembers the scale it was painted on, so brush strokes keep their frequencies too. Everything is flattened into a 1024 × 1024 picture of loudness values (the maximum of all layers): the canvas.
- **Sine lines.** The canvas becomes a bank of sine waves, one per line, spaced evenly in canvas height (so evenly on your analyser's scale). A sine at a fixed frequency shows in a spectrogram as a horizontal line at that height, and its loudness over time draws the picture along it. Each line takes the brightest part of the canvas rows nearest to it (halfway to the lines either side), so thin strokes never fall between lines. Brightness becomes level through Contrast; the analyser's Slope is compensated (around 1 kHz); and the whole drawing is scaled so its loudest moment sits at −12 dBFS RMS before Level.
- **Playback.** One oscillator per line, its level following the drawing as playback moves across it. Starts, stops, jumps, retriggers and the host's bypass fade in a few milliseconds, and a change to the drawing crossfades in 20 ms, so nothing clicks. Lines too close to the top of the sample rate's range are skipped, so nothing aliases. A safety clipper (transparent below −1 dBFS) follows Level.
- **Timing.** The position in the drawing comes from the DAW's transport, from MIDI notes, or from a free-running loop, at the host's tempo or yours.
- **Real time.** The canvas and the sine plan are rebuilt on the message thread within a few tens of milliseconds of each edit and handed to the audio thread without locks; the audio thread never allocates or waits.

**Why sound and not MIDI?** A spectrogram shows the frequencies present at each moment. MIDI notes can only make the few pitches of a synth patch, on a semitone grid, with harmonics drawn on top; pictures need hundreds of exactly placed frequencies. So FreqArt *is* the instrument: MIDI is used as a trigger, and the drawing can also be bounced to a WAV file.

## Install

Downloads are on the repository's **Releases** page, under **Assets**. Every successful build replaces the release for the current version.

### macOS: installer

Download `FreqArt-<version>-macOS.pkg` and open it. It installs `FreqArt.vst3` into `/Library/Audio/Plug-Ins/VST3/`. Then rescan plug-ins in your DAW. It runs on Apple Silicon and Intel Macs with macOS 10.15 or later.

The installer is not notarised by Apple, so macOS will not open it with a double-click the first time:

- **macOS 15 and later:** double-click it and close the warning, then go to **System Settings > Privacy & Security**, scroll down to the message about the FreqArt installer and click **Open Anyway**, then confirm.
- **Earlier versions:** right-click (or Control-click) it, choose **Open**, then **Open** again.

At the end the installer offers to move itself to the Trash.

### Manual install

Download `FreqArt-VST3-macOS.zip`, unzip it, and copy `FreqArt.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (just for you) or `/Library/Audio/Plug-Ins/VST3/` (every user). Then rescan plug-ins in your DAW.

A manually copied plug-in keeps the download quarantine flag. If the DAW refuses to load it, clear the flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/FreqArt.vst3
```

Windows and Linux builds are paused until the macOS version is finished.

## Build from source

Requirements: CMake 3.22 or newer and a C++17 compiler (Xcode on macOS). JUCE 8.0.15 is downloaded automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The plug-in is written to `build/FreqArt_artefacts/Release/VST3/FreqArt.vst3`. For a universal binary (Apple Silicon and Intel), as released, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring.

| CMake option | Default | Purpose |
| --- | --- | --- |
| `FREQART_BUILD_TESTS` | ON | Build `FreqArtTests`. |
| `FREQART_BUILD_TOOLS` | ON | Build the `FreqArtSnapshot` interface snapshot tool. |
| `FREQART_COPY_PLUGIN_AFTER_BUILD` | OFF | Copy the plug-in into your VST3 folder after each build. |
| `FREQART_ENABLE_LTO` | ON | Link-time optimisation in Release builds (turn it off for faster development builds). |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | *(download)* | Use a local JUCE checkout instead of downloading it. |

To build the macOS installer from a build folder (macOS only):

```sh
installer/macos/build-pkg.sh build out        # writes out/FreqArt-<version>-macOS.pkg
```

The version comes from `project(FreqArt VERSION ...)` in `CMakeLists.txt` unless given as a third argument. JUCE signs the plug-in before it writes `moduleinfo.json` into it, which breaks the signature's seal; the script signs its copy again (ad hoc) when needed.

On Linux, install the JUCE dependencies first:

```sh
sudo apt-get install libasound2-dev libx11-dev libxext-dev libxrandr-dev \
    libxinerama-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev
```

### Tests

```sh
ctest --test-dir build -C Release --output-on-failure
```

or run `build/FreqArtTests_artefacts/Release/FreqArtTests [filter]` to run only the tests whose names contain `filter`. `FreqArtTests` needs no host. It checks the frequency-scale maths; the document's save and load; the image modes (auto-invert on a white background, transparency, outline); text rendering; compositing and placement; the plan (frequencies, spacing, normalisation, contrast, slope); the plan exchange between threads; the engine (frequency of a single line, silence, click-free crossfades and jumps, the aliasing guard, real-time speed); the playhead (transport positions, tempo changes and ramps, bar lines, MIDI one-shots and how they end, free loop, preview, jumps); end to end, a canvas rendered to audio and analysed back must match the canvas; the processor (state round trip, MIDI and panic, bypass fades, the time signature, level and clipper bounds); the WAV export (and its file names); and the editor, driven with synthetic mouse, wheel and key events (bounce files and the export following the drawing, undo steps, selecting and placing objects, the live view's span, pictures that cannot be read). The editor tests need a display: on Linux run it under `xvfb-run -a`.

### Snapshot tool

`FreqArtSnapshot <folder> [pixel-scale]` renders the editor in its main states (canvas and live view, selected objects, the brush, the settings and help panels, other window sizes) to PNG files, without a host. On macOS it runs as it is; on Linux run it under a virtual display (`xvfb-run -a FreqArtSnapshot out`).

### Continuous integration

Every push builds the universal macOS plug-in on GitHub Actions, runs the tests natively and again on the Intel half under Rosetta, renders the interface snapshots (artifact **FreqArt-Snapshots**), validates the plug-in with pluginval at strictness 10, builds the installer and test-installs it, then publishes everything as the release `v<version>`.

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | The drawing: document, text, pictures, compositing, the frequency scale, fonts, and the plan that turns the canvas into sine lines. No audio-thread code, no UI. |
| `src/dsp/` | Everything the audio thread runs (the engine, the playhead tracker, the plan exchange), plus the WAV export and the live analyser. |
| `src/ui/` | The editor: header, toolbar, canvas, inspector, live view, footer, settings and help. |
| `src/Parameters.*` | Parameter ids, ranges and defaults, shared by the processor, the editor and the tests. |
| `src/PluginProcessor.*` | The host-facing plug-in: parameters, state, undo, plan building, MIDI and transport. |
| `tests/` | `FreqArtTests`. |
| `tools/` | The interface snapshot tool. |
| `assets/fonts/` | The embedded fonts and their licences. |
| `installer/macos/` | The macOS installer: `build-pkg.sh`, its distribution file, pages and postinstall script. |
| `docs/DESIGN.md` | The design. |

## Licences

- **FreqArt** is © 2026 AnyMeans and released under the GNU AGPLv3; see `LICENSE`.
- **JUCE 8** is dual-licensed: AGPLv3, or a commercial licence from JUCE. Distributing FreqArt without publishing its source under the AGPLv3 requires a JUCE licence.
- The **VST3 SDK** bundled with JUCE is used under its own licence. VST is a trademark of Steinberg Media Technologies GmbH.
- The embedded fonts (Inter, Space Grotesk, Jost, Fraunces, DM Mono) are used under the SIL Open Font License 1.1; see `assets/fonts/OFL-*.txt`.
