// ==========================================
// STATE & CONFIG
// ==========================================
const state = {
  mode: 'manual',
  activePiezo: 0,
  piezos: [
    { brightness: 50, intensity: 50 },
    { brightness: 50, intensity: 50 },
    { brightness: 50, intensity: 50 }
  ],
  fan: 50,
  schedules: [],
  timeouts: { piezo: null, fan: null }
};

function clamp(val) { return Math.max(0, Math.min(100, parseInt(val, 10) || 0)); }

// ==========================================
// SYNC LOGIC
// ==========================================
function syncPiezo() {
  clearTimeout(state.timeouts.piezo);
  state.timeouts.piezo = setTimeout(async () => {
    if (state.activePiezo === -1) return;
    const p = state.piezos[state.activePiezo];
    try {
      await fetch('/piezos', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ piezoNum: state.activePiezo, brightness: p.brightness, intensity: p.intensity })
      });
    } catch (e) { console.warn('Piezo sync failed:', e.message); }
  }, 250);
}

function syncFan() {
  clearTimeout(state.timeouts.fan);
  state.timeouts.fan = setTimeout(async () => {
    try {
      await fetch('/fan', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ speed: state.fan }) });
    } catch (e) { console.warn('Fan sync failed:', e.message); }
  }, 250);
}

// ==========================================
// MANUAL MODE
// ==========================================
function bindModeSelector() {
  const sel = document.getElementById('system-mode');
  const mPanel = document.getElementById('manual-panel');
  const sPanel = document.getElementById('schedule-panel');
  sel.addEventListener('change', e => {
    state.mode = e.target.value;
    mPanel.classList.toggle('hidden', state.mode !== 'manual');
    sPanel.classList.toggle('hidden', state.mode !== 'schedule');
  });
}

function bindPiezoBullets() {
  document.querySelectorAll('input[name="active-piezo"]').forEach(r => {
    r.checked = parseInt(r.value) === state.activePiezo;
    r.addEventListener('change', e => {
      state.activePiezo = parseInt(e.target.value);
      const idx = state.activePiezo === -1 ? 0 : state.activePiezo;
      document.getElementById('m_br').value = state.piezos[idx].brightness;
      document.getElementById('m_br_in').value = state.piezos[idx].brightness;
      document.getElementById('m_int').value = state.piezos[idx].intensity;
      document.getElementById('m_int_in').value = state.piezos[idx].intensity;
      document.querySelectorAll('#manual-controls input').forEach(i => i.disabled = state.activePiezo === -1);
      syncPiezo();
    });
  });
}

function bindManualSliders() {
  const pair = (sId, iId, ch) => {
    const s = document.getElementById(sId), i = document.getElementById(iId);
    if (!s || !i) return;
    const up = v => { 
      const c = clamp(v); s.value = c; i.value = c; 
      state.piezos[state.activePiezo === -1 ? 0 : state.activePiezo][ch] = c; 
      if (state.activePiezo !== -1) syncPiezo(); 
    };
    s.oninput = e => up(e.target.value);
    i.oninput = e => up(e.target.value);
    i.onkeydown = e => { if(e.key==='Enter') i.blur(); };
  };
  pair('m_br', 'm_br_in', 'brightness');
  pair('m_int', 'm_int_in', 'intensity');
}

function bindFan() {
  const s = document.getElementById('fan_sl'), i = document.getElementById('fan_in');
  s.oninput = () => { i.value = s.value; state.fan = clamp(s.value); syncFan(); };
  i.onchange = () => { s.value = i.value; state.fan = clamp(i.value); syncFan(); };
}

// ==========================================
// SCHEDULING MODULE
// ==========================================
function timeToMin(t) { if (!t) return 0; const [h,m] = t.split(':').map(Number); return h*60+m; }

// DAY-AWARE OVERLAP DETECTION
function hasScheduleOverlap(schedA, schedB) {
  // 1. Check if they share ANY common day
  const sharesDay = schedA.days.some(d => schedB.days.includes(d));
  if (!sharesDay) return false; // Different days = never overlaps

  // 2. If days overlap, check time intersection
  const sA = timeToMin(schedA.start), eA = timeToMin(schedA.end);
  const sB = timeToMin(schedB.start), eB = timeToMin(schedB.end);

  if (sA >= eA || sB >= eB) return false; // Invalid time ranges don't trigger overlap warnings
  return sA < eB && sB < eA; // Standard interval overlap check
}

