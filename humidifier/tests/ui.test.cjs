// Run with: node --test tests/ui.test.cjs
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');

const webRoot = join(__dirname, '../src/static_web_resources');
const html = readFileSync(join(webRoot, 'index.html'), 'utf8');
const script = readFileSync(join(webRoot, 'main.js'), 'utf8');
const manualIds = ['m_br', 'm_br_in', 'm_int', 'm_int_in', 'm_freq', 'm_freq_in'];

function deferred() {
  let resolve;
  const promise = new Promise(r => { resolve = r; });
  return { promise, resolve };
}

function waitForAbort(signal) {
  return new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(new Error('Aborted')), { once: true });
  });
}

async function settle() {
  for (let i = 0; i < 20; i++) await Promise.resolve();
}

function response(data, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => data,
    text: async () => typeof data === 'string' ? data : JSON.stringify(data)
  };
}

function settings(active = 0) {
  return {
    active,
    piezos: [
      { brightness: 0, intensity: 0, frequencyHz: 1600000 },
      { brightness: 10, intensity: 50, frequencyHz: 1700000 },
      { brightness: 100, intensity: 100, frequencyHz: 1800000 }
    ],
    actualFrequencyHz: active === -1 ? 0 : 1600000,
    actualDutyBasisPoints: 0
  };
}

function element(tag = '') {
  const node = {
    value: /\bvalue="([^"]*)"/.exec(tag)?.[1] ?? '',
    disabled: /\bdisabled\b/.test(tag),
    checked: /\bchecked\b/.test(tag),
    className: /\bclass="([^"]*)"/.exec(tag)?.[1] ?? '',
    textContent: '',
    handlers: {},
    addEventListener(type, handler) { this.handlers[type] = handler; },
    dispatch(type, value = this.value) {
      this.value = value;
      const handler = this.handlers[type] || this[`on${type}`];
      assert.ok(handler, `Missing ${type} handler`);
      return handler({ target: this });
    }
  };
  node.classList = {
    toggle(name, enabled) {
      const classes = new Set(node.className.split(' ').filter(Boolean));
      if (enabled) classes.add(name);
      else classes.delete(name);
      node.className = [...classes].join(' ');
    }
  };
  return node;
}

function harness(fetchImpl = async () => response(settings())) {
  const nodes = new Map();
  for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
    nodes.set(match[1], element(match[0]));
  }
  const radios = [...html.matchAll(/<input[^>]+name="active-piezo"[^>]*>/g)]
    .map(match => element(match[0]));
  const timers = new Map();
  const requests = [];
  let timerId = 0;
  const context = vm.createContext({
    document: {
      getElementById: id => nodes.get(id),
      addEventListener() {},
      querySelectorAll: selector => {
        if (selector === 'input[name="active-piezo"]') return radios;
        if (selector === '#manual-controls input') return manualIds.map(id => nodes.get(id));
        return [];
      }
    },
    fetch: (url, options) => {
      requests.push({ url, options });
      return fetchImpl(url, options);
    },
    setTimeout: (fn, delay) => { timers.set(++timerId, { fn, delay }); return timerId; },
    clearTimeout: id => timers.delete(id),
    setInterval() {},
    AbortController,
    console
  });
  vm.runInContext(`${script}\nthis.api = {
    state, writes, loadPiezos, loadFan, syncPiezo, renderAppliedPiezo,
    bindPiezoBullets, bindManualSliders, bindGlobalFan, renderPiezoControls,
    updateUptime, updateClock, REQUEST_TIMEOUT_MS
  };`, context);
  const api = context.api;
  api.bindPiezoBullets();
  api.bindManualSliders();
  api.bindGlobalFan();
  api.renderPiezoControls();
  const runTimers = delay => {
    for (const [id, timer] of [...timers]) {
      if (timer.delay === delay) { timers.delete(id); timer.fn(); }
    }
  };
  return {
    api, nodes, requests, runTimers,
    timerCount: delay => [...timers.values()].filter(timer => timer.delay === delay).length,
    select: value => radios.find(r => r.value === String(value)).dispatch('change'),
    radios,
    async flush() {
      runTimers(250);
      await api.writes.piezo;
      await api.writes.fan;
    },
    posts: () => requests.filter(r => r.options?.method === 'POST')
      .map(r => ({ url: r.url, body: JSON.parse(r.options.body) }))
  };
}

