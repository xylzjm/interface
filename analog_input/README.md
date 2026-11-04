# analog_input — Hardware-independent Analog Input Interface

A portable C99 interface for analog input channels: PT100, thermocouple,
4-20 mA and 0-10 V. The interface defines the operation table a concrete
hardware driver supplies and the conversion helpers that turn a raw sample into
an engineering value.

- **Operation table**: a concrete driver implements six operations and
  installs them with `analog_input_ctor()`. Everything an application calls is
  an inline wrapper in `analog_input.h`, so the interface has no link-time
  dependency on any particular driver.
- **Sensor conversion**: PT100 and thermocouple helpers convert between the
  electrical quantity and the engineering value without touching hardware.
- **Two work modes**: measurement and wire-break detection, mutually exclusive
  because they are work modes of the same converter.
- **No hardware assumptions**: the interface includes only `<stdint.h>` and
  `<stddef.h>` — no MCU, RTOS or vendor headers.

## Layout

```
analog_input/                this module's root
├── README.md                 this file
├── analog_input.h            public interface and inline application helpers
└── analog_input_math.c       PT100 and thermocouple conversion math
```

## Implementing the interface

A concrete implementation embeds `analog_input_t` as the first member of its
structure and installs the operation table with `analog_input_ctor()`. The
concrete state can be recovered from the `analog_input_t` pointer because the
base member is first.

The cached configuration is owned by the implementation's `configure()`
operation. Before returning success it stores the configuration that is now in
effect into `self->config`, which may differ from the request where a default
was resolved. When it fails, it must not leave `self->config` describing a
device that was left in another state.

`analog_input_ctor()` leaves behind a configuration the interface itself
rejects — type `THERMOCOUPLE` combined with `thermocouple_type` `NONE`. A
concrete implementation can use that pair to recognise an object that was never
configured, without keeping a separate flag.

## Work modes

Break detection and measurement are work modes of the same hardware, so they
are never active at the same time: enabling detection leaves the measurement
mode, and only disabling it again brings the measurement back. Detection is
not defined for 4-20 mA and 0-10 V inputs, where an interrupted loop or an
absent voltage looks like a valid low reading.

`disable_break_detection()` restores the configuration that `configure()` last
accepted, so the object returns to the measurement mode it describes.

## PT100 conversion

The PT100 helper uses the Callendar-Van Dusen resistance-temperature model of
IEC 60751 for a 100 ohm sensor:

```
R(T) = R0 * (1 + A*T + B*T^2)                   T >= 0
R(T) = R0 * (1 + A*T + B*T^2 + C*(T-100)*T^3)   T <  0
```

The input resistance is in ohms and the output temperature is in degrees
Celsius. The quadratic form of the model is inverted analytically; below 0
degrees, where the cubic coefficient applies, the result is refined with a few
Newton steps. Resistances outside the -200 .. +850 degrees range are rejected
instead of being extrapolated.

## Thermocouple conversion

The thermocouple helpers use the ITS-90 reference functions from NIST
Monograph 175 ("Temperature-Electromotive Force Reference Functions and Tables
for the Letter-Designated Thermocouple Types Based on the ITS-90"). Each range
is

```
E(t90) = sum(c_i * t90^i) + a0 * exp(a1 * (t90 - a2)^2)
```

with `E` in microvolts and `t90` in degrees Celsius; the exponential term is
used by type K only. All sets were checked against the fixed-point values of
the monograph (ice point, tin, silver, gold, ...) to better than 0.1 microvolt.

The helper functions cover the sensor polynomial only. Cold-junction
compensation is composed by the caller, by adding the cold-junction emf to the
measured emf before the inverse conversion.

### Inverting the reference function

The reference functions are monotonic, so a bracketed Newton iteration always
converges; the bracket is also used to fall back to bisection when a Newton
step would leave the interval. Linear interpolation between the range ends is
the first guess.

### Type B near ambient

Type B is only usable above roughly 250 C: near ambient its output is a few
microvolts on a flat curve, so input noise may be negative. Clamping to the ice
point keeps the channel usable instead of flagging a conversion error on every
reading.

## Accuracy

The polynomials are evaluated in single precision. Above -200 C the result
stays within 0.1 C of the reference; below -240 C the cancellation between the
high order terms of types E and T costs a few degrees (about 3 C for type T at
-260 C). Use double math there if cryogenic accuracy matters.