function checkOverlap(target, excludeId) {
  return state.schedules.some(s => s.id !== excludeId && hasScheduleOverlap(target, s));
}

function renderSchedules() {
  const con = document.getElementById('schedule-container');
  if (!con) return;
  con.innerHTML = state.schedules.map(s => {
    const days = ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
    const inv = checkOverlap(s, s.id);
    return `
      <div class="schedule-card ${inv?'invalid':''}" data-id="${s.id}">
        <button class="delete-btn" data-id="${s.id}">✕</button>
        <div class="sched-row">
          <label>Start <input type="time" class="s-st" value="${s.start}"></label>
          <label>End <input type="time" class="s-en" value="${s.end}"></label>
        </div>
        <div class="days-row">${days.map((d,i)=>`<label class="day-chip"><input type="checkbox" data-day="${i}" ${s.days.includes(i)?'checked':''}>${d}</label>`).join('')}</div>
        <div class="piezo-row">
          <span>Piezo:</span>
          <label><input type="radio" name="sp_${s.id}" value="0" ${s.piezo===0?'checked':''}><span class="bullet">●</span>P0</label>
          <label><input type="radio" name="sp_${s.id}" value="1" ${s.piezo===1?'checked':''}><span class="bullet">●</span>P1</label>
          <label><input type="radio" name="sp_${s.id}" value="2" ${s.piezo===2?'checked':''}><span class="bullet">●</span>P2</label>
        </div>
        <div class="controls-card">
          <div class="slider-group"><label>Brightness</label><div class="slider-row"><input type="range" class="s-br-s" min="0" max="100" value="${s.brightness}"><input type="number" class="s-br-i" min="0" max="100" value="${s.brightness}"></div></div>
          <div class="slider-group"><label>Intensity</label><div class="slider-row"><input type="range" class="s-int-s" min="0" max="100" value="${s.intensity}"><input type="number" class="s-int-i" min="0" max="100" value="${s.intensity}"></div></div>
        </div>
        <button class="btn save-btn" data-id="${s.id}" ${inv?'disabled':''}>💾 Save Schedule</button>
        <div class="status hidden" data-id="${s.id}"></div>
      </div>`;
  }).join('');
  bindScheduleCards();
}

function bindScheduleCards() {
  document.querySelectorAll('.delete-btn').forEach(b => b.onclick = () => {
    state.schedules = state.schedules.filter(s => s.id !== parseInt(b.dataset.id));
    renderSchedules();
  });

  document.querySelectorAll('.schedule-card').forEach(card => {
    const id = parseInt(card.dataset.id);
    const sch = state.schedules.find(s => s.id === id);
    if (!sch) return;

    const setStatus = (msg, type) => {
      const el = card.querySelector('.status');
      el.textContent = msg; el.className = `status ${type}`;
    };

    card.querySelector('.s-st').onchange = e => { sch.start = e.target.value; refreshCardUI(card); };
    card.querySelector('.s-en').onchange = e => { sch.end = e.target.value; refreshCardUI(card); };
    card.querySelectorAll('input[data-day]').forEach(cb => cb.onchange = e => {
      const d = parseInt(e.target.dataset.day);
      sch.days = e.target.checked ? [...new Set([...sch.days, d])] : sch.days.filter(x => x!==d);
      refreshCardUI(card);
    });
    card.querySelectorAll('input[type="radio"]').forEach(r => r.onchange = e => { sch.piezo = parseInt(e.target.value); refreshCardUI(card); });

    const bindSl = (clsS, clsI, key) => {
      const s = card.querySelector(clsS), i = card.querySelector(clsI);
      s.oninput = () => { i.value = s.value; sch[key] = clamp(s.value); refreshCardUI(card); };
      i.onchange = () => { s.value = i.value; sch[key] = clamp(i.value); refreshCardUI(card); };
    };
    bindSl('.s-br-s', '.s-br-i', 'brightness');
    bindSl('.s-int-s', '.s-int-i', 'intensity');

    card.querySelector('.save-btn').onclick = async () => {
      const btn = card.querySelector('.save-btn');
      if (checkOverlap(sch, sch.id)) {
        setStatus('⚠️ Time overlaps on selected days.', 'error');
        return;
      }
      btn.disabled = true; btn.textContent = 'Saving...';
      try {
        await fetch('/schedules', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({schedule:sch}) });
        setStatus('✅ Saved successfully!', 'success');
        setTimeout(()=>setStatus('','hidden'), 2000);
      } catch(e) { setStatus(`❌ ${e.message}`, 'error'); }
      finally { btn.disabled = false; btn.textContent = '💾 Save Schedule'; }
    };
  });
}

