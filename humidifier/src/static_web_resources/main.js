// ==========================================
// 1. STATE & CONFIGURATION
// ==========================================
const state = {
  mode: 'manual',
  activePiezo: -1,
  piezos: [
    { brightness: 50, intensity: 50, frequencyHz: 1700000 },
    { brightness: 50, intensity: 50, frequencyHz: 1700000 },
    { brightness: 50, intensity: 50, frequencyHz: 1700000 }
  ],
  fan: 50,
  loaded: { piezo: false, fan: false },
  schedules: [],
  timeouts: { piezo: null, fan: null }
};
const writes = { piezo: Promise.resolve(), fan: Promise.resolve() };
const revisions = { piezo: 0, fan: 0 };
const writeGenerations = { piezo: 0, fan: 0 };
const REQUEST_TIMEOUT_MS = 5000;

function clamp(val) { 
  return Math.max(0, Math.min(100, parseInt(val, 10) || 0)); 
}

async function checkedFetch(url, options, format = 'text') {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    if (!response.ok) {
      const detail = (await response.text()).trim();
      throw new Error(detail || `HTTP ${response.status}`);
    }
    // Keep the deadline active while reading the body as well as the headers.
    return await response[format]();
  } catch (error) {
    if (controller.signal.aborted) {
      throw new Error('Request timed out. Check the device connection and try again.');
    }
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}

function showControlStatus(control, message, kind = '') {
  const el = document.getElementById(`${control}-status`);
  el.textContent = message;
  el.className = `status ${kind}`;
  document.getElementById(`${control}-retry`).classList.toggle('hidden', kind !== 'error');
}

function renderPiezoControls() {
  document.querySelectorAll('input[name="active-piezo"]').forEach(r => {
    r.checked = Number(r.value) === state.activePiezo;
    r.disabled = !state.loaded.piezo;
  });
  const piezo = state.piezos[state.activePiezo === -1 ? 0 : state.activePiezo];
  for (const [id, value] of [
    ['m_br', piezo.brightness],
    ['m_int', piezo.intensity],
    ['m_freq', (piezo.frequencyHz / 1000000).toFixed(3)]
  ]) {
    document.getElementById(id).value = value;
    document.getElementById(`${id}_in`).value = value;
  }
  document.querySelectorAll('#manual-controls input').forEach(i => {
    i.disabled = !state.loaded.piezo || state.activePiezo === -1;
  });
}

function renderAppliedPiezo(data) {
  const el = document.getElementById('piezo-applied');
  if (!Number.isFinite(data.actualFrequencyHz) || !Number.isFinite(data.actualDutyBasisPoints)) {
    el.textContent = 'Applied settings are unavailable.';
    return;
  }
  if (data.actualFrequencyHz === 0) {
    el.textContent = 'All piezos are off.';
    return;
  }
  const frequency = (data.actualFrequencyHz / 1000000).toFixed(6);
  const duty = (data.actualDutyBasisPoints / 100).toFixed(2);
  const constant = data.actualDutyBasisPoints === 0 || data.actualDutyBasisPoints === 10000;
  const active = [0, 1, 2].includes(data.active) ? `Piezo ${data.active} · ` : '';
  el.textContent = constant
    ? `${active}Configured frequency: ${frequency} MHz. Output is constant ${data.actualDutyBasisPoints === 0 ? 'low' : 'high'} (${duty}% duty cycle).`
    : `${active}Applied frequency: ${frequency} MHz · PWM duty cycle: ${duty}%.`;
}

