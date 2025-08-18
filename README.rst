polarh9 — Minimal Polar H9 recorder (BlueZ / sd-bus)
================================================

Overview
--------
A tiny **single-file** C++20 application that automatically finds a Bluetooth
LE device named ``Polar H9 EA190E24`` **or** ``Polar H10 8A8F192B``, connects to
it via BlueZ D-Bus, enables notifications on the standard Heart Rate
Measurement characteristic (UUID ``00002a37-0000-1000-8000-00805f9b34fb``),
and writes each received packet as a line to cache files under your home.

- For H9, BPM goes to ``$HOME/.cache/polarh9`` and RR/NN intervals go to
  ``$HOME/.cache/polarh9rr``.
- For H10, BPM goes to ``$HOME/.cache/polarh10``, RR/NN intervals to
  ``$HOME/.cache/polarh10rr``, Battery Level (if available) to
  ``$HOME/.cache/polarh10_battery``, and any other notifying characteristics
  are logged raw (hex) as ``$HOME/.cache/polarh10_<uuid8>``.
  Some readable device-info fields are captured once to
  ``$HOME/.cache/polarh10_device_info``.

Each line is:

- BPM stream: ``<epoch_ms>,<bpm>``
- RR stream:  ``<epoch_ms>,<rr_ms>``  (RR converted from 1/1024 s to ms)
- Battery:    ``<epoch_ms>,<percent>``
- Generic:    ``<epoch_ms>,<hex-bytes>``

Dependencies (Arch Linux)
-------------------------
- bluez (daemon + libraries)
- systemd-libs (for ``libsystemd`` / sd-bus)
- **Clang** (recent)
- meson (>= 0.60) and ninja

Quick build (Arch)
------------------
.. code-block:: bash

   meson setup --reconfigure build
   meson compile -C build

   # Run (ensure bluetoothd is running and device is advertising)
   ./build/polarh9

Notes
-----
- Implementation is a single translation unit: ``main.cpp``.
- We intentionally compile and link with ``clang++`` (see ``meson.build``).
- The program assumes a default adapter path ``/org/bluez/hci0`` and uses
  modern BlueZ D-BUS APIs (no deprecated ``gatttool``).
- It actively scans up to ~90s if the device isn't already known to BlueZ.
- Output files are created under ``$HOME/.cache``; directories are auto-created.
- Extra debug prints are included to help diagnose discovery, connection,
  characteristic lookup, notification flow, value parsing (flags/BPM/RR),
  battery and generic streams, and entry points. Use ``-d``/``--debug``.

Android
-------
This project **builds** for Android using a cross file; the produced binary is
a stub that prints a clear message and exits because Android does not expose
BlueZ D-Bus. To capture Polar H9/H10 data on Android you typically use the
Java/Kotlin Bluetooth stack and call into native code via JNI. Integrating the
Android Bluetooth stack is beyond the scope of this minimal example.

Cross-compile example (Android arm64)
-------------------------------------
Set ``ANDROID_NDK_HOME`` to your NDK path and then:

.. code-block:: bash

   meson setup build-android \
     --cross-file android-arm64.cross

   meson compile -C build-android

Troubleshooting
---------------
- Make sure ``bluetoothd`` is running: ``sudo systemctl status bluetooth``
- Ensure your user can access Bluetooth (often just being in the ``lp``
  group on Arch or running from a desktop session is sufficient).
- If the device is paired/remembered under a different alias, update
  the target name in the source or pair/forget as needed with
  ``bluetoothctl``.
