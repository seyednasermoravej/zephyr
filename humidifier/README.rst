Humidifier controls (ESP32 DevKitC)
==================================

The embedded web UI, HTTP API, and firmware support selecting Piezo 0, 1,
or 2, turning all piezos off, and saving each piezo's target frequency
(1.600--1.800 MHz) and PWM duty cycle (0--100%). One piezo can be selected
at a time. Settings survive reboot; firmware initializes outputs to Off. Old settings
are migrated to a versioned NVS record while preserving Wi-Fi credentials.

Build and connect
-----------------

From this application directory, with the Zephyr environment activated::

   west blobs fetch hal_espressif
   west build -b esp32_devkitc/esp32/procpu -d build . -- -DDTC_OVERLAY_FILE=boards/esp32.overlay
   west flash -d build

The board joins the saved Wi-Fi network and obtains an address using DHCP.
Open ``http://<board-ip>/`` using the address logged on the serial console.
On a fresh device, the default network name is ``Humidifier`` and its
password is ``12345678``. These are credentials for an external access
point; the board does not create a hotspot. Alternatively, connect from
the serial Zephyr shell with ``wifi disconnect`` followed by
``wifi connect -s "YOUR_SSID" -p "YOUR_PASSWORD" -k 1`` for WPA2-PSK
(see ``wifi connect -h`` for other options), then save your network in
the web UI for the next boot. Credential fields
currently support up to 31 bytes each.

If the saved network is unavailable, the initial connection attempt ends so
the serial shell can connect to another network. Automatic station retries
are disabled to avoid rejecting provisioning commands while reconnecting.

The ESP32 allows eight HTTP clients and closes idle connections after three
seconds. This leaves room for parallel browser requests and API access;
the previous three-client limit could leave new requests waiting while
browser keep-alive connections occupied every slot.

In Manual mode, select a piezo and adjust the frequency and PWM controls.
The UI shows both requested values and the applied hardware values.
Selecting Off sends an explicit command to disconnect all three selectors
and stop PWM. Changes are serialized, and rejected requests are displayed
with a Retry button. The existing scheduling UI has no implemented
``/schedules`` endpoint; these controls operate in Manual mode.

Debugging with ESP-PROG
----------------------

In VS Code, select ``ESP32: Debug (build + ESP-PROG flash)`` and press F5.
This builds a separate ``build-debug`` image using ``debug.conf``, programs
and verifies it through JTAG, and stops at ``main``. Continue execution to
start Wi-Fi and the web server. Stop an existing debug session before
starting another flash operation so the probe is available.
Ending the single-CPU debug session resumes the application, including
when the debugger disconnects while execution is paused.

The configuration uses the locally installed Espressif OpenOCD
``v0.12.0-esp32-20260703`` and Zephyr SDK ``1.0.1``. Adjust the paths in
``.vscode/launch.json`` and ``.vscode/tasks.json`` if those tools move.
JTAG runs at 500 kHz. The debug image includes Zephyr thread information;
OpenOCD exposes the active CPU only for thread-aware debugging. Flash
probing is disabled during debugging because it stalled on this board;
programming runs in a separate OpenOCD process with flash support enabled.

The flash task writes ``build-debug/zephyr/zephyr.bin`` at ``0x1000`` for
this classic ESP32 simple-boot configuration. It does not erase the NVS
partition at ``0x3b0000``. An attach-only debug profile is also available
when the board already contains the matching image. The normal-build
attach profile requires the matching ``build/zephyr/zephyr.elf`` image.

An optimized build can highlight the ``nvs_mount failed`` log line even
when the call succeeds. Inspect ``rc`` immediately after ``nvs_mount()``
and check ``fs->ready``. On the connected board, the debug build returned
``rc = 0`` and ``fs->ready = true``, with two 4096-byte sectors. Mount
errors now include the return code and geometry in the log.

PWM precision and wiring
------------------------

The ESP32 overlay uses GPIO19 / LEDC channel 1 / timer 1 for shared piezo
PWM, with active-high selectors on GPIO22, GPIO23, and GPIO21 for piezos
0, 1, and 2. Fan/LED outputs use timer 0. The firmware disconnects all
selectors before reconfiguring PWM, then connects the selected piezo.