// ==========================================
// 2. SYNC LOGIC
// ==========================================
function scheduleWrite(control, url, payload, immediate = false) {
  clearTimeout(state.timeouts[control]);
  const revision = ++revisions[control];
  const generation = writeGenerations[control];
  showControlStatus(control, 'Applying settings...');
  if (control === 'piezo') document.getElementById('piezo-applied').textContent = 'Awaiting confirmation from the device.';
  const submit = () => {
    // Capture each command and serialize requests so an older reply cannot
    // leave the device on a previously selected piezo after Off is selected.
    writes[control] = writes[control].then(async () => {
      if (generation !== writeGenerations[control]) return;
      try {
        const applied = await checkedFetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        }, control === 'piezo' ? 'json' : 'text');
        if (revision === revisions[control]) {
          if (control === 'piezo') renderAppliedPiezo(applied);
          showControlStatus(control, 'Settings applied.', 'success');
        }
      } catch (e) {
        // The device may have applied a command whose response was lost. Drop
        // the queued backlog and keep the latest desired state for explicit Retry.
        ++writeGenerations[control];
        clearTimeout(state.timeouts[control]);
        const failedRevision = revisions[control];
        if (control === 'piezo') document.getElementById('piezo-applied').textContent = 'Applied settings are unconfirmed.';
        showControlStatus(control, `Settings request failed: ${e.message}`, 'error');
        if (control === 'piezo') {
          try {
            const confirmed = await checkedFetch('/piezos', undefined, 'json');
            if (failedRevision === revisions[control]) renderAppliedPiezo(confirmed);
          } catch (_) {
            // Keep the unconfirmed message if the device is unreachable.
          }
        }
      }
    });
  };
  if (immediate) submit();
  else state.timeouts[control] = setTimeout(submit, 250);
}

function syncPiezo(immediate = false) {
  if (!state.loaded.piezo) return;
  const payload = state.activePiezo === -1
    ? { piezoNum: -1 }
    : { piezoNum: state.activePiezo, ...state.piezos[state.activePiezo] };
  scheduleWrite('piezo', '/piezos', payload, immediate);
}

function syncFan(immediate = false) {
  if (!state.loaded.fan) return;
  scheduleWrite('fan', '/fan', { speed: state.fan }, immediate);
}

// ==========================================
// 3. MODE & MANUAL PANEL BINDINGS
// ==========================================
function bindModeSelector() {
  const sel = document.getElementById('system-mode');
  const mPanel = document.getElementById('manual-panel');
  const sPanel = document.getElementById('schedule-panel');
  const fanPanel = document.getElementById('global-fan-panel');
  
  sel.addEventListener('change', e => {
    state.mode = e.target.value;
    mPanel.classList.toggle('hidden', state.mode !== 'manual');
    sPanel.classList.toggle('hidden', state.mode !== 'schedule');
    fanPanel.classList.toggle('hidden', state.mode === 'schedule');
  });
}

function bindPiezoBullets() {
  document.querySelectorAll('input[name="active-piezo"]').forEach(r => {
    r.addEventListener('change', e => {
      if (!state.loaded.piezo) return;
      state.activePiezo = parseInt(e.target.value);
      renderPiezoControls();
      syncPiezo(true);
    });
  });
  document.getElementById('piezo-retry').onclick = () => {
    if (state.loaded.piezo) syncPiezo(true);
    else loadPiezos();
  };
}

function bindManualSliders() {
  const pair = (sId, iId, ch, normalize = clamp, format = v => v) => {
    const s = document.getElementById(sId), i = document.getElementById(iId);
    if (!s || !i) return;
    const up = v => {
      if (!state.loaded.piezo || state.activePiezo === -1) return;
      if (v === '' || !Number.isFinite(Number(v))) {
        renderPiezoControls();
        return;
      }
      const c = normalize(v);
      s.value = format(c); i.value = format(c);
      state.piezos[state.activePiezo][ch] = c;
      syncPiezo();
    };
    s.oninput = e => up(e.target.value);
    i.onchange = e => up(e.target.value);
    i.onkeydown = e => { if(e.key==='Enter') i.blur(); };
  };
  pair('m_br', 'm_br_in', 'brightness');
  pair('m_int', 'm_int_in', 'intensity');
  pair('m_freq', 'm_freq_in', 'frequencyHz',
    v => Math.max(1600000, Math.min(1800000, Math.round(Number(v) * 1000) * 1000)),
    v => (v / 1000000).toFixed(3));
}

function bindGlobalFan() {
  const s = document.getElementById('fan_sl'), i = document.getElementById('fan_in');
  const up = value => {
    if (!state.loaded.fan) return;
    state.fan = clamp(value);
    s.value = state.fan; i.value = state.fan;
    syncFan();
  };
  s.oninput = () => up(s.value);
  i.onchange = () => up(i.value);
  document.getElementById('fan-retry').onclick = () => {
    if (state.loaded.fan) syncFan(true);
    else loadFan();
  };
}

