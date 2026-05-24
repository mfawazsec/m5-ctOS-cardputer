# NUIT Near-Ultrasound Voice Injection

Synthesizes SSB-AM (single-sideband amplitude modulation) near-ultrasonic audio with carrier at ~18.5kHz to inject voice commands inaudibly into nearby smart speakers or phone voice assistants.

**Operation:** Select a command from the on-device menu. The module generates a near-ultrasonic burst encoding the command via SSB-AM upper sideband and emits it through the NS4168 I2S amplifier.

**Production use:** Load base-band command audio (pre-recorded PCM) from SD card at `/sdcard/commands/<name>.raw`, modulate at runtime. Phase 2: on-device TTS via phoneme model.

**Research basis:** Xia et al., USENIX Security 2023 — NUIT attack on voice assistants.
