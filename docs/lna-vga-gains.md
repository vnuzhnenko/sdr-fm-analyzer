# FAQ: What are LNA and VGA (Baseband) Gains?

In Software Defined Radio (and specifically on the HackRF One), **LNA** and **VGA** are two distinct amplifier stages placed at different points along the receive (RX) chain:

```text
Antenna 
   │
   ▼
[RF Amp] ────► [LNA (RF/IF)] ────► [Mixer / Downconverter] ────► [Low-Pass Filter] ────► [VGA (Baseband)] ────► [ADC (8-bit)]
(0 or 14 dB)    (0 to 40 dB)       (shifts RF down to baseband)   (removes out-of-band)   (0 to 62 dB)           (outputs I/Q)
```

---

## 1. LNA (Low-Noise Amplifier) Gain
* **Where it sits:** At the RF / Intermediate Frequency (IF) front-end, directly after the antenna switch and before mixing.
* **What it does:** Amplifies very weak signals picked up by the antenna. It is engineered with a very low *noise figure*, meaning it amplifies the signal while introducing minimal thermal/electrical noise of its own.
* **HackRF Range:** **0 to 40 dB**, configured in **8 dB steps** (`0, 8, 16, 24, 32, 40`).
* **When to increase:** When receiving distant, low-power transmitters.
* **Risk of setting too high:** If a strong signal is present, high LNA gain will saturate the mixer, causing non-linear distortion (intermodulation). This creates phantom/ghost signals and elevates the noise floor across the entire spectrum.

---

## 2. VGA (Variable Gain Amplifier / Baseband Gain)
* **Where it sits:** At the *baseband* stage, located **after** the mixer has downconverted the RF signal to baseband I/Q and after the analog anti-aliasing low-pass filter, immediately before the ADC.
* **What it does:** Amplifies the filtered baseband analog voltage to span the full dynamic input range of the 8-bit Analog-to-Digital Converter (ADC).
* **HackRF Range:** **0 to 62 dB**, configured in **2 dB steps** (`0, 2, 4, ..., 62`).
* **When to increase:** When the digitized I/Q samples are under-utilizing the ADC resolution (e.g. values are only fluctuating between -5 and +5 instead of utilizing the full -128 to +127 range).
* **Risk of setting too high:** Saturated baseband signals will clip against the ADC limits (-128 and +127), converting smooth sinusoids into flat-topped square waves and creating severe harmonic audio distortion.

---

## 3. What is the HackRF "RF Amp"?
HackRF One includes an additional front-end amplifier switch directly behind the antenna connector:
* **Range:** Binary toggle: **0 dB (off)** or **+14 dB (on)**.
* **Usage:** For commercial broadcast FM (which is very strong), keep this **OFF**. It is generally reserved for faint, high-frequency signals like satellite downlinks (e.g. GPS, NOAA, or ADS-B).

---

## 4. Recommended Gain Settings for Broadcast FM
Because commercial FM radio stations transmit at high power:
1. **RF Amp:** `OFF` (`0`)
2. **LNA Gain:** `16 dB` to `24 dB`
3. **VGA Gain:** `16 dB` to `20 dB`
4. If the received audio sounds distorted or scratchy, lower VGA first, then reduce LNA.