test('controls wait for loaded data and preserve zero duty, brightness, and fan speed', async () => {
  const firstRead = deferred();
  const h = harness(url => url === '/piezos' ? firstRead.promise : response({ speed: 0 }));
  const loading = h.api.loadPiezos();
  assert.ok(h.radios.every(r => r.disabled));
  assert.ok(manualIds.every(id => h.nodes.get(id).disabled));
  firstRead.resolve(response(settings()));
  await loading;
  await h.api.loadFan();
  assert.equal(h.api.state.activePiezo, 0);
  assert.ok(h.radios.every(r => !r.disabled));
  assert.ok(manualIds.every(id => !h.nodes.get(id).disabled));
  assert.equal(h.nodes.get('m_br').value, 0);
  assert.equal(h.nodes.get('m_int').value, 0);
  assert.equal(h.nodes.get('m_freq_in').value, '1.600');
  assert.equal(h.nodes.get('fan_sl').value, 0);
  assert.equal(h.posts().length, 0);
});

test('frequency MHz values and 0–100% duty reach the API as integers', async () => {
  const h = harness();
  await h.api.loadPiezos();
  h.nodes.get('m_freq_in').dispatch('change', '1.800');
  h.nodes.get('m_int_in').dispatch('change', '100');
  await h.flush();
  assert.deepEqual(h.posts()[0].body, {
    piezoNum: 0, brightness: 0, intensity: 100, frequencyHz: 1800000
  });
  h.nodes.get('m_freq').dispatch('input', '1.600');
  h.nodes.get('m_int').dispatch('input', '0');
  await h.flush();
  assert.equal(h.posts()[1].body.frequencyHz, 1600000);
  assert.equal(h.posts()[1].body.intensity, 0);
  h.nodes.get('m_freq_in').dispatch('change', '2.000');
  await h.flush();
  assert.equal(h.posts()[2].body.frequencyHz, 1800000);
});

test('Off sends a command, cancels pending edits, and disables manual inputs', async () => {
  const h = harness();
  await h.api.loadPiezos();
  h.nodes.get('m_int').dispatch('input', '85');
  h.select(-1);
  await h.flush();
  assert.deepEqual(h.posts(), [{ url: '/piezos', body: { piezoNum: -1 } }]);
  assert.ok(manualIds.every(id => h.nodes.get(id).disabled));
  h.select(2);
  await h.flush();
  assert.deepEqual(h.posts()[1].body, {
    piezoNum: 2, brightness: 100, intensity: 100, frequencyHz: 1800000
  });
});

test('rapid selections are serialized through Off with one request in flight', async () => {
  const pending = [];
  const h = harness((url, options) => {
    if (options.method !== 'POST') return response(settings());
    const request = deferred();
    pending.push(request);
    return request.promise;
  });
  await h.api.loadPiezos();
  h.select(1);
  await Promise.resolve();
  h.select(2);
  h.select(-1);
  assert.equal(pending.length, 1);
  pending[0].resolve(response(settings(1)));
  // Resolve the chain's first request before allowing the next one to start.
  for (let i = 0; i < 10; i++) await Promise.resolve();
  assert.equal(pending.length, 2);
  assert.equal(h.nodes.get('piezo-status').textContent, 'Applying settings...');
  pending[1].resolve(response(settings(2)));
  for (let i = 0; i < 10; i++) await Promise.resolve();
  assert.equal(pending.length, 3);
  pending[2].resolve(response(settings(-1)));
  await h.flush();
  assert.deepEqual(h.posts().map(p => p.body.piezoNum), [1, 2, -1]);
  assert.equal(h.nodes.get('piezo-applied').textContent, 'All piezos are off.');
});

