# GAIROSCOPE — Speaker-to-Gyroscope Covert Channel

Encodes data as FSK (frequency-shift keying) modulated ultrasonic audio and emits via the NS4168 I2S amplifier. Nearby phone MEMS gyroscopes mechanically resonate at the ultrasonic beat frequency, encoding the signal in gyroscope output samples.

**FSK parameters:** Bit 0 → 19800 Hz, Bit 1 → 20200 Hz, 125ms per bit (~8 bits/sec).

**Receiver:** The ctOS web UI serves `/gairoscope` — an HTML page that reads `window.DeviceMotionEvent` gyroscope data and decodes the FSK signal in JavaScript. No app install required.

**Frequency sweep mode:** Sweeps 18–22kHz to locate target phone's gyroscope resonance frequency empirically.

**Research basis:** Guri, Ben-Gurion University, IEEE 2022 — GAIROSCOPE covert channel.
