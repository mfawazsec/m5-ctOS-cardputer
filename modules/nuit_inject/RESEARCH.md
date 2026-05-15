# NUIT Research Citations

## Primary Paper
- Xia, Q. et al. — "Near-Ultrasound Inaudible Trojan (NUIT): Attacks on Consumer Voice Services and Proactive Defenses", USENIX Security 2023
  https://www.usenix.org/conference/usenixsecurity23/presentation/xia

## Signal Processing
- SSB-AM (single-sideband amplitude modulation): upper sideband, carrier 18–20kHz
- Voice command bandwidth: 300–3400Hz (telephony grade) shifted up to carrier
- Audio bandwidth of human hearing typically ends ~20kHz; MEMS mic sensitivity extends higher

## Hardware References
- NS4168 I2S amplifier datasheet (M5Stack Cardputer)
- Speaker: 8-ohm, ~1W max output
- ESP-IDF I2S TX driver (I2S_NUM_1)
- GPIO: BCK=34, WS=33, DOUT=35

## Threat Model
Attacker within speaker range (~1–3m) injects inaudible near-ultrasonic command into
a voice assistant microphone. The assistant demodulates the AM signal and processes it
as a legitimate voice command.

## Authorization
Authorized penetration testing, CTF, and security research only. Test only against
devices you own or have explicit authorization to test.
