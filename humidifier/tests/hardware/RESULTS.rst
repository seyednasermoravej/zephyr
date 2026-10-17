ESP32 hardware test results — 2026-10-12
=======================================

Normal runtime controls passed. Reset exposed an unresolved electrical
behavior: all three active-high selector signals read HIGH before firmware
initialized them. Do not interpret this report as verification that the
connected power circuit stays off throughout reset.

Setup and evidence
------------------

* Board: ESP32 DevKitC, ESP32 revision 3.1, USB serial ``/dev/ttyUSB0``.
* Analyzer: physical Saleae Logic ``9D283DDEC2962FCC``, Logic 2.4.29,
  24 MS/s digital sampling.
* Shared ground; channel 0 = GPIO19 PWM, channel 1 = GPIO21/Piezo 2,
  channel 2 = GPIO22/Piezo 0, channel 3 = GPIO23/Piezo 1.
* Browser/API address during testing: ``http://192.168.1.5``.
* Evidence directory:
  ``/home/naser/humidifier-hardware-results/2026-10-12``.

The evidence includes native ``.sal`` captures, compressed raw CSV exports,
JSON measurements, browser screenshots, build/flash logs, and the flashed
``zephyr.bin`` with its SHA256 in ``firmware.json``. The user's previous
manual recording was preserved as ``preserved-manual.sal``. The source
snapshot and test scripts used for this run are included in ``source/``.

Build and flash completed, and esptool verified the written data hash.
Two runtime problems were corrected in the flashed firmware: button pins
now correctly map to GPIO36/39/34 without unsupported internal pull-ups,
and an unavailable saved AP no longer causes endless station retries that
reject serial provisioning commands. These input-only button pins require
external pull-ups. Automatic Wi-Fi station retries are disabled.

Waveforms and selection
-----------------------

All 45 matrix cases passed: three piezos, three target frequencies
(1.6/1.7/1.8 MHz), and five duty settings (0/25/50/75/100%). Each case
verified that only the requested selector was HIGH.

.. list-table:: Measured oscillating outputs
   :header-rows: 1

   * - Target
     - Expected timer frequency
     - Measured range
   * - 1.600 MHz
     - 1,600,000 Hz
     - 1,599,912.50–1,599,913.12 Hz
   * - 1.700 MHz
     - 1,702,127.66 Hz
     - 1,702,034.51–1,702,035.63 Hz
   * - 1.800 MHz
     - 1,777,777.78 Hz
     - 1,777,680.54–1,777,681.24 Hz

A 1.800 MHz target therefore does not produce exactly 1.800 MHz with this
Zephyr LEDC interface. Applied values shown in the UI describe timer
configuration; they are not measured frequencies.

Measured duty ranges were 24.42–25.90% for 25%, 48.84–49.93% for 50%,
and 73.27–75.95% for 75%. Every 0% case was constant LOW; every 100%
case was constant HIGH. The analyzer samples every 41.67 ns, only about
13–15 times per PWM cycle. These observations support the tested settings,
but do not accurately resolve all 3.125% hardware duty steps. Neither
clock was independently calibrated.

The continuous switching capture verified
Off → Piezo 0 → Off → Piezo 1 → Off → Piezo 2 → Off.
No selector overlap occurred in 3,924,685 exported event rows. The two
break-before-make intervals were 442.500 and 439.875 microseconds.

Browser, API, and persistence
----------------------------

The actual embedded page was exercised in Firefox. Seven checks covered
all three selections, Off, targets 1.6/1.7/1.8 MHz, and duty 0/50/100%.
Displayed applied values matched the board's API. Screenshots cover wide
and mobile layouts and the applied/off states.

Seventeen supported HTTP checks passed: fourteen invalid commands returned
400, a 256-byte body returned 413, a valid 255-byte body returned 200,
and a valid Content-Length body split across TCP writes returned 200.
Invalid requests left the original state unchanged.

An additional check documented a transport limitation: a valid chunked
request returned 400 without changing state. The local Zephyr HTTP/1
transport counts decoded body bytes but forwards bytes using an offset
into the raw receive buffer, which includes chunk framing. This is outside
the application's command parser. The browser's Content-Length requests
worked. Chunked requests remain unsupported.

Distinct frequency/duty pairs for all three piezos survived a hardware
reset. The API reported Off after initialization. Original setpoints were
then restored: all frequencies 1.700 MHz, duty 50/50/70%, brightness
20/40/60. A final 128 ms capture verified all four signals stayed LOW.
The serial test process was closed; the board remained reachable and Off.

Wi-Fi was connected temporarily to the host's network for testing; saved
Wi-Fi credentials were not replaced. After another reset, reconnect or
save the desired network through the existing Wi-Fi settings.

Reset finding and remaining verification
----------------------------------------

The user subsequently confirmed that the board is an ESP32-WROOM DevKit
and no external pull-down resistors are fitted on the selector lines.

The reset capture held reset for approximately 120 ms. GPIO19 then read
HIGH for 256.479 ms total, and all three selectors read HIGH together for
326.020 ms. Each signal had one rising and one falling edge, with no PWM
oscillation, and remained LOW after initialization.

The `ESP32 datasheet, Appendix A <https://documentation.espressif.com/esp32_datasheet_en.html>`_
lists these pins as output-disabled during reset. Unconnected analyzer
inputs also read HIGH, so floating pins/analyzer bias are a plausible
explanation. The digital trace does not prove active GPIO drive, voltage,
source impedance, or piezo energization.

GPIO19/21/22/23 support software-controlled internal pull-downs, but the
datasheet lists no pull-down enabled on these pins during reset. Enabling
one in application initialization would not resolve the reset interval.

Provide external pull-downs on active-high selector/control inputs, sized
for the actual driver, then repeat the capture through reset/startup with
the intended circuit connected. Firmware cannot drive these pins LOW
while reset is asserted. Electrical Off throughout reset remains
unverified. The tests otherwise cover ESP32 GPIO behavior; power-driver
operation, analog waveforms, and mist production were not tested.