Reset needs hardware biasing: a bench capture on 2026-10-12 read all three
selectors HIGH for about 326 ms during reset/startup, then LOW after
initialization. ESP32 output drivers are disabled during reset, so floating
pins can read HIGH; this digital capture does not establish whether a
connected piezo driver would turn on. Fit external pull-downs on active-high
selector/control inputs, sized for the connected driver, and repeat the
reset capture with that circuit connected. Firmware alone cannot keep these
pins driven LOW while reset is asserted. See the
`ESP32 datasheet, Appendix A <https://documentation.espressif.com/esp32_datasheet_en.html>`_.

The local Zephyr LEDC driver accepts integer source-clock cycles. With
the 80 MHz clock, the available frequencies within the requested range
are approximately 1.600000, 1.632653, 1.666667, 1.702128, 1.739130, and
1.777778 MHz. Targets are rounded to a supported period while keeping
the output within 1.6--1.8 MHz. In particular, a 1.800 MHz target produces
about 1.777778 MHz; this driver interface cannot produce exactly 1.800 MHz.
Duty cycle has 5-bit resolution (3.125% increments). 0% and 100% produce
constant low and high outputs, respectively, rather than an oscillation.
Reported applied values are calculated from timer settings, not measured.
See the `Espressif LEDC documentation <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/ledc.html>`_
for the frequency/duty resolution tradeoff.

This implementation targets the ESP32 DevKitC overlay. The experimental
ESP32-S3 and nRF52840 overlays need independent PWM and selector mappings
before they can offer equivalent control.

Piezo API
---------

``GET /piezos`` returns JSON containing ``active`` (-1 for Off, otherwise
0--2), three ``piezos`` records (``brightness``, ``intensity``, and
``frequencyHz``), ``actualFrequencyHz``, and ``actualDutyBasisPoints``.
The latter is percent multiplied by 100 (5000 means 50.00%). Both applied
fields are zero when Off. At 0%/100% duty, the frequency field describes
the configured timer period although the output is constant.

``POST /piezos`` requires ``piezoNum``. Other fields are optional and keep
their saved values when omitted. ``intensity`` is the existing API name
for PWM duty percentage. Commands accept only these four literal field
names and integer values; unknown/duplicate fields and trailing data are
rejected. Examples::

   curl -H 'Content-Type: application/json' -d '{"piezoNum":1,"frequencyHz":1700000,"intensity":50,"brightness":40}' http://<board-ip>/piezos
   curl -H 'Content-Type: application/json' -d '{"piezoNum":-1}' http://<board-ip>/piezos

Successful requests return the applied status. Invalid values return
HTTP 400, oversized bodies return 413, and output/storage failures return
500. Failed output configuration or saving stops the piezo output.

Use ``Content-Length`` request bodies, as the browser UI does. Fragmented
requests with that framing passed on the device. Chunked request bodies
are currently unsupported by the local Zephyr HTTP/1 transport; the
tested valid chunked command returned 400 without changing settings.

Host checks
-----------

Run from this application directory::

   node --test tests/ui.test.cjs
   python3 tests/backend/test_piezos_handler.py
   python3 tests/firmware/test_piezo_control.py
   python3 tests/firmware/test_piezo_storage.py
   g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined,address -Iinc tests/piezo_pwm_test.cpp -o /tmp/humidifier-piezo-pwm-test
   /tmp/humidifier-piezo-pwm-test

The tests exercise browser commands, HTTP request handling, and PWM
frequency/duty conversion.

Hardware checks
---------------

The ESP32 and Saleae Logic were tested on 2026-10-12. See
`the hardware test report <tests/hardware/RESULTS.rst>`_ for measurements,
captures, runtime fixes, and the unresolved reset behavior.

To repeat the waveform checks, enable Logic 2's automation server on port
10430, stop any manual capture, and install ``logic2-automation`` in a
Python virtual environment. Connect a shared ground and these channels:

* Channel 0: GPIO19, PWM.
* Channel 1: GPIO21, Piezo 2 selector.
* Channel 2: GPIO22, Piezo 0 selector.
* Channel 3: GPIO23, Piezo 1 selector.

Use a bench circuit prepared for 0--100% duty, including constant HIGH at
100%. With the board Off and reachable over HTTP, run (use a new output
directory for each capture run)::

   python tests/hardware/run_checks.py --url http://<board-ip> --output /tmp/humidifier-matrix
   python tests/hardware/check_switching.py --url http://<board-ip> --output /tmp/humidifier-switching
   python tests/hardware/check_api.py --url http://<board-ip> --output /tmp/humidifier-api.json

