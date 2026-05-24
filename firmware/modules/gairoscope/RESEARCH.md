# GAIROSCOPE Research Citations

## Primary Paper
- Guri, M., Ben-Gurion University — "GAIROSCOPE: Injecting Data from Air-Gapped Computers to Nearby Gyroscopes", IEEE Transactions on Information Forensics and Security, 2022
  DOI: 10.1109/TIFS.2022.3183571

## Signal Theory
- MEMS gyroscope resonance frequency: typically 20–30kHz (mechanical)
- Ultrasonic beat frequency: two tones at fc±Δf → gyro oscillates at beat frequency Δf
- Data encoding: FSK at beat frequency; demodulated by gyro output sampling at phone app
- Data rate: ~8 bits/sec (per Guri 2022 experimental results)

## Receiver Implementation
- W3C DeviceMotionEvent API: `window.addEventListener('devicemotion', ...)`
- Gyroscope sampling rate: typically 100Hz on iOS, variable on Android
- JavaScript FFT on gyro time series to detect FSK transitions

## Hardware References
- NS4168 I2S amplifier + 8-ohm speaker
- ESP-IDF I2S TX (I2S_NUM_1), 44.1kHz sample rate
- GPIO: BCK=34, WS=33, DOUT=35

## Limitations
- Phone must be within ~0.5–2m for sufficient acoustic coupling
- iOS 13+: requires user permission for DeviceMotionEvent
- Android: permissions vary by browser/version
