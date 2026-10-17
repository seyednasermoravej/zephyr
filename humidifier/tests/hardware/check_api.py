#!/usr/bin/env python3
"""Check validation on a real, idle board. Requires every piezo to be OFF."""
import argparse
import json
import socket
import sys
import time
import urllib.request
import urllib.error
import urllib.parse
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--url', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
base = a.url.rstrip('/')
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def call(raw=None):
    req = urllib.request.Request(base + '/piezos', data=raw,
                                 headers={'Content-Type': 'application/json'})
    try:
        with opener.open(req, timeout=6) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.load(e)


status, before = call()
assert status == 200 and before['active'] == -1, 'Board must be OFF before testing'
cases = [
    ('missing selection', '{}'),
    ('invalid selector', '{"piezoNum":3}'),
    ('invalid negative selector', '{"piezoNum":-2}'),
    ('low frequency', '{"piezoNum":0,"frequencyHz":1599999}'),
    ('high frequency', '{"piezoNum":0,"frequencyHz":1800001}'),
    ('low duty', '{"piezoNum":0,"intensity":-1}'),
    ('high duty', '{"piezoNum":0,"intensity":101}'),
    ('duplicate key', '{"piezoNum":0,"piezoNum":1}'),
    ('unknown key', '{"piezoNum":0,"unknown":1}'),
    ('trailing bytes', '{"piezoNum":0}garbage'),
    ('overflow', '{"piezoNum":4294967296}'),
    ('float', '{"piezoNum":0,"intensity":50.5}'),
    ('truncated', '{"piezoNum":'),
    ('embedded NUL', '{"piezoNum":0}\0'),
    ('oversize', ' ' * 256),
]
try:
    results = []
    for name, payload in cases:
        code, data = call(payload.encode())
        expected = 413 if name == 'oversize' else 400
        assert code == expected, (name, code, data)
        assert call()[1] == before, (name, 'state changed')
        results.append({'case': name, 'status': code, 'unchanged': True})
    code, data = call(b'{"piezoNum":-1}'.ljust(255, b' '))
    assert code == 200 and data['active'] == -1
    results.append({'case': '255-byte body boundary', 'status': code})
    address = urllib.parse.urlparse(base)
    body = b'{"piezoNum":-1}'
    def raw_post(chunked):
        with socket.create_connection((address.hostname, address.port or 80), timeout=6) as connection:
            framing = 'Transfer-Encoding: chunked' if chunked else f'Content-Length: {len(body)}'
            connection.sendall((f'POST /piezos HTTP/1.1\r\nHost: {address.netloc}\r\n'
                                f'Content-Type: application/json\r\n{framing}\r\n'
                                'Connection: close\r\n\r\n').encode())
            for part in [body[:4], body[4:10], body[10:]]:
                connection.sendall(f'{len(part):x}\r\n'.encode() + part + b'\r\n' if chunked else part)
                time.sleep(.05)
            if chunked:
                connection.sendall(b'0\r\n\r\n')
            reply = b''
            while True:
                part = connection.recv(4096)
                if not part:
                    break
                reply += part
        return int(reply.split(b'\r\n', 1)[0].split()[1]), reply


    code, reply = raw_post(False)
    assert code == 200 and b'"active":-1' in reply, reply
    assert call()[1] == before
    results.append({'case': 'fragmented Content-Length off', 'status': code})
    # Current Zephyr HTTP/1 transport mishandles chunk framing before invoking the
    # application callback. Verify rejection leaves state unchanged; do not claim
    # support for this upstream limitation. Browser fetch uses Content-Length.
    code, reply = raw_post(True)
    assert code == 400, reply
    assert call()[1] == before
    results.append({'case': 'chunked request unsupported by current Zephyr transport',
                    'status': code, 'unchanged': True, 'limitation': True})
finally:
    # An invalid command accepted by a regression might enable a piezo. Always
    # request OFF, and preserve the original failure if cleanup also fails.
    test_error = sys.exc_info()[1]
    try:
        code, data = call(b'{"piezoNum":-1}')
        if code != 200 or data.get('active') != -1:
            raise RuntimeError(f'OFF cleanup failed: HTTP {code}, {data!r}')
    except BaseException as cleanup_error:
        if test_error is None:
            raise
        print(f'OFF cleanup also failed: {cleanup_error}', file=sys.stderr)

a.output.write_text(json.dumps(results, indent=2))
limitations = sum(bool(result.get('limitation')) for result in results)
print(f'{len(results) - limitations} supported HTTP checks passed; '
      f'{limitations} known limitation confirmed (chunked requests remain unsupported).')
