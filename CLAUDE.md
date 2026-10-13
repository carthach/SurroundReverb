# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About This Project

**Surround Reverb** (by Carthach) is a multichannel reverb effect plugin. It runs on any speaker layout of up to 16 channels (stereo, 5.1, 7.1, 7.1.4, 9.1.6...) and can also take a mono or stereo input into a wider output.

This project is derived from the [Pamplejuce](https://github.com/sudara/pamplejuce) template — a JUCE audio plugin template using CMake, C++23, and modern CI/CD. It builds cross-platform (macOS, Windows, Linux) with support for multiple plugin formats (VST3, AU, AUv3, CLAP, Standalone).

The template provides the build system, CI/CD, and project structure. The plugin-specific logic lives in `source/`.

## Build Commands

Builds default to **Debug** (faster builds, better debugging). Use Release when actually making music with the plugin: the Debug reverb uses several times more CPU.

Day to day development happens in Xcode:

```bash
# Generate the Xcode project (run once, or after CMakeLists.txt changes), then open it
cmake -B Builds-Xcode -G Xcode
open Builds-Xcode/SurroundReverb.xcodeproj
```

Command line builds (this is what Claude should use) go in `Builds` with Ninja, so they never collide with the Xcode project:

```bash
# Configure (run once, or after CMakeLists.txt changes)
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build everything, or just the tests
cmake --build Builds
cmake --build Builds --target Tests

# Run tests (from project root)
ctest --test-dir Builds --verbose --output-on-failure

# Or run tests directly
./Builds/Tests

# Run a single test by name or tag
./Builds/Tests "[engine]"

# Run benchmarks
./Builds/Benchmarks

# Validate the AU after a build
auval -v aufx SRvb Cart
```

A full build copies the plugins into `~/Library/Audio/Plug-Ins` (`COPY_PLUGIN_AFTER_BUILD`). AUv3 is only built by the Xcode generator.

For a Release build: `cmake -B Builds-Release -G Ninja -DCMAKE_BUILD_TYPE=Release`

On macOS for universal binary: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`

## Project Structure

- `source/` - Plugin source code
  - `ReverbEngine` - The DSP. Depends only on `juce_audio_basics`, knows nothing about the plugin
  - `PluginProcessor` - Parameters, bus layouts, state, and mapping JUCE channel sets onto the engine
  - `PluginEditor` - The UI: knobs, look and feel, output meters
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions, melatonin_inspector
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**Reverb**: `ReverbEngine` is a 16 line feedback delay network with a Hadamard feedback matrix. Each input channel goes through pre-delay, low/high cut filters and three allpass diffusers, then drives every line using its own Hadamard row as the sign pattern. Each speaker reads its own disjoint set of lines, which is what keeps the outputs decorrelated. Per line: a modulated cubic-interpolated delay, a gain that sets the RT60, and a low and a high shelf that scale the decay time in their bands (the damping ratios). The ratios are capped at 2 because that is what keeps the loop gain below 1, so don't widen that range. `README.md` explains the approach in full, with references: keep it in step with the DSP.

**LFE**: LFE inputs only drive the reverb through the LFE send, and LFE outputs only get reverb (a 120 Hz lowpassed mix of all lines) through the LFE return. Both default to off. The dry LFE follows the mix like any other channel.

**Layouts**: `PluginProcessor::makeEngineLayout` turns the bus layout into a `ReverbEngine::Layout`: a `Role` per input and output (front, centre, surround, height, lfe), which decides which level control it answers to, and where each input's dry signal goes. `PluginProcessor::isLayoutSupported` is the single source of truth for which layouts are allowed (no ambisonics).

**Parameters**: All live in the `AudioProcessorValueTreeState` (`PluginProcessor::state`), with IDs in the `ParameterIDs` namespace. `processBlock` reads them once per block into a `ReverbEngine::Parameters`, and the engine does its own smoothing. Parameter IDs and ranges are part of saved sessions, so don't rename or rescale them casually.

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `PamplejuceFunctions.cmake` - One include that defines the `pamplejuce_*` functions below (and turns off their legacy include-time behavior)
- `PamplejuceIPP.cmake` - Intel IPP integration (optional)

Target setup happens via explicit function calls in `CMakeLists.txt`, not at include time:
- `pamplejuce_add_assets()` - Includes all files in assets/ as binary data (from `Assets.cmake`)
- `pamplejuce_add_tests()` - Configures the Catch2 test target (from `Tests.cmake`)
- `pamplejuce_add_benchmarks()` - Configures the Catch2 benchmark target (from `Benchmarks.cmake`)
- `pamplejuce_shared_code_defaults()` - C++23, fast math (from `SharedCodeDefaults.cmake`)
- `pamplejuce_xcode_prettify()` - Xcode folder/scheme cleanup (from `XcodePrettify.cmake`)

Note: after `include(PamplejuceFunctions)`, a plain `include(Tests)` (or Assets, etc.) is a no-op - call the function instead.

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (Standalone AU VST3 AUv3)
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

The macOS deployment target is 12.0, set in `CMakeLists.txt` right after `include(PamplejuceMacOS)`. The template's 10.14 can't be used: Xcode 27 won't generate a project for anything older than 12.0.

## CI

`.github/workflows/build_and_test.yml` builds, tests and runs pluginval on macOS, Windows and Linux. Currently disabled, as commented out steps that can be restored:
- **Intel IPP** - the install steps, `IPP_DIR`, and `include(PamplejuceIPP)` in `CMakeLists.txt`
- **macOS code signing and notarization** - the macOS installer is built unsigned
- **Windows code signing** (Azure Trusted Signing)

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: LSP/clangd often reports false positive diagnostic errors (like "undeclared identifier", "file not found") because it doesn't have full context of the JUCE module system. Ignore these unless the actual build fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_inspector](https://github.com/sudara/melatonin_inspector) — runtime component debugger (already included)
- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
