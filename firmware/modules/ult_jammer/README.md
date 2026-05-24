# Ultrasonic Microphone Jammer

Emits randomized band-limited noise in the 18–22kHz range via the NS4168 I2S amplifier to jam MEMS microphones and prevent voice assistant eavesdropping.

**Modes:** Low / Medium / High intensity (amplitude levels).

**Technique:** Generates 8 random-frequency sinusoids summed per 100ms window. Fresh random seed each window prevents adaptive filtering by countermeasures.

**Display:** Jammer active indicator, intensity level.

**Research basis:** SUAD, IEEE TMC 2025 — ultrasound injection defense research.