// ==========================================
// 4. SCHEDULING MODULE (FIXED)
// ==========================================
function timeToMin(t) { 
  if (!t) return 0; 
  const [h, m] = t.split(':').map(Number); 
  return h * 60 + m; 
}

// Check if End Time > Start Time
function isTimeValid(start, end) {
  if (!start || !end) return false;
  return timeToMin(end) > timeToMin(start);
}

function hasScheduleOverlap(schedA, schedB) {
  // Empty days = applies to next day (single occurrence)
  const isNextDayA = schedA.days.length === 0;
  const isNextDayB = schedB.days.length === 0;

  // Case 1: One is "next day", other is "specific days" -> Different days -> NO OVERLAP
  if (isNextDayA !== isNextDayB) return false;

  // Case 2: Both are "next day" -> Only overlap if time ranges intersect
  if (isNextDayA && isNextDayB) {
    const sA = timeToMin(schedA.start), eA = timeToMin(schedA.end);
    const sB = timeToMin(schedB.start), eB = timeToMin(schedB.end);
    return (sA < eB && sB < eA) && (sA < eA && sB < eB);
  }

  // Case 3: Both have specific days -> Check day intersection first
  const sharesDay = schedA.days.some(d => schedB.days.includes(d));
  if (!sharesDay) return false;

  // Check time intersection
  const sA = timeToMin(schedA.start), eA = timeToMin(schedA.end);
  const sB = timeToMin(schedB.start), eB = timeToMin(schedB.end);
  if (sA >= eA || sB >= eB) return false; // Invalid ranges don't trigger conflicts
  return sA < eB && sB < eA;
}

function getScheduleErrors(sched, excludeId) {
  const errors = [];
  
  // 1. Time Validity Check
  if (!isTimeValid(sched.start, sched.end)) {
    errors.push("End time must be after Start time");
  }

  // 2. Overlap Check
  const overlap = state.schedules.some(s => 
    s.id !== excludeId && hasScheduleOverlap(sched, s)
  );
  if (overlap) {
    errors.push("Time overlaps with another schedule");
  }
  
  return errors;
}

function renderSchedules() {
  const con = document.getElementById('schedule-container');
  if (!con) return;
  
  con.innerHTML = state.schedules.map(s => {
    const days = ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
    const errors = getScheduleErrors(s, s.id);
    const hasErrors = errors.length > 0;
    
    return `
      <div class="schedule-card ${hasErrors ? 'invalid' : ''}" data-id="${s.id}">
        <button class="delete-btn" data-id="${s.id}" title="Delete">✕</button>
        
        <div class="sched-row">
          <label>Start <input type="time" class="s-st" value="${s.start}"></label>
          <label>End <input type="time" class="s-en" value="${s.end}"></label>
        </div>
        
        <div class="days-row">
          ${days.map((d, i) => `
            <label class="day-chip">
              <input type="checkbox" data-day="${i}" ${s.days.includes(i) ? 'checked' : ''}>${d}
            </label>`).join('')}
        </div>
        
        <div class="piezo-row">
          <span>Piezo:</span>
          <label><input type="radio" name="sp_${s.id}" value="0" ${s.piezo===0?'checked':''}><span class="bullet">●</span>P0</label>
          <label><input type="radio" name="sp_${s.id}" value="1" ${s.piezo===1?'checked':''}><span class="bullet">●</span>P1</label>
          <label><input type="radio" name="sp_${s.id}" value="2" ${s.piezo===2?'checked':''}><span class="bullet">●</span>P2</label>
        </div>
        
        <div class="controls-card">
          <div class="slider-group">
            <label>Brightness</label>
            <div class="slider-row">
              <input type="range" class="s-br-s" min="0" max="100" value="${s.brightness}">
              <input type="number" class="s-br-i" min="0" max="100" value="${s.brightness}">
            </div>
          </div>
          <div class="slider-group">
            <label>Intensity</label>
            <div class="slider-row">
              <input type="range" class="s-int-s" min="0" max="100" value="${s.intensity}">
              <input type="number" class="s-int-i" min="0" max="100" value="${s.intensity}">
            </div>
          </div>
          <div class="slider-group">
            <label>Fan Speed</label>
            <div class="slider-row">
              <input type="range" class="s-fan-s" min="0" max="100" value="${s.fanSpeed}">
              <input type="number" class="s-fan-i" min="0" max="100" value="${s.fanSpeed}">
            </div>
          </div>
        </div>
        
        <button class="btn save-btn" data-id="${s.id}" ${hasErrors ? 'disabled' : ''}>💾 Save Schedule</button>
        ${hasErrors ? `<div class="status error">${errors.join(', ')}</div>` : ''}
      </div>`;
  }).join('');
  
  bindScheduleCards();
}

