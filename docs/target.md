# Target - Equestrian Airbag Fall Detection

## Product Goal

This project targets an embedded fall-detection system for equestrian airbags. The system measures the real-time relative distance between rider and horse using UWB, with a target measurement rate of roughly 100 Hz or more and minimal latency.

Distance measurements are combined with accelerometer, gyroscope, and other inertial sensor data to detect fall situations as early as possible and trigger the airbag before impact.

The main technical challenge is to keep distance and motion estimates robust despite fast movement, vibration, body masking, changing RF conditions, and the power constraints of a wearable system.

## Continuous Motion Energy Signals

The firmware and Android pipeline should eventually expose continuous motion-energy channels, not just hard trigger thresholds. These values are intended as risk-score inputs first; threshold logic can be added later after dynamic captures.

The values below are proxies for physical energy. They do not attempt to compute exact kinetic energy, because mass, absolute speed, and body segment dynamics are not fully known from the embedded sensors alone.

### Accelerometer Dynamic Energy

Use accelerometer data to estimate how much non-gravity linear motion is present.

```text
a_dyn = measured_acceleration - estimated_gravity
E_acc = rolling_sum_or_rms(a_dyn^2)
```

A short rolling window, for example 100 ms to 300 ms, can capture abnormal movement while smoothing isolated spikes. This channel is useful but ambiguous, because it also reacts to horse gait, vibration, and impacts.

### Gyroscope Rotational Energy

Use gyroscope data to estimate how strongly the rider box is rotating.

```text
omega = norm(gx, gy, gz)
E_gyro = rolling_sum_or_rms(omega^2)
```

This is likely one of the most useful early fall indicators. A rider leaving the saddle often starts with a fast pitch, roll, or combined rotation before impact.

### Jerk Energy

Use the derivative of acceleration to detect sudden transitions.

```text
jerk = derivative(acceleration)
E_jerk = rolling_sum_or_rms(jerk^2)
```

This can highlight abrupt changes between normal riding motion and fall dynamics, but it amplifies sensor noise and should be filtered carefully.

### UWB Separation Dynamics

Use UWB distance to estimate rider-horse separation speed and acceleration.

```text
sep_v = derivative(distance)
sep_a = derivative(sep_v)
```

For the airbag use case, separation velocity is a key signal: the rider moving away from the horse quickly is more meaningful than distance alone.

## Future Risk Score Direction

A future fall-risk score should combine multiple continuous channels instead of relying on a single instant threshold.

```text
risk = w1 * E_gyro
     + w2 * E_acc
     + w3 * E_jerk
     + w4 * sep_v
     + w5 * tilt_delta
```

The likely priority order for early experiments is:

1. UWB separation velocity
2. Initiator gyroscope rotational energy
3. Tilt delta from the armed or learned riding baseline
4. Accelerometer dynamic energy
5. Jerk energy

Thresholds should be tuned from real dynamic captures, including normal riding, jumps, mounting/dismounting, near-falls, and actual controlled fall scenarios where safe and appropriate.
