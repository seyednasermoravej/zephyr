#!/usr/bin/env python3
"""Capture live selector switching on the same wiring as run_checks.py."""
import argparse
import csv
import json
import time
from pathlib import Path
from saleae import automation
from run_checks import request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--url', required=True)
parser.add_argument('--output', type=Path, required=True)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
base = a.url.rstrip('/')
before = request(base)
off = {'piezoNum': -1}
try:
    request(base, off)
    with automation.Manager.connect(port=10430) as manager:
        devices = manager.get_devices(include_simulation_devices=False)
        assert len(devices) == 1
        with manager.start_capture(
            device_id=devices[0].device_id,
            device_configuration=automation.LogicDeviceConfiguration(
                enabled_digital_channels=[0, 1, 2, 3], digital_sample_rate=24_000_000),
            capture_configuration=automation.CaptureConfiguration(
                capture_mode=automation.TimedCaptureMode(duration_seconds=2.0)),
        ) as capture:
            time.sleep(.05)
            for index in range(3):
                request(base, {'piezoNum': index, 'frequencyHz': 1700000, 'intensity': 50})
                time.sleep(.12)
            request(base, off)
            capture.wait()
            capture.save_capture(str(a.output / 'capture.sal'))
            capture.export_raw_data_csv(str(a.output), digital_channels=[0, 1, 2, 3])
finally:
    request(base, off)
    for index, saved in enumerate(before['piezos']):
        try:
            request(base, {'piezoNum': index, **saved})
        finally:
            request(base, off)

changes = []
last_mask = None
rows = 0
with (a.output / 'digital.csv').open() as f:
    for row in csv.DictReader(f):
        rows += 1
        timestamp = float(row['Time [s]'])
        mask = sum(int(row[f'Channel {ch}']) << ch for ch in (1, 2, 3))
        assert mask in (0, 2, 4, 8), f'Multiple selectors active at {timestamp}s'
        if mask != last_mask:
            changes.append({'time_seconds': timestamp, 'mask': mask})
            last_mask = mask
        final_pwm = int(row['Channel 0'])
assert [x['mask'] for x in changes] == [0, 4, 0, 8, 0, 2, 0], changes
assert final_pwm == 0
gaps = [(changes[i + 1]['time_seconds'] - changes[i]['time_seconds']) * 1e6
        for i in (2, 4)]
assert all(gap > 0 for gap in gaps), gaps
result = {'passed': True, 'rows': rows, 'selector_changes': changes,
          'break_before_make_microseconds': gaps, 'final_pwm': final_pwm,
          'final_selector_mask': last_mask, 'sampling_rate': 24000000}
(a.output / 'result.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
