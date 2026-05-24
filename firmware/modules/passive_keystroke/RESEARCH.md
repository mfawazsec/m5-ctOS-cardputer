# Passive Keystroke Logger Research Citations

## Primary Papers
- Harrison, O., Toreini, E. & Mehrnezhad, M. — "A Practical Deep Learning-Based Acoustic Side Channel Attack on Keyboards", arXiv:2308.01074, 2023
- LLM-assisted spectrogram correction for acoustic keystroke inference, arXiv 2025 (TBD citation on publication)

## Model Architecture
- CoAtNet (Convolution + Attention Network) for keystroke audio classification
- Input: mel-spectrogram of 32ms keystroke window at 16kHz
- 36 English keyboard keys × top-1 accuracy >95% (paper results on MacBook Pro)

## Hardware References
- SPM1423 MEMS microphone datasheet (M5Stack Cardputer)
- ESP-IDF I2S PDM RX driver (`i2s_channel_init_pdm_rx_mode`)
- GPIO: CLK=41, DIN=40 (Cardputer schematic)

## Threat Model
Passive audio side-channel: attacker within microphone range (~1m) of typing target
captures keystrokes via ambient audio, infers typed text offline with ML classifier.

## Authorization
Authorized penetration testing and security research only. Never use against third parties
without explicit written authorization.