test('HTTP write failures are visible, retryable, and do not break the queue', async () => {
  let fail = true;
  const h = harness((url, options) => options.method === 'POST' && fail
    ? response('PWM update failed', 500)
    : response({ ...settings(2), actualFrequencyHz: 1777778, actualDutyBasisPoints: 5000 }));
  await h.api.loadPiezos();
  h.select(2);
  await h.flush();
  assert.match(h.nodes.get('piezo-status').textContent, /PWM update failed/);
  assert.equal(h.nodes.get('piezo-status').className, 'status error');
  assert.doesNotMatch(h.nodes.get('piezo-retry').className, /hidden/);
  assert.equal(h.requests.filter(r => r.options.method !== 'POST').length, 2);
  assert.match(h.nodes.get('piezo-applied').textContent, /Piezo 2/);
  fail = false;
  h.nodes.get('piezo-retry').dispatch('click');
  await h.flush();
  assert.equal(h.nodes.get('piezo-status').className, 'status success');
  assert.match(h.nodes.get('piezo-applied').textContent, /1\.777778 MHz.*50\.00%/);
  assert.match(h.nodes.get('piezo-retry').className, /hidden/);
});

test('failed initial reads keep controls disabled and can be retried', async () => {
  let fail = true;
  const h = harness(() => fail ? response('Device unavailable', 503) : response(settings(-1)));
  await h.api.loadPiezos();
  assert.ok(h.radios.every(r => r.disabled));
  assert.match(h.nodes.get('piezo-status').textContent, /Device unavailable/);
  fail = false;
  await h.api.loadPiezos();
  assert.ok(h.radios.every(r => !r.disabled));
  assert.ok(manualIds.every(id => h.nodes.get(id).disabled));
  assert.equal(h.api.state.activePiezo, -1);
});

test('readback after a failed request does not overwrite a newer command', async () => {
  const readback = deferred();
  const nextWrite = deferred();
  let reads = 0;
  let posts = 0;
  const h = harness((url, options) => {
    if (options.method !== 'POST') return ++reads === 1 ? response(settings()) : readback.promise;
    return ++posts === 1 ? response('Driver error', 500) : nextWrite.promise;
  });
  await h.api.loadPiezos();
  h.select(1);
  for (let i = 0; i < 10; i++) await Promise.resolve();
  assert.equal(reads, 2);
  h.select(2);
  readback.resolve(response(settings(0)));
  for (let i = 0; i < 10; i++) await Promise.resolve();
  assert.equal(posts, 2);
  assert.equal(h.nodes.get('piezo-applied').textContent, 'Awaiting confirmation from the device.');
  nextWrite.resolve(response({ ...settings(2), actualFrequencyHz: 1777778, actualDutyBasisPoints: 5000 }));
  await h.flush();
  assert.match(h.nodes.get('piezo-applied').textContent, /Piezo 2/);
  assert.equal(h.nodes.get('piezo-status').className, 'status success');
});

