# Passive Acoustic Keystroke Logger

Captures individual keystroke audio segments from the SPM1423 MEMS mic at 16kHz/16-bit PCM. Energy-threshold detection isolates keystroke transients (512-sample / 32ms windows), saving each as a numbered `.wav` file to `/sdcard/keystrokes/`.

**Offline inference:** feed saved WAV files to the CoAtNet-based keystroke classifier (Python, runs on MacBook) to classify which keys were pressed.

**Display:** live keystroke event counter and recording status.

**Research basis:** Harrison, Toreini & Mehrnezhad, arXiv 2023 — acoustic side-channel on keyboards.
