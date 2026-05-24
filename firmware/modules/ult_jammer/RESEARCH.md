# Ultrasonic Jammer Research Citations

## Primary Paper
- SUAD — "SUAD: Solid-Channel Ultrasound Injection Attack and Defense to Voice Assistants", IEEE Transactions on Mobile Computing, 2025

## MEMS Mic Characteristics
- SPM1423 frequency response: flat 20Hz–20kHz; sensitivity falls above but still responsive
- Many MEMS mics remain sensitive at 18–22kHz despite nominal 20kHz upper spec
- Jamming exploits mic nonlinearity: ultrasonic interference demodulates to audible noise

## Hardware References
- NS4168 I2S amplifier max output: ~85dB SPL at 10cm
- Speaker: 8-ohm, max 1W
- ESP-IDF I2S TX, `esp_random()` for LFSR seed

## Defensive Use
Primary use case: personal privacy protection against covert recording. Counter-surveillance
in sensitive meeting environments.
