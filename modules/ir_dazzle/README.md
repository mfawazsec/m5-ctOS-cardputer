# IR Camera Dazzling

Drives the Cardputer's onboard IR LED (GPIO 44) at maximum duty cycle via the ESP32 RMT peripheral to saturate IR-sensitive cameras.

**Modes:**
1. Continuous — 38kHz carrier, maximum duty cycle
2. Burst — 10ms on / 5ms off pulse pattern
3. Sweep — frequency sweep 20–56kHz over ~2.25 seconds

**Keyboard:** [1] Continuous, [2] Burst, [3] Sweep. Start/stop module via module manager.

**Effective range:** 0.5–3m depending on camera IR sensitivity. Test empirically.

**Research basis:** Nassi et al. — aIR-Jumper covert IR channel research (BGU).
