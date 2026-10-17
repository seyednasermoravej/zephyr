#!/usr/bin/env python3
"""Exercise a real humidifier and Saleae Logic2; leave every piezo off.

Requires logic2-automation, its local server, and an ESP32 reachable over HTTP.
Channel defaults: GPIO19=0, GPIO21=1, GPIO22=2, GPIO23=3 (shared ground).
Run only on a bench setup prepared for the requested 0..100% duty outputs.
"""
import argparse
import csv
import json
import math
import urllib.request
from pathlib import Path
from saleae import automation


def request(base, payload=None, path='/piezos'):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(base + path, data=data,
                                 headers={'Content-Type': 'application/json'})
    # Device traffic must stay on the LAN, regardless of host proxy settings.
    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=8) as r:
        return json.load(r)


def measure(csv_path):
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        first = next(reader)
        time_key = next(iter(first))
        start = last_time = float(first[time_key])
        channels = {k: {'initial': int(v), 'final': int(v), 'transitions': 0,
                        'rises': 0, 'first_rise': None, 'last_rise': None,
                        'high_seconds': 0.0}
                    for k, v in first.items() if k != time_key}
        overlap_samples = int(sum(int(first[f'Channel {ch}']) for ch in (1, 2, 3)) > 1)
        for row in reader:
            now = float(row[time_key])
            for name, info in channels.items():
                value = int(row[name])
                info['high_seconds'] += info['final'] * (now - last_time)
                if value != info['final']:
                    info['transitions'] += 1
                    if value:
                        info['rises'] += 1
                        if info['first_rise'] is None:
                            info['first_rise'] = now
                        info['last_rise'] = now
                info['final'] = value
            if sum(int(row[f'Channel {ch}']) for ch in (1, 2, 3)) > 1:
                overlap_samples += 1
            last_time = now
    duration = last_time - start
    if duration <= 0:
        raise RuntimeError('Capture has no measurable duration')
    for info in channels.values():
        info['high_percent'] = 100 * info.pop('high_seconds') / duration
        if info['rises'] > 1:
            info['frequency_hz'] = (info['rises'] - 1) / (info['last_rise'] - info['first_rise'])
    return {'duration_seconds': duration, 'channels': channels,
            'overlapping_selector_samples': overlap_samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--quick', action='store_true', help='Only 1.7 MHz / 50% on each piezo')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    base = args.url.rstrip('/')
    baseline = request(base)
    (args.output / 'before.json').write_text(json.dumps(baseline, indent=2))
    results = []
    expected_hz = {1600000: 1600000.0, 1700000: 80000000 / 47, 1800000: 80000000 / 45}
    matrix = [(p, f, d) for p in range(3)
              for f in ([1700000] if args.quick else expected_hz)
              for d in ([50] if args.quick else [0, 25, 50, 75, 100])]
    off = {'piezoNum': -1}
    try:
        request(base, off)
        with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as manager:
            devices = manager.get_devices(include_simulation_devices=False)
            if len(devices) != 1:
                raise RuntimeError(f'Expected one physical analyzer: {devices!r}')
            for piezo, frequency, duty in matrix:
                label = f'p{piezo}_{frequency}Hz_{duty}pct'
                folder = args.output / label
                folder.mkdir()
                command = {'piezoNum': piezo, 'frequencyHz': frequency, 'intensity': duty}
                applied = request(base, command)
                try:
                    with manager.start_capture(
                        device_id=devices[0].device_id,
                        device_configuration=automation.LogicDeviceConfiguration(
                            enabled_digital_channels=[0, 1, 2, 3], digital_sample_rate=24_000_000),
                        capture_configuration=automation.CaptureConfiguration(
                            capture_mode=automation.TimedCaptureMode(duration_seconds=0.02)),
                    ) as capture:
                        capture.wait()
                        # Stop output immediately after sampling, before file export/analysis.
                        request(base, off)
                        capture.save_capture(str(folder / 'capture.sal'))
                        capture.export_raw_data_csv(str(folder), digital_channels=[0, 1, 2, 3])
                finally:
                    request(base, off)
                measured = measure(folder / 'digital.csv')
                errors = []
                if applied.get('active') != piezo:
                    errors.append('API active piezo mismatch')
                if applied['piezos'][piezo]['frequencyHz'] != frequency or applied['piezos'][piezo]['intensity'] != duty:
                    errors.append('API requested setting mismatch')
                expected_selector = {0: 2, 1: 3, 2: 1}[piezo]
                for ch in (1, 2, 3):
                    info = measured['channels'][f'Channel {ch}']
                    if info['transitions'] or info['initial'] != int(ch == expected_selector):
                        errors.append(f'Selector channel {ch} did not stay at expected level')
                pwm = measured['channels']['Channel 0']
                if duty in (0, 100):
                    if pwm['transitions'] or pwm['initial'] != int(duty == 100):
                        errors.append('PWM endpoint was not a constant expected level')
                else:
                    freq = pwm.get('frequency_hz')
                    if freq is None or not math.isclose(freq, expected_hz[frequency], rel_tol=.005):
                        errors.append('PWM frequency differs by more than 0.5%')
                    if abs(pwm['high_percent'] - duty) > 8:
                        errors.append('PWM duty differs by more than 8 percentage points at 24 MS/s')
                result = {'label': label, 'command': command, 'api': applied,
                          'measurement': measured, 'errors': errors, 'passed': not errors}
                results.append(result)
                (folder / 'result.json').write_text(json.dumps(result, indent=2))
                (args.output / 'results.json').write_text(json.dumps(results, indent=2))
                print(json.dumps({'label': label, 'passed': not errors, 'errors': errors,
                                  'frequency_hz': pwm.get('frequency_hz'),
                                  'duty_percent': pwm['high_percent']}), flush=True)
                if errors:
                    raise RuntimeError(f'Physical measurement failed: {label}: {errors}')
    finally:
        # Restore each original setpoint, then disconnect immediately.
        request(base, off)
        for index, saved in enumerate(baseline['piezos']):
            try:
                request(base, {'piezoNum': index, **saved})
            finally:
                request(base, off)
    print(f'{len(results)} hardware cases passed; all piezos are off.', flush=True)


if __name__ == '__main__':
    main()
