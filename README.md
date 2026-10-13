# Surround Reverb

A multichannel reverb plugin for surround and immersive mixes, built with [JUCE](https://juce.com) on the [Pamplejuce](https://github.com/sudara/pamplejuce) template.

- Works on any speaker layout of up to 16 channels: stereo, LCR, quad, 5.1, 7.1, 7.1.2, 7.1.4, 9.1.6...
- Mono or stereo sources can feed a wider output, for example a stereo track into a 5.1 reverb
- Every speaker gets its own decorrelated tail from a single shared reverb, so the room wraps around the listener
- Separate decay times for the lows, mids and highs
- The LFE stays out of the reverb unless you send it in or ask for reverb back
- VST3, AU, AUv3, CLAP and Standalone, on macOS, Windows and Linux

## Controls

| Section | Control | What it does |
| --- | --- | --- |
| Reverb | Size | Size of the room |
| | Decay | Time for the mids to fall by 60 dB (RT60), from 0.2 to 20 seconds |
| | Pre-Delay | Gap before the reverb starts, up to 250 ms |
| | Diffusion | How smeared the onset of the reverb is |
| | Modulation | Slow movement in the tail, which stops long decays sounding metallic. At 0% the tail has no pitch movement at all |
| Damping | Low Freq, Low Decay | Below this frequency the decay time is multiplied by Low Decay, from 0.1x to 2x. Above 1x the lows ring for longer than the mids, which is what makes a hall sound warm |
| | High Freq, High Decay | The same for the highs. Below 1x the highs die away first, as they do in a real room |
| Speaker Levels | Front, Center, Surround, Height | Reverb level for each group of speakers. All the way down switches the group off, for example to keep the center clear for dialogue |
| Rear | Delay | Holds the surround speakers' reverb back by up to 100 ms, to push the back of the room further away |
| | Tone | Makes the surround speakers' reverb darker or brighter, by up to 12 dB above 4 kHz |
| LFE | Send | How much the LFE input drives the reverb. Off by default |
| | Return | How much reverb comes back in the LFE, lowpassed at 120 Hz. Off by default |
| Tone & Mix | Low Cut, High Cut | Filters on the signal going into the reverb |
| | Mix | Dry/wet balance. Set it to 100% when using the plugin on a send |

Controls for speakers that the current layout doesn't have are greyed out.

## How it works

The reverb produces a diffuse tail only. It doesn't model early reflections, so it adds depth and envelopment without saying anything about where a source is. The Waves R360 manual [1] describes the same division of labour, and comparing against it is where the two band damping, the LFE send and return, and the rear tone control came from.

### One network for every channel

The core is a feedback delay network (FDN): 16 delay lines whose outputs are mixed by a matrix and fed back into their inputs. Stautner and Puckette [2] introduced the structure for exactly this purpose, multichannel reverberation, and Jot and Chaigne [3] turned it into the general design method used here. Smith [4] and Välimäki et al. [5] are good overviews.

- **Feedback matrix.** A 16x16 Hadamard matrix, computed with a fast Walsh-Hadamard transform. It is orthogonal, so on its own the loop neither gains nor loses energy [3][6], and since every entry is non-zero each line feeds every other line on every pass, which builds echo density quickly.
- **Delay lengths.** 29 to 97 ms at the default size, chosen so that no two are simple multiples of each other and their echoes don't pile up. Size scales all of them together.
- **Inputs.** Each input channel is pre-delayed, filtered, and smeared by three allpass filters in series (Schroeder [7]) whose lengths differ slightly per channel. It then drives all 16 lines at once, using its own row of the Hadamard matrix as the pattern of signs. Different channels therefore excite the network in orthogonal ways, even when they carry the same signal.
- **Outputs.** The lines are dealt out between the speakers, and each speaker reads only its own. Because no line is shared, the speakers' tails are decorrelated from each other, which is what produces envelopment rather than a phantom image between speakers. The tests measure this: the correlation between any two speakers of a 5.1 tail stays below 0.2.

A single shared network, rather than one reverb per channel, means a sound in any one speaker reverberates from all of them, and the cost barely grows with the channel count.

### Decay time

Following Jot and Chaigne [3], the losses are placed inside each delay line, in proportion to its length. A line of length `L` seconds gets a gain of `10^(-3L/T)`, so that every line, and therefore the whole network, decays by 60 dB in `T` seconds whatever the delay lengths are.

Frequency dependent decay uses the same idea per band. Each line also has a first order low shelf and high shelf, with gains of `10^(-3L/T * (1/r - 1))` for a band whose decay time should be `r` times the mids'. A ratio above 1 makes the shelf boost, but the ratios stop at 2, which keeps the total gain around the loop below 1 at every frequency. The tests check the result band by band, and check that the worst case (both ratios at 2, overlapping bands, 20 second decay, full modulation) only ever gets quieter.

### Modulation

Each line's length is varied by its own slow sine oscillator (0.3 to 1.2 Hz, up to 1.5 ms). Without this, the resonances of a long tail stay put and it rings; moving the delays smears them (Dattorro [8]; Schlecht and Habets [9] cover time variation in FDNs more generally). The cost is a little pitch movement in the tail, so Modulation can be turned down to nothing.

The lines are read with third order Lagrange interpolation (Laakso et al. [10]). Linear interpolation would be cheaper but acts as a lowpass inside the loop, losing up to 3 dB at a quarter of the sample rate on every pass and so shortening the high frequency decay regardless of the damping settings.

### Filters

The low cut and high cut are 12 dB/octave state variable filters in the trapezoidal form (Zavalishin [11], Simper [12]), which stay stable and accurate while their cutoffs are being moved. They sit before the network, so they shape what the reverb hears without touching the dry signal.

### LFE

The LFE channel is for band-limited effects (up to 120 Hz [13]), and a decorrelated full range tail in it is rarely wanted. By default the LFE input doesn't drive the reverb and no reverb is returned to it. The Send and Return controls open each path separately, and the return is a mix of every line, lowpassed at 120 Hz.

### Things it doesn't do

- No early reflections or source positioning
- No ambisonic layouts: the channels of an ambisonic signal aren't speaker feeds, and decorrelating them would destroy the encoding

## References

1. Waves, [R360° Surround Reverb User Guide](https://assets.wavescdn.com/pdf/plugins/r360-surround-reverb.pdf).
2. J. Stautner and M. Puckette, "Designing Multi-Channel Reverberators", *Computer Music Journal*, vol. 6, no. 1, pp. 52-65, 1982.
3. J.-M. Jot and A. Chaigne, "Digital Delay Networks for Designing Artificial Reverberators", 90th AES Convention, paper 3030, 1991. [AES E-Library](https://aes2.org/publications/elibrary-page/?id=5663)
4. J. O. Smith, [*Physical Audio Signal Processing*](https://ccrma.stanford.edu/~jos/pasp/), online book, chapter on feedback delay networks.
5. V. Välimäki, J. Parker, L. Savioja, J. O. Smith and J. S. Abel, "Fifty Years of Artificial Reverberation", *IEEE Transactions on Audio, Speech, and Language Processing*, vol. 20, no. 5, pp. 1421-1448, 2012.
6. S. J. Schlecht and E. A. P. Habets, "On Lossless Feedback Delay Networks", *IEEE Transactions on Signal Processing*, vol. 65, no. 6, 2017. [arXiv:1606.07729](https://arxiv.org/abs/1606.07729)
7. M. R. Schroeder, "Natural Sounding Artificial Reverberation", *Journal of the Audio Engineering Society*, vol. 10, no. 3, pp. 219-223, 1962.
8. J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", *Journal of the Audio Engineering Society*, vol. 45, no. 9, pp. 660-684, 1997.
9. S. J. Schlecht and E. A. P. Habets, "Time-varying feedback matrices in feedback delay networks and their application in artificial reverberation", *Journal of the Acoustical Society of America*, vol. 138, no. 3, pp. 1389-1398, 2015. [doi:10.1121/1.4928394](https://doi.org/10.1121/1.4928394)
10. T. I. Laakso, V. Välimäki, M. Karjalainen and U. K. Laine, "Splitting the Unit Delay", *IEEE Signal Processing Magazine*, vol. 13, no. 1, pp. 30-60, 1996.
11. V. Zavalishin, *The Art of VA Filter Design*, Native Instruments.
12. A. Simper, "Linear Trapezoidal Integrated State Variable Filter", [Cytomic technical papers](https://cytomic.com/technical-papers/).
13. ITU-R, [Recommendation BS.775](https://www.itu.int/rec/R-REC-BS.775), "Multichannel stereophonic sound system with and without accompanying picture".

## Building

```bash
git submodule update --init --recursive

# Xcode
cmake -B Builds-Xcode -G Xcode
open Builds-Xcode/SurroundReverb.xcodeproj

# or on the command line
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
ctest --test-dir Builds --output-on-failure
```

Building copies the plugins into your user plugin folders.
