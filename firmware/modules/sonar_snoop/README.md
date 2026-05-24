# SonarSnoop — Active Acoustic Side-Channel

Emits ~20kHz sonar ping from speaker, captures echo via MEMS microphone, computes echo delta over time. Gesture and proximity inference from echo patterns.

**Cycle:** 5ms ping → 20ms listen, ~40Hz repetition rate.

**Output:** Raw echo stream logged to `/sdcard/sonar_echo.bin` for offline ML gesture classification. Live ASCII amplitude trace on screen.

**Phase 2:** Train classifier on phone unlock gesture echo patterns.

**Research basis:** Ba et al., Lancaster University, 2019 — SonarSnoop active acoustic side-channel attack.