test('applied values distinguish constant output at the duty endpoints', () => {
  const h = harness();
  h.api.renderAppliedPiezo({ actualFrequencyHz: 1600000, actualDutyBasisPoints: 0 });
  assert.match(h.nodes.get('piezo-applied').textContent, /constant low \(0\.00%/);
  h.api.renderAppliedPiezo({ actualFrequencyHz: 1777778, actualDutyBasisPoints: 10000 });
  assert.match(h.nodes.get('piezo-applied').textContent, /constant high \(100\.00%/);
});

test('a stalled write times out, drops its backlog, and retries only the latest requested settings', async () => {
  let connected = true;
  const h = harness((url, options) => connected
    ? response(settings())
    : waitForAbort(options.signal));
  await h.api.loadPiezos();
  connected = false;
  h.select(1);
  await settle();
  h.select(2);
  h.nodes.get('m_int_in').dispatch('change', '28');
  h.runTimers(250);
  assert.equal(h.posts().length, 1);
  h.runTimers(h.api.REQUEST_TIMEOUT_MS);
  await settle();
  assert.match(h.nodes.get('piezo-status').textContent, /timed out/);
  assert.equal(h.nodes.get('piezo-status').className, 'status error');
  assert.doesNotMatch(h.nodes.get('piezo-retry').className, /hidden/);
  assert.equal(h.requests.find(r => r.options.method === 'POST').options.signal.aborted, true);
  assert.equal(h.api.state.activePiezo, 2);
  assert.equal(h.api.state.piezos[2].intensity, 28);
  // The readback also gets a deadline; neither it nor the queued edits retry a write.
  h.runTimers(h.api.REQUEST_TIMEOUT_MS);
  await h.flush();
  assert.equal(h.posts().length, 1);
  assert.equal(h.nodes.get('piezo-applied').textContent, 'Applied settings are unconfirmed.');
  connected = true;
  h.nodes.get('piezo-retry').dispatch('click');
  await h.flush();
  assert.equal(h.posts().length, 2);
  assert.deepEqual(h.posts()[1].body, {
    piezoNum: 2, brightness: 100, intensity: 28, frequencyHz: 1800000
  });
  assert.equal(h.nodes.get('piezo-status').className, 'status success');
});

test('the write deadline covers a stalled response body after headers arrive', async () => {
  const h = harness((url, options) => options.method === 'POST'
    ? { ...response(settings(1)), json: () => waitForAbort(options.signal) }
    : response(settings()));
  await h.api.loadPiezos();
  h.select(1);
  await settle();
  h.runTimers(h.api.REQUEST_TIMEOUT_MS);
  await h.flush();
  assert.match(h.nodes.get('piezo-status').textContent, /timed out/);
  assert.doesNotMatch(h.nodes.get('piezo-retry').className, /hidden/);
  assert.equal(h.posts().length, 1);
});

test('a stalled initial read times out and leaves a usable Retry button', async () => {
  const h = harness((url, options) => waitForAbort(options.signal));
  const loading = h.api.loadPiezos();
  h.runTimers(h.api.REQUEST_TIMEOUT_MS);
  await loading;
  assert.ok(h.radios.every(r => r.disabled));
  assert.match(h.nodes.get('piezo-status').textContent, /timed out/);
  assert.doesNotMatch(h.nodes.get('piezo-retry').className, /hidden/);
});

for (const [name, method, url, recoveredBody] of [
  ['uptime', 'updateUptime', '/uptime', '12345'],
  ['clock', 'updateClock', '/time', { timestamp: 1700000000, synced: true }]
]) {
  test(`${name} polling waits for a stalled read to finish before scheduling another`, async () => {
    let connected = false;
    const h = harness((path, options) => connected
      ? response(recoveredBody)
      : waitForAbort(options.signal));
    const polling = h.api[method]();
    for (let i = 0; i < 10; i++) h.runTimers(1000);
    assert.equal(h.requests.filter(r => r.url === url).length, 1);
    assert.equal(h.timerCount(1000), 0);
    h.runTimers(h.api.REQUEST_TIMEOUT_MS);
    await polling;
    assert.equal(h.timerCount(1000), 1);
    if (name === 'uptime') assert.match(h.nodes.get('uptime').textContent, /Unavailable/);
    connected = true;
    h.runTimers(1000);
    await settle();
    assert.equal(h.requests.filter(r => r.url === url).length, 2);
    assert.equal(h.timerCount(1000), 1);
    if (name === 'uptime') assert.equal(h.nodes.get('uptime').textContent, '12345 ms');
    else assert.equal(h.nodes.get('clock-sync-status').textContent, '✅ Synced');
  });
}
