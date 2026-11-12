# VstPlay

A small personal VST3 / AU plugin host for macOS, built with [JUCE](https://juce.com) 8.0.6 (GPL, fine for personal use).

## What it does

- Scans for VST3 and AudioUnit plugins and caches the list in `~/Library/VstPlay/plugins.xml`.
- Format filter dropdown: **All / VST3 / AU** (filters the displayed list only; no rescan).
- Loads the selected plugin and opens its native editor window.
- Plays through the default audio output device (stereo).
- MIDI input from all connected MIDI devices plus an on-screen keyboard.

Instruments work (tested with Microtonic, DecentSampler). Effects load but are silent, because there is no audio input yet (see below).

## Build and run

Requires Xcode command line tools, CMake and git (JUCE is fetched on first configure).

```
cmake -B build -G Xcode
cmake --build build --config Debug
open build/VstPlay_artefacts/Debug/VstPlay.app
```

Click **Scan**, pick a plugin, play the keyboard.

## Layout

- `CMakeLists.txt` – fetches JUCE, defines the app target, enables VST3/AU hosting.
- `Source/Main.cpp` – the whole app:
  - `PluginWindow` – window wrapping a plugin's editor.
  - `MainComponent` – UI, scanning, filtering, loading, audio/MIDI wiring.
  - `MainWindow`, `VstPlayApp` – standard JUCE app boilerplate.

## Design notes and lessons learned

- **Audio graph.** The plugin runs inside an `AudioProcessorGraph` (plugin node -> stereo output node, MIDI input node -> plugin), fed to the device by an `AudioProcessorPlayer`. Do not hand the plugin straight to the player: multi-output instruments such as Microtonic (4 outputs) and Microtonic Multi (16) crashed with a null `memmove` inside the plugin because the buffer had only 2 channels. The graph allocates a buffer for every channel the plugin declares.
- **Disabling aux buses did not help.** Plugins still wrote to them, so the host keeps all buses enabled and just routes the first two output channels to the output.
- **Crash triage.** Crash reports are in `~/Library/Logs/DiagnosticReports/VstPlay-*.ips`; the triggered thread's frames show whether the crash is in the host or inside a plugin.
- **Filter indexing.** The plugin dropdown is filled from, and indexed into, the filtered `types` array, so filtering is consistent. An earlier suspected indexing bug turned out to be the multi-bus crash above.

## Known limitations

- No audio input, so effect plugins produce no sound.
- Only the first stereo pair of multi-output plugins is heard.
- Scanning runs in-process on the main thread; a plugin that crashes while scanning takes the host with it.
- No saving or restoring of plugin state/presets.
- No VST2 (deprecated; SDK no longer distributed).
- Debug build only has been used.

## Where to go from here

1. **Audio file player** (recommended first): `AudioFormatManager` + `AudioTransportSource` + `AudioSourcePlayer` feeding the plugin, with play/stop/loop. Easiest way to test effects. Needs a graph input path (or mix the file into the graph's input node).
2. **Live audio input:** open the device with 2 input channels, add an `audioInputNode` to the graph and connect it to the plugin. Needs `NSMicrophoneUsageDescription` in the app plist (JUCE: `MICROPHONE_PERMISSION_ENABLED TRUE` in `juce_add_gui_app`). Use headphones to avoid feedback.
3. **Save/restore state:** `getStateInformation` / `setStateInformation` on the plugin, stored to a file; auto-restore the last plugin on launch.
4. **Out-of-process scanning:** scan in a child process (JUCE `ChildProcess` / `PluginDirectoryScanner` with a dead-man's-pedal file) so bad plugins don't crash the host.
5. **Plugin chain / mixer:** several nodes in the graph, with routing for multi-out instruments.
6. **Audio device settings UI:** `AudioDeviceSelectorComponent` for choosing device, sample rate and buffer size.
7. **Release build and bundling:** `cmake --build build --config Release`.
