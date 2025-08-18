polarh9 — Minimal Polar H9 recorder (BlueZ / sd-bus)
================================================

Overview
--------
A tiny C++20 *modules* application that automatically finds a Bluetooth LE
device named ``Polar H9 EA190E24``, connects to it via BlueZ D-Bus, enables
notifications on the standard Heart Rate Measurement characteristic
(UUID ``00002a37-0000-1000-8000-00805f9b34fb``), and writes each received
packet as a line to ``$HOME/.cache/polarh9``.

Each line is:

``<epoch_ms>,<bpm>,<hex-bytes>``

Dependencies (Arch Linux)
-------------------------
- bluez (daemon + libraries)
- systemd-libs (for ``libsystemd`` / sd-bus)
- a recent Clang or GCC with basic C++20 modules support
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
- C++20 modules are used by providing a named module interface:
    * ``polarh9.cppm`` — the module interface (exports ``int run()``).
    * ``app.cpp`` — a tiny entry TU that *links* to the exported ``run()``
      symbol (declared ``extern int run();``) to avoid GCC BMI ordering
      issues. The module still builds as a named module unit.
- The program assumes a default adapter path ``/org/bluez/hci0`` and uses
  modern BlueZ D-Bus APIs (no deprecated ``gatttool``).
- It actively scans up to ~90s if the device isn't already known to BlueZ.
- Output file is created at ``$HOME/.cache/polarh9`` (directories auto-created).
- Extra debug prints have been added to help diagnose discovery, connection,
  characteristic lookup, and notification flow.

Android
-------
This project **builds** for Android using a cross file, but at runtime it
prints a clear message and exits because Android does not expose BlueZ
D-Bus. To capture Polar H9 data on Android you typically use the Java/Kotlin
Bluetooth stack (e.g. via an Android app) and call into native code via JNI.
Integrating the Android Bluetooth stack is beyond the scope of this minimal
example.

Cross-compile example (Android arm64)
-------------------------------------
Set ``ANDROID_NDK_HOME`` to your NDK path and then:

.. code-block:: bash

   meson setup build-android \
     --cross-file android-arm64.cross

   meson compile -C build-android

Build System Notes
------------------
- GCC can trip over modules with depfiles enabled. The Meson project turns
  off depfiles (``b_depfiles=false``) to keep module builds stable on GCC.

Troubleshooting
---------------
- Make sure ``bluetoothd`` is running: ``sudo systemctl status bluetooth``
- Ensure your user can access Bluetooth (often just being in the ``lp``
  group on Arch or running from a desktop session is sufficient).
- If the device is paired/remembered under a different alias, update
  ``kTargetName`` in the source or pair/forget as needed with
  ``bluetoothctl``.
