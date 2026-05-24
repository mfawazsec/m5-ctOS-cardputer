# IR Dazzle Research Citations

## Primary Research
- Nassi, B. et al., Ben-Gurion University — "aIR-Jumper: Covert Air-Gap Exfiltration/Infiltration via Security Cameras and Infrared"
  Explores IR-based covert channels using security cameras as transceivers.

## Hardware References
- Cardputer IR LED: GPIO 44, driven via ESP32-S3 RMT peripheral
- Maximum peak current: do not exceed 100mA (check LED datasheet)
- ESP-IDF RMT TX driver (`rmt_tx.h`)
- RMT resolution: 1MHz (1µs per tick)

## Effective Frequencies
- 38kHz: standard IR remote control carrier (IR cameras typically most sensitive here)
- 20–56kHz sweep: covers range of IR-blocking filter cutoffs in different camera models

## Limitations
- Cardputer speaker is small; effective dazzle range ~0.5–3m
- Outdoor ambient IR (sunlight) may overwhelm at longer range
- Does not defeat camera IR-cut filters that block near-IR entirely

## Note on aIR-Jumper TX Mode
Optional data-encoding mode using modulated IR pulses (aIR-Jumper transmitter).
For research and demonstration only; not a primary function of this module.