function refreshCardUI(card) {
  const id = parseInt(card.dataset.id);
  const sch = state.schedules.find(s => s.id === id);
  const inv = checkOverlap(sch, sch.id);
  card.classList.toggle('invalid', inv);
  card.querySelector('.save-btn').disabled = inv;
}

document.getElementById('add-schedule-btn').onclick = () => {
  state.schedules.push({ id: Date.now(), start:'08:00', end:'09:00', days:[], piezo:0, brightness:50, intensity:50 });
  renderSchedules();
};

// ==========================================
// NETWORK & SYSTEM FEATURES
// ==========================================
document.getElementById('net_save').onclick = async (e) => {
  e.preventDefault();
  const btn = e.target, status = document.getElementById('net_status');
  const ssid = document.getElementById('net_ssid').value.trim();
  const pass = document.getElementById('net_pass').value;
  if (!ssid) { status.textContent = 'SSID required'; status.className = 'status error'; return; }
  btn.disabled = true; btn.textContent = 'Saving...';
  try {
    await fetch('/credentials', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ssid, password:pass}) });
    status.textContent = '✅ Saved.'; status.className = 'status success';
    document.getElementById('net_pass').value = '';
  } catch(err) { status.textContent = `❌ ${err.message}`; status.className = 'status error'; }
  finally { btn.disabled = false; btn.textContent = 'Save Credentials'; }
};

document.getElementById('pass_toggle').onclick = () => {
  const p = document.getElementById('net_pass');
  p.type = p.type === 'password' ? 'text' : 'password';
  document.getElementById('pass_toggle').textContent = p.type === 'password' ? '👁️' : '🙈';
};

// ==========================================
// FETCH & INIT
// ==========================================
async function fetchInitialData() {
  try {
    const p = await fetch('/piezos');
    if (p.ok) {
      const d = await p.json();
      if (d.active !== undefined) state.activePiezo = d.active;
      d.piezos?.forEach((v,i) => { if(i<3){ state.piezos[i].brightness = v.brightness||50; state.piezos[i].intensity = v.intensity||50; }});
    }
    const f = await fetch('/fan');
    if (f.ok) state.fan = (await f.json()).speed || 50;
    
    document.querySelector(`input[name="active-piezo"][value="${state.activePiezo}"]`).checked = true;
    const idx = state.activePiezo === -1 ? 0 : state.activePiezo;
    document.getElementById('m_br').value = state.piezos[idx].brightness; document.getElementById('m_br_in').value = state.piezos[idx].brightness;
    document.getElementById('m_int').value = state.piezos[idx].intensity; document.getElementById('m_int_in').value = state.piezos[idx].intensity;
    document.getElementById('fan_sl').value = state.fan; document.getElementById('fan_in').value = state.fan;
    document.querySelectorAll('#manual-controls input').forEach(i => i.disabled = state.activePiezo === -1);

    const c = await fetch('/credentials');
    if (c.ok) document.getElementById('net_ssid').value = (await c.json()).ssid || '';
  } catch(e) { console.warn('Init fetch failed:', e.message); }
}

const clk = { ts:0, sync:false, last:0 };
async function updateClock() {
  try {
    const r = await fetch('/time'); if(r.ok){const d=await r.json(); clk.ts=d.timestamp; clk.sync=d.synced; clk.last=Math.floor(Date.now()/1000);}
  } catch(e){}
  const now = new Date((clk.ts + Math.floor((Date.now()/1000)-clk.last))*1000);
  document.getElementById('clock-time').textContent = now.toLocaleTimeString('en-GB');
  document.getElementById('clock-date').textContent = now.toLocaleDateString('en-GB');
  const el = document.getElementById('clock-sync-status');
  el.textContent = clk.sync?'✅ Synced':'⏳ Offline';
  el.className = `sync-status ${clk.sync?'synced':'offline'}`;
}

setInterval(async()=>{ try{const r=await fetch('/uptime'); if(r.ok)document.getElementById('uptime').textContent=`${await r.text()} ms`;}catch(e){}},1000);

document.addEventListener('DOMContentLoaded', () => {
  bindModeSelector();
  bindPiezoBullets();
  bindManualSliders();
  bindFan();
  fetchInitialData();
  renderSchedules();
  updateClock();
  setInterval(updateClock, 1000);
});