polar9-monitor Specification
============================

Overview
--------
This repository builds a small C++20 command-line program named ``polarm`` that
connects to a Polar H9 or Polar H10 Bluetooth LE heart-rate strap using BlueZ
over the system D-Bus (``libsystemd``/``sd-bus``). It discovers the device by
advertised name, connects, subscribes to the standard Heart Rate Measurement
characteristic, parses the heart-rate payload, and streams readings to stdout.
The app includes a maintenance loop that periodically re-checks connection and
notifications and attempts to reacquire the device if it disappears.

Primary Responsibilities
------------------------
- Discover a Polar H9/H10 device by name via BlueZ ``GetManagedObjects``.
- Optionally start discovery if the device is not yet known.
- Connect to the device and locate the Heart Rate Measurement characteristic
  (UUID ``00002a37-0000-1000-8000-00805f9b34fb``).
- Start notifications and listen for ``PropertiesChanged`` signals.
- Parse Heart Rate Measurement payloads and output values to stdout.
- Suppress duplicate consecutive output lines.
- Maintain the connection and re-enable notifications if they drop.

Supported Devices
-----------------
- Polar H9: ``Polar H9 EA190E24``
- Polar H10: ``Polar H10 8A8F192B``

Program Interface
-----------------
Binary name: ``polarm``

Flags:
- ``-h`` / ``--help``: print usage and exit
- ``-d`` / ``--debug``: enable verbose debug logging to stderr
- ``-hw`` / ``--health-warning`` / ``--health-warnings``: emit health screening warnings to stderr

Output Format
-------------
The program emits one line per received notification to stdout:

``<epoch_ms>,<bpm>[,<rr_ms>...]``

Where:
- ``epoch_ms`` is the current wall-clock timestamp in milliseconds.
- ``bpm`` is the parsed heart-rate value (8- or 16-bit).
- ``rr_ms`` values are RR intervals converted from 1/1024 s to milliseconds.

Health Warnings
---------------
When ``--health-warnings`` (or an alias) is enabled, the program emits warnings to stderr and
rings the terminal bell (``\a``) on detection of:
- Bradycardia: BPM below 60.
- Tachycardia: BPM above 100.
- Arrhythmia screening based on RR-only signals:
  - Pause/dropout candidates when RR < 250 ms or RR > 2500 ms.
  - Ectopic-like short-long patterns using RR ratio heuristics.
  - Possible AF screening (RR-only) using Dash-style rules over 128-beat
    cleaned RR segments (RMSSD/meanRR, TPR, Shannon entropy).
AF detection here is a screening signal only; clinical AF diagnosis requires
ECG evidence (irregularly irregular RR plus absent P-waves) over >= 30 s.

Operational Flow
----------------
1) Open the system D-Bus connection (``sd_bus_open_system``).
2) Search managed BlueZ objects for a matching device name.
3) If not found, start discovery and scan up to ~90 seconds.
4) Connect to the device if not already connected.
5) Find the Heart Rate Measurement characteristic by UUID.
6) Call ``StartNotify`` and add a D-Bus match for ``PropertiesChanged``.
7) Event loop:
   - Process D-Bus events.
   - On idle, run maintenance to reconnect, reacquire, and re-enable notify.

Parsing Rules
-------------
Heart Rate Measurement parsing follows Bluetooth SIG flags:
- Bit 0: 8-bit vs 16-bit heart-rate value
- Bit 3: energy expended present (skipped if present)
- Bit 4: RR-intervals present (consume all remaining 16-bit values)

RR intervals are converted as:
``rr_ms = (rr_1024 * 1000 + 512) / 1024``

Key Components
--------------
- ``main.cpp``: CLI, scan/connect flow, event loop, D-Bus match setup.
- ``bluetooth.cpp`` / ``bluetooth.hpp``: BlueZ D-Bus helpers, parsing, callbacks.
- ``device_polar_h9.cpp`` / ``device_polar_h10.cpp``: device name constants.
- ``meson.build``: build configuration (C++20, clang++, libsystemd).

Dependencies
------------
- BlueZ daemon and libraries.
- ``libsystemd`` for ``sd-bus`` (non-Android builds).
- ``meson`` and ``ninja`` for building; C++20-capable compiler (clang++).

Build and Run (Arch example)
----------------------------
.. code-block:: bash

   meson setup --reconfigure build
   meson compile -C build
   ./build/polarm

Assumptions and Limitations
---------------------------
- Uses the default adapter path ``/org/bluez/hci0``.
- Device matching is by exact advertised name.
- Requires a running ``bluetoothd`` and a user environment with Bluetooth access.
- Outputs to stdout only; no file logging is performed by this program.
