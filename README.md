This is a research project for hands-on learning of Software Defined Radio (SDR), particullary with HackRF One,
in C language.

## Goals

- Setup parameters and connect to a device by using a [HackRF One C library](https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.h) [done]
- Receive I/O samples from the device in non-blocking mode [done]
- Apply low pass filter [done]
- Decimate and resample [done]
- Demodulate FM signal [done]
- Output as audio stream [done]

## Study materials

### General resources

- [Software Defined Radio with HackRF](https://greatscottgadgets.com/sdr/)
    - Video series: [SDR with HackRF Training](https://www.youtube.com/watch?v=BeeSN14JUYU&list=PLu0BPYzTjiHru1KmPThmbY-8rRm3EWvUQ)
- [PySDR: A Guide to SDR and DSP using Python](https://pysdr.org/)

### Technical explanations, RF concepts, and architectural decisions used in this project

1. **Hardware & RF**
   * [LNA and VGA Gains, RF Amplification, and Gain Staging](./docs/lna-vga-gains.md)

2. **Concurrency & DSP**
   * [Circular Ring Buffer: Architecture, Diagrams & Implementation](./docs/circular-ring-buffer.md)
   * [Frequency Translation and Avoiding the DC Spike (phase_step)](./docs/frequency-transation.md)
   * [FM Background Noise and De-Emphasis Filtering (deemphasis_alpha)](./docs/fm-deemphasis-noise.md)
   * [Downsampling and Decimation in SDR (Channel Filtering)](./docs/downsampling-decimation.md)
   * [The Hamming Window in FFT and FIR Filter Design](./docs/hamming-window.md)
   * [FM Demodulation with atan2 and the Polar Discriminator](./docs/fm-demodulation-atan2.md)
   * [Fractional Resampling (250 kHz to 48 kHz Audio)](./docs/fractional-resampling.md)


## HackRF One

- Product page: https://greatscottgadgets.com/hackrf/one/
- Documentation: https://hackrf.readthedocs.io/en/latest/
- HackRF repository: https://github.com/greatscottgadgets/hackrf/

### Product description

HackRF One from Great Scott Gadgets is a Software Defined Radio peripheral capable of transmission or reception of radio signals from 1 MHz to 6 GHz. Designed to enable test and development of modern and next generation radio technologies, HackRF One is an open source hardware platform that can be used as a USB peripheral or programmed for stand-alone operation.

```
- 1 MHz to 6 GHz operating frequency
- half-duplex transceiver
- up to 20 million samples per second
- 8-bit quadrature samples (8-bit I and 8-bit Q)
- compatible with GNU Radio, SDR#, and more
- software-configurable RX and TX gain and baseband filter
- software-controlled antenna port power (50 mA at 3.3 V)
- SMA female antenna connector
- SMA female clock input and output for synchronization
- convenient buttons for programming
- internal pin headers for expansion
- Hi-Speed USB 2.0
- USB-powered
- open source hardware
```

### Local device

The library and CLI utilities are installed locally. When hardware is connected via USB, it is shown as:

```sh
$ hackrf_info
hackrf_info version: 2026.01.3
libhackrf version: 2026.01.3 (0.9.2)
Found HackRF
Index: 0
Serial number: 0000000000000000c66c63dc31075983
Board ID Number: 4 (HackRF One)
Firmware Version: 2024.02.1 (API:1.08)
Part ID Number: 0xa000cb3c 0xbc61473f
Hardware Revision: r9
Hardware does not appear to have been manufactured by Great Scott Gadgets.
Hardware supported by installed firmware:
    HackRF One
```

## 3. Build & Execution

### Building

```sh
$ make
```

### Running with a pipe to sound system

Option 1, with [aplay](https://linux.die.net/man/1/aplay):
```bash
./bin/fm-analyzer -f 101500000 | aplay -r 48000 -f S16_LE -c 1 -t raw
```


Option 2,  with [ffplay](https://ffmpeg.org/ffplay.html) (part of FFmpeg): 
```sh
./bin/fm-analyzer -f 101500000 | ffplay -f s16le -ar 48000 -
```
