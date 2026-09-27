# FAQ: Frequency Translation and Phase Step in SDR

In digital signal processing, shifting a frequency spectrum digitally requires calculating a constant **phase increment** ($\Delta\phi$) per sample:

```c
float phase_step = 2.0f * (float)M_PI * freq_offset_hz / sample_rate;
```

This line calculates the **digital phase increment (in radians)** needed to shift the radio signal in software using a **Digital Downconverter (Software Mixer)**.

---

## 1. Why Do We Need Frequency Translation in SDR?

### The Problem: The HackRF "DC Spike" (Zero-IF Artifact)
HackRF One uses a **Direct-Conversion (Zero-IF / Homodyne)** receiver architecture. In direct conversion, the tuner's hardware local oscillator (LO) is set directly to the target radio frequency to downconvert it to 0 Hz (Baseband $I/Q$).

However, direct-conversion hardware suffers from two physical flaws:
1. **Local Oscillator Leakage:** Some of the tuner's own oscillator energy leaks into the mixer and RF input, creating a constant DC voltage offset right at **$0\text{ Hz}$** (the center frequency).
2. **1/f Flicker Noise:** Semiconductor transistors produce significant low-frequency thermal noise concentrated near $0\text{ Hz}$.

If you tune the HackRF directly to your target FM station (e.g. $101.1\text{ MHz}$), this unwanted **DC spike** lands dead-center in the middle of your broadcast, causing severe buzzing, carrier noise, and distorted demodulation.

```text
Direct Tuning (Problematic):
Power
  ▲
  │            [DC SPIKE]
  │                ▲
  │             ┌──┴──┐
  │         ┌───┤     ├───┐  <-- Target FM Signal corrupted by DC spike!
──┴─────────┴───┴─────┴───┴────────► Frequency
                0 Hz (Center)
```

---

### The Solution: Off-Tuning + Software Frequency Translation
Instead of tuning the hardware to the exact station:
1. **Off-Tune Hardware:** We tune the HackRF slightly off to the side (e.g., $+250\text{ kHz}$ away: $101.35\text{ MHz}$).
2. The hardware DC spike is now at $101.35\text{ MHz}$, while our desired station sits cleanly at **$-250\text{ kHz}$**, completely untouched by hardware DC noise.
3. **Digital Frequency Translation:** In software DSP, we mathematically shift the spectrum by $+250\text{ kHz}$ to center our station back to $0\text{ Hz}$ before filtering and demodulating.

```text
Off-Tuned Hardware + Digital Translation (Clean):
Power
  ▲
  │                       [DC SPIKE]
  │                           ▲
  │       Target Station   ┌──┴──┐
  │          ┌─────┐       │     │
──┴──────────┴─────┴───────┴─────┴──► Frequency
           -250 kHz         0 Hz (Hardware LO)
              │
              └────── Shift +250 kHz in software ────► Centers at 0 Hz cleanly!
```

---

## 2. Mathematical Derivation of `phase_step`

To shift a signal in frequency by $f_{\text{offset}}$, we apply Euler's identity by multiplying the complex input signal $s[n] = I[n] + jQ[n]$ with a complex sinusoid (local oscillator):

$$s_{\text{shifted}}[n] = s[n] \cdot e^{-j \phi[n]}$$

Where $\phi[n]$ is the instantaneous phase angle at sample index $n$.

### Step 1: Angular Frequency
In continuous time, angular velocity $\omega$ (in radians per second) is:
$$\omega = 2\pi \cdot f_{\text{offset}}$$

### Step 2: Discrete Time Interval
In digital signal processing, samples arrive at discrete time steps $T_s = \frac{1}{f_s}$, where $f_s$ is `input_sample_rate`:
$$t = n \cdot T_s = \frac{n}{f_s}$$

### Step 3: Phase per Sample (`phase_step`)
The total phase at sample $n$ is:
$$\phi[n] = \omega \cdot t = (2\pi \cdot f_{\text{offset}}) \cdot \left(\frac{n}{f_s}\right) = n \cdot \left(\frac{2\pi \cdot f_{\text{offset}}}{f_s}\right)$$

The change in phase between one sample and the next ($\Delta\phi = \phi[n] - \phi[n-1]$) is constant:

$$\Delta\phi = \frac{2\pi \cdot f_{\text{offset}}}{f_s}$$

In C code:
```c
float phase_step = 2.0f * (float)M_PI * (float)freq_offset_hz / (float)sample_rate;
```

---

## 3. How It Is Used in the DSP Pipeline

In a sample processing loop, each incoming complex sample $(I, Q)$ is multiplied by the rotating complex vector:

```c
// Precalculated phase increment per sample:
// float phase_step = 2.0f * M_PI * freq_offset / sample_rate;
// Phase accumulator state: float phase = 0.0f;

for (size_t n = 0; n < num_samples; n++) {
    float cos_val = cosf(phase);
    float sin_val = sinf(phase);

    // Complex multiplication: (I + jQ) * (cos(phase) - j*sin(phase))
    float shifted_i = in_i[n] * cos_val + in_q[n] * sin_val;
    float shifted_q = in_q[n] * cos_val - in_i[n] * sin_val;

    // Advance phase for the next sample
    phase += phase_step;

    // Wrap phase to keep it within [-PI, PI] to prevent floating point precision loss
    if (phase > (float)M_PI) {
        phase -= 2.0f * (float)M_PI;
    } else if (phase < -(float)M_PI) {
        phase += 2.0f * (float)M_PI;
    }
}
```

### Summary of Benefits
1. **Pristine Audio Quality:** Completely evades the hardware DC offset and $1/f$ flicker noise.
2. **Zero Moving Parts:** Frequency translation is performed purely in software using basic trigonometric multiplication.
3. **Constant Time $O(1)$:** `phase_step` is precalculated once at initialization, requiring only one addition per sample during streaming.