The matrix checks all three selectors at 1.6, 1.7, and 1.8 MHz targets and
0%, 25%, 50%, 75%, and 100% duty. Waveform scripts restore saved setpoints
and leave the outputs Off. The 24 MS/s analyzer gives only 13--15 samples
per PWM cycle, so these measurements cannot resolve every duty increment
accurately. These checks cover GPIO signals, not the power driver or mist
production.

Original HTTP server sample documentation
========================================

.. zephyr:code-sample:: sockets-http-server
   :name: HTTP Server
   :relevant-api: http_service http_server tls_credentials json

   Implement an HTTP(s) Server demonstrating various resource types.

Overview
--------

This sample application demonstrates the use of the :ref:`http_server_interface` library.
This library provides high-level functions to simplify and abstract server implementation.
The server supports the HTTP/1.1 protocol which can also be upgraded to HTTP/2,
it also support native HTTP/2 protocol without upgrading.

Requirement
-----------

`QEMU Networking <https://docs.zephyrproject.org/latest/connectivity/networking/qemu_setup.html#networking-with-qemu>`_

Building and running the server
-------------------------------

There are configuration files for various setups in the
:zephyr_file:`samples/net/sockets/http_server` directory:

.. list-table::

    * - :zephyr_file:`prj.conf <samples/net/sockets/http_server/prj.conf>`
      - This is the standard default config.

    * - :zephyr_file:`ieee802154-overlay.conf <samples/net/sockets/http_server/ieee802154-overlay.conf>`
      - This overlay config can be added for IEEE 802.15.4 support.

    * - :zephyr_file:`overlay-usbd.conf <samples/net/sockets/http_server/overlay-usbd.conf>`
      - This overlay config can be added for connecting via network USB.

    * - :zephyr_file:`overlay-tls.conf <samples/net/sockets/http_server/overlay-tls.conf>`
      - This overlay config can be added to build the HTTPS variant.

    * - :zephyr_file:`overlay-dhcpv4.conf <samples/net/sockets/http_server/overlay-dhcpv4.conf>`
      - This overlay enables DHCPv4 client feature.

To build and run the HTTP server application:

.. code-block:: bash

   $ west build -p auto -b <board_to_use> -t run samples/net/sockets/http_server

For the HTTPS version:

.. code-block:: bash

   $ west build -p auto -b <board_to_use> -t run --test samples/net/sockets/http_server/sample.net.sockets.https.server

When the server is up, we can make requests to the server using either HTTP/1.1 or
HTTP/2 protocol from the host machine.

**With HTTP/1.1:**

- Using a browser: ``http://192.0.2.1/``
- Using curl: ``curl -v --compressed http://192.0.2.1/``
- Using ab (Apache Bench): ``ab -n10 http://192.0.2.1/``

**With HTTP/2:**

- Using nghttp client: ``nghttp -v --no-dep http://192.0.2.1/``
- Using curl: ``curl --http2 -v --compressed http://192.0.2.1/``
- Using h2load: ``h2load -n10 http://192.0.2.1/``

Web browsers use stricter security settings for the HTTP/2 protocol. So to use HTTP/2
with a web browser, you must ALPN (add ``-DCONFIG_NET_SAMPLE_HTTPS_USE_ALPN`` to
the west build command) on top of the HTTPS build shown above.
Additionally the server certificate must be signed by a CA certificate trusted
by your browser.

The best way to do this is to generate your own CA certificate:

.. code-block:: bash

   $ west build -b <board_to_use> -t sample_ca_cert samples/net/sockets/http_server

Generate a server certificate signed by this CA certificate:

.. code-block:: bash

   $ west build -t sample_server_cert samples/net/sockets/http_server

And then build the application with the newly generated server certificate and key:

.. code-block:: bash

   $ west build samples/net/sockets/http_server

The CA certificate should be added to your browser's list of trusted authorities to
enable usage of HTTP/2. If using Firefox, it may also be required to change the setting
``network.http.http2.enforce-tls-profile`` to false, since it seems that using a CA
certificate issued by an authority unknown to Firefox is considered a security error when
using HTTP/2.

Server Customization
---------------------

The server sample contains several parameters that can be customized based on
the requirements. These are the configurable parameters:

- ``CONFIG_NET_SAMPLE_HTTP_SERVER_SERVICE_PORT``: Configures the service port.

- ``CONFIG_HTTP_SERVER_MAX_CLIENTS``: Defines the maximum number of HTTP/2
  clients that the server can handle simultaneously.

- ``CONFIG_HTTP_SERVER_MAX_STREAMS``: Specifies the maximum number of HTTP/2
  streams that can be established per client.

