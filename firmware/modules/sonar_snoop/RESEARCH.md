# SonarSnoop Research Citations

## Primary Papers
- Ba, Z. et al. — "SonarSnoop: Active Acoustic Side-Channel Attack", Lancaster University, 2019
  arXiv:1808.10250
- CASPER covert channel research — acoustic covert channels via speaker/mic pairs

## Signal Processing
- IIR bandpass filter (2nd-order Butterworth, 18–22kHz at 44.1kHz Fs)
- Echo correlation: baseline subtraction; delta amplitude tracks gesture presence
- Ping frequency: 20kHz (just above typical human hearing range)

## Hardware References
- SPM1423 MEMS microphone (RX)
- NS4168 I2S amplifier + 8-ohm speaker (TX)
- Half-duplex scheduling: I2S_NUM_1 (TX), I2S_NUM_0 (RX) — alternate enable/disable
- ESP-IDF dual I2S peripheral support

## Limitations
- Cardputer speaker is small; effective range limited (~30–60cm)
- Acoustic feedback between co-located speaker and mic requires careful timing
- Echo amplitudes are small; high gain + bandpass filter essential

## Authorization
For security research and CTF use only.