function bindScheduleCards() {
  document.querySelectorAll('.delete-btn').forEach(b => 
    b.onclick = () => {
      state.schedules = state.schedules.filter(s => s.id !== parseInt(b.dataset.id));
      renderSchedules();
    }
  );

  document.querySelectorAll('.schedule-card').forEach(card => {
    const id = parseInt(card.dataset.id);
    const sch = state.schedules.find(s => s.id === id);
    if (!sch) return;

    // Time Inputs: Update state immediately on change
    card.querySelector('.s-st').onchange = e => { 
      sch.start = e.target.value; 
      renderSchedules(); // Re-render to show/hide errors
    };
    card.querySelector('.s-en').onchange = e => { 
      sch.end = e.target.value; 
      renderSchedules(); 
    };
    
    // Day checkboxes
    card.querySelectorAll('input[data-day]').forEach(cb => 
      cb.onchange = e => {
        const d = parseInt(e.target.dataset.day);
        sch.days = e.target.checked 
          ? [...new Set([...sch.days, d])] 
          : sch.days.filter(x => x !== d);
        renderSchedules();
      }
    );
    
    // Piezo radios
    card.querySelectorAll('input[type="radio"]').forEach(r => 
      r.onchange = e => { sch.piezo = parseInt(e.target.value); renderSchedules(); }
    );

    // Sliders
    const bindSl = (clsS, clsI, key) => {
      const s = card.querySelector(clsS), i = card.querySelector(clsI);
      s.oninput = () => { i.value = s.value; sch[key] = clamp(s.value); };
      i.onchange = () => { s.value = i.value; sch[key] = clamp(i.value); };
    };
    bindSl('.s-br-s', '.s-br-i', 'brightness');
    bindSl('.s-int-s', '.s-int-i', 'intensity');
    bindSl('.s-fan-s', '.s-fan-i', 'fanSpeed');

    // Save Button
    card.querySelector('.save-btn').onclick = async () => {
      const btn = card.querySelector('.save-btn');
      const errors = getScheduleErrors(sch, sch.id);
      
      if (errors.length > 0) {
        alert('Cannot save:\n' + errors.join('\n'));
        return;
      }
      
      btn.disabled = true; 
      btn.textContent = 'Saving...';
      try {
        await checkedFetch('/schedules', {
          method: 'POST', 
          headers: { 'Content-Type': 'application/json' }, 
          body: JSON.stringify({ schedule: sch }) 
        });
        alert('✅ Schedule saved!');
      } catch(e) { 
        alert('❌ Save failed: ' + e.message); 
      }
      finally { 
        btn.disabled = false; 
        btn.textContent = '💾 Save Schedule'; 
      }
    };
  });
}

document.getElementById('add-schedule-btn').onclick = () => {
  state.schedules.push({ 
    id: Date.now(), 
    start: '08:00', 
    end: '09:00', 
    days: [], 
    piezo: 0, 
    brightness: 50, 
    intensity: 50, 
    fanSpeed: 50 
  });
  renderSchedules();
};

// ==========================================
// 5. NETWORK, CLOCK & UPTIME
// ==========================================
document.getElementById('net_save').onclick = async (e) => {
  e.preventDefault();
  const btn = e.target, status = document.getElementById('net_status');
  const ssid = document.getElementById('net_ssid').value.trim();
  const pass = document.getElementById('net_pass').value;
  if (!ssid) { status.textContent = 'SSID required'; status.className = 'status error'; return; }
  
  btn.disabled = true; btn.textContent = 'Saving...';
  try {
    await checkedFetch('/credentials', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ssid, password:pass}) });
    status.textContent = '✅ Saved.'; status.className = 'status success';
    document.getElementById('net_pass').value = '';
  } catch(err) { status.textContent = ` ${err.message}`; status.className = 'status error'; }
  finally { btn.disabled = false; btn.textContent = 'Save Credentials'; }
};