- ``CONFIG_HTTP_SERVER_CLIENT_BUFFER_SIZE``: Defines the buffer size allocated
  for each client. This limits the maximum length of an individual HTTP header
  supported.

- ``CONFIG_HTTP_SERVER_MAX_URL_LENGTH``: Specifies the maximum length of an HTTP
  URL that the server can process.

- ``CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE``: Enables Websocket service endpoint.
  This allows a Websocket client to connect to ``/`` endpoint, all the data that
  the client sends is echoed back.

To customize these options, we can run ``west build -t menuconfig``, which provides
us with an interactive configuration interface. Then we could navigate from the top-level
menu to: ``-> Subsystems and OS Services -> Networking -> Network Protocols``.

Websocket Connectivity
----------------------

You can use a simple Websocket client application like this to test the Websocket
connectivity.

.. code-block:: python

   import websocket

   websocket.enableTrace(True)
   ws = websocket.WebSocket()
   ws.connect("ws://192.0.2.1/ws_echo")
   ws.send("Hello, Server")
   print(ws.recv())
   while True:
     line = input("> ")
     if line == "quit":
       break
     ws.send(line)
     print(ws.recv())
   ws.close()


Testing over USB
----------------

Let's see a real example on how the HTTP(S) server can be tested on a real device
using an USB connection toward a Linux host PC. For this purpose let's take an
NRF52840 board as example.

First of all build the sample enabling HTTPS service and flash the board:

.. zephyr-app-commands::
         :zephyr-app: samples/net/sockets/http_server/
         :board: nrf52840dk/nrf52840
         :goals: build
         :gen-args: -DCONFIG_NET_SAMPLE_HTTPS_SERVICE=y -DEXTRA_CONF_FILE=overlay-netusb.conf

Then connect the USB cable to the host PC and issue:

.. code-block:: bash

   $ ip link show

to get the device name Linux assigned to the USB-Ethernet interface. For the
following let's assume that the name is ``eth-device``.

Now we need to configure IP and routing for this interface:

.. code-block:: bash

   $ sudo ip addr add 192.0.2.2/24 dev eth-device
   $ sudo ip route add 192.0.2.0/24 dev eth-device

Here:

* we picked an IP address for the interface, i.e. ``192.0.2.2/24``, which is
  different form the server one, i.e. :kconfig:option:`CONFIG_NET_CONFIG_MY_IPV4_ADDR`,
  but in the allowed IP range defined by the ``/24`` mask.
* we assume that 192.168.0.x range do not conflict with other addresses and
  routes in the host system. If that's the case, then all IP addresses should
  be fixed (sample, host IP interface, certificate).

Once this is done, it should be possible to test either HTTP and HTTPS with
``curl``:

.. code-block:: bash

   $ curl -v --compressed http://192.0.2.1
   $ curl -v --compressed https://192.0.2.1

.. note::

   To have a successful HTTPS connection ensure to update the CA certificates
   of the host Linux system adding
   :zephyr_file:`samples/net/sockets/http_server/src/certs/ca_cert.pem` to the
   list of known CAs.

Performance Analysis
--------------------

CPU Usage Profiling
*******************

We can use ``perf`` to collect statistics about the CPU usage of our server
running in native_sim board with the ``stat`` command:

.. code-block:: console

   $ sudo perf stat -p <pid_of_server>

``perf stat`` will then start monitoring our server. We can let it run while
sending requests to our server. Once we've collected enough data, we can
stop ``perf stat``, which will print a summary of the performance statistics.

Hotspot Analysis
****************

``perf record`` and ``perf report`` can be used together to identify the
functions in our code that consume the most CPU time:

.. code-block:: console

   $ sudo perf record -g -p <pid_of_server> -o perf.data

After running our server under load (For example, using ApacheBench tool),
we can stop the recording and analyze the data using:

.. code-block:: console

   $ sudo perf report -i perf.data

After generating a file named ``perf.data`` which contains the profiling data,
we can visualize it using ``FlameGraph`` tool. It's particularly useful for
identifying the most expensive code-paths and inspect where our application is
spending the most time.

To do this, we need to convert the ``perf.data`` to a format that ``FlameGraph``
can understand:

.. code-block:: console

   $ sudo perf script | ~/FlameGraph/stackcollapse-perf.pl > out.perf-folded

And, then, generate the ``FlameGraph``:

.. code-block:: console

   $ ~/FlameGraph/flamegraph.pl out.perf-folded > flamegraph.svg

We can view flamegraph.svg using a web browser.
