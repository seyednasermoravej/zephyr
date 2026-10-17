Proposed three-piezo driver circuit
==================================

Revision A, 12 October 2026. This is a circuit proposal for review, not a
validated PCB design or a complete component-selection specification.
The existing ESP32 hardware tests measured logic outputs only; they did
not validate this proposed 24 V power stage.

Drawings
--------

* ``humidifier_reference.pdf``: two-sheet vector PDF.
* ``humidifier_reference.svg`` / ``.png``: shared power stage and control.
* ``selector_detail.svg`` / ``.png``: one selector cell, repeated three times.
* ``render_schematic.py``: editable drawing source; run with Python and matplotlib.

Changes from the supplied sketch
-------------------------------

The main MOSFET source and the three selected-piezo return paths join at
``RET`` above ``RS``. The resistor is between RET and true GND. This makes
the sensed current approximately the input-inductor current, rather than
the AC current in the piezo branch alone. Its average is useful for a
Holtek-style target-current frequency search. It is not an acoustic-power
measurement, piezo RMS-current measurement, or isolated measurement of
the main MOSFET's peak channel current.

The 24 V input reservoir capacitor connects to GND below RS. No power
return, ground wire, test instrument connection or bypass capacitor may
short RET directly to GND. K+ and K- are separate Kelvin sense traces from
the two resistor terminals. Ordinary grounded oscilloscope clips connect
to true GND; measure floating gate-source or drain-source voltages with an
appropriate differential measurement arrangement.

Each piezo has its own series DC-blocking capacitor and its own return
selector. The piezo negative terminals are not joined ahead of the
selectors. Each selector uses two N-channel MOSFETs with sources joined,
drains facing outward, and opposing body diodes. Their on-resistance,
voltage rating, off-state capacitance and leakage need assessment at the
actual ultrasonic voltage and current. Off-state capacitance means even
this topology does not provide perfect RF isolation.

Driver references
-----------------

The drawing deliberately uses isolated driver inputs and DC-capable
floating bias to make every gate-source reference explicit. This is one
implementation, not a claim that every existing driver must be replaced.
Reusing the user's existing driver requires its part number and input,
output, supply and ground specifications.

* Main driver U1: input supply/return are 3.3 V/GND. Output bias and return
  reference QMAIN's source, RET. Use a Kelvin return to the source. Its
  isolated bias negative is RET; do not connect it to true GND as well.
* Selector k: its output driver return and isolated bias negative connect
  only to the joined source node SREFk. SREF0, SREF1 and SREF2 are separate.
  Gate pull-downs connect to the respective sources, not logic GND.
* Each driver needs local supply bypass capacitors. As a starting layout
  provision, allow 100 nF ceramic plus a small local reservoir capacitor
  between each output supply and that driver's own return. Final values
  follow the selected driver's datasheet and gate-charge demand.
* Selectors stay on throughout atomizer bursts. Their bias must support
  indefinite ON time; a bootstrap-only supply is not sufficient by itself.
* U1 must support continuous 1.6--1.8 MHz operation at the actual MOSFET
  gate charge and drive voltage. Peak-current marketing ratings alone do
  not establish switching loss, thermal suitability or required edge times.

Current feedback and control
----------------------------

Use a current-sense front end whose common-mode range includes ground
and the expected transients. Set gain G and, if needed, a buffered VZERO
offset so the complete output waveform stays within the amplifier's and
ESP32 ADC's permitted ranges. After filtering and settling::

   Iavg = (Vadc - Vzero) / (G * Rs)

The front end and its input filter must tolerate the actual MHz ripple;
an arbitrary slow amplifier driven into overload will not give a correct
average. ADC calibration, offset subtraction and measured filter settling
are part of the eventual firmware. The shunt value and ADC GPIO are not
yet known and are therefore labeled TBD. ADC1 is proposed to avoid ESP32
ADC2/Wi-Fi resource contention; choose a free pin and update the overlay.

The separate comparator/latch path is for input/inductor overcurrent
shutdown. It must not share the ADC path's slow averaging filter. Its
threshold, propagation delay and any transient rejection require design.
This common shunt cannot guarantee QMAIN peak-current protection because
resonant branch current can circulate without appearing as equivalent
input current. Add dedicated sensing if that protection is required.

``GPIO19`` carries complete ultrasonic bursts. The logic equation is
``PWM_GATED = GPIO19 AND ARM AND FAULT_OK``. GPIO19 and ARM each have a
10 kohm pull-down to GND. The fault latch starts in the inhibited state.
ARM uses an additional GPIO, still to be assigned; firmware asserts it
only after exactly one selector is enabled and its turn-on time has elapsed.
Hardware inhibits drive when ARM or FAULT_OK is false; the driver must then
force QMAIN off. Implement the gate with suitable logic and characterize
startup/shutdown pulses. Fault threshold and response time remain to be set.

* 0% intensity: no carrier, QMAIN off.
* 100% intensity: continuous validated ultrasonic carrier, not constant HIGH.
* Intermediate intensity: vary the burst ON fraction while keeping the
  selected AC switch on and the within-cycle duty at a validated setting.
* To change piezos: stop drive, wait for measured ring-down, turn off the
  old selector, allow its turn-off time, enable the new one, allow its
  turn-on time, then resume drive. Do not drive with all selectors open.

The existing firmware still implements raw carrier duty, six discrete
frequency choices and a different selector-change sequence. These drawings
do not modify or flash firmware. It needs a separate implementation of burst
control, finer frequency generation, real current sensing, fault handling
and the sequence above after component details are confirmed.

Values still requiring validation
---------------------------------

4.7 uH and 100 nF are copied from the user's sketch as provisional starting
values, not calculated or validated resonance values. Specify the piezo
impedance and ratings under its actual water/mounting conditions, MOSFET
part numbers, gate-driver part number, shunt value, supply-current target,
and ADC circuitry before finalizing the circuit. The switch node can exceed
24 V: capacitor and MOSFET voltage ratings must follow measured peak
voltages plus suitable margin. L1's saturation current and losses must also
be checked. Snubber/clamp requirements depend on the measured switching
waveform and are not assigned arbitrary values in this proposal.

Primary references
------------------

* `Holtek WAS-1995EN, Figure 5 and frequency tracking <https://www.holtek.com/webapi/5905185/WAS-1995EN_ReferenceDesign.pdf>`_.
  The common-return sensing arrangement is adapted to the three branches;
  this drawing is not a reproduction of Holtek's validated single-piezo board.
* `TI, bidirectional switches and opposing MOSFETs <https://www.ti.com/lit/an/slva948/slva948.pdf>`_.
* `TI, gate-driver fundamentals <https://www.ti.com/lit/ml/slua618a/slua618a.pdf>`_.
* `TI, low-side current-sense layout and Kelvin connections <https://www.ti.com/document-viewer/lit/html/SSZT805/GUID-5F278960-BA94-4362-9099-87ABC753A4D6>`_.
* `Espressif, ADC interface and input range <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/adc/index.html>`_.