document.getElementById('pass_toggle').onclick = () => {
  const p = document.getElementById('net_pass');
  p.type = p.type === 'password' ? 'text' : 'password';
  document.getElementById('pass_toggle').textContent = p.type === 'password' ? '👁️' : '🙈';
};

function isPercent(value) {
  return Number.isInteger(value) && value >= 0 && value <= 100;
}

async function loadPiezos() {
  showControlStatus('piezo', 'Loading piezo settings...');
  try {
    const d = await checkedFetch('/piezos', undefined, 'json');
    if (![-1, 0, 1, 2].includes(d.active) || !Array.isArray(d.piezos) || d.piezos.length !== 3) {
      throw new Error('Invalid piezo settings received');
    }
    const piezos = d.piezos.map(p => {
      const frequencyHz = p.frequencyHz ?? 1700000;
      if (!isPercent(p.brightness) || !isPercent(p.intensity) ||
          !Number.isInteger(frequencyHz) || frequencyHz < 1600000 || frequencyHz > 1800000) {
        throw new Error('Invalid piezo settings received');
      }
      return { brightness: p.brightness, intensity: p.intensity, frequencyHz };
    });
    state.piezos = piezos;
    state.activePiezo = d.active;
    state.loaded.piezo = true;
    renderPiezoControls();
    renderAppliedPiezo(d);
    showControlStatus('piezo', 'Settings loaded.');
  } catch (e) {
    showControlStatus('piezo', `Could not load piezo settings: ${e.message}`, 'error');
  }
}

async function loadFan() {
  showControlStatus('fan', 'Loading fan settings...');
  try {
    const d = await checkedFetch('/fan', undefined, 'json');
    if (!isPercent(d.speed)) throw new Error('Invalid fan speed received');
    state.fan = d.speed;
    state.loaded.fan = true;
    document.getElementById('fan_sl').value = state.fan; document.getElementById('fan_in').value = state.fan;
    document.getElementById('fan_sl').disabled = false;
    document.getElementById('fan_in').disabled = false;
    showControlStatus('fan', 'Settings loaded.');
  } catch (e) {
    showControlStatus('fan', `Could not load fan settings: ${e.message}`, 'error');
  }
}

async function loadCredentials() {
  try {
    const credentials = await checkedFetch('/credentials', undefined, 'json');
    document.getElementById('net_ssid').value = credentials.ssid || '';
  } catch (e) {
    const status = document.getElementById('net_status');
    status.textContent = `Could not load credentials: ${e.message}`;
    status.className = 'status error';
  }
}

async function fetchInitialData() {
  await Promise.all([loadPiezos(), loadFan(), loadCredentials()]);
}

const clk = { ts:0, sync:false, last:0 };
async function updateClock() {
  try {
    const d = await checkedFetch('/time', undefined, 'json');
    clk.ts = d.timestamp; clk.sync = d.synced; clk.last = Math.floor(Date.now()/1000);
  } catch(e) { clk.sync = false; }
  const now = new Date((clk.ts + Math.floor((Date.now()/1000)-clk.last))*1000);
  document.getElementById('clock-time').textContent = now.toLocaleTimeString('en-GB');
  document.getElementById('clock-date').textContent = now.toLocaleDateString('en-GB');
  const el = document.getElementById('clock-sync-status');
  el.textContent = clk.sync?'✅ Synced':' Offline';
  el.className = `sync-status ${clk.sync?'synced':'offline'}`;
  setTimeout(updateClock, 1000);
}

async function updateUptime() {
  try {
    const uptime = await checkedFetch('/uptime');
    document.getElementById('uptime').textContent = `${uptime} ms`;
  } catch (e) {
    document.getElementById('uptime').textContent = 'Unavailable — device not responding';
  }
  setTimeout(updateUptime, 1000);
}

// ==========================================
// 6. INITIALIZATION
// ==========================================
document.addEventListener('DOMContentLoaded', () => {
  bindModeSelector();
  bindPiezoBullets();
  bindManualSliders();
  bindGlobalFan();
  renderPiezoControls();
  fetchInitialData();
  renderSchedules(); // Initial render of schedules
  updateClock();
  updateUptime();
});
