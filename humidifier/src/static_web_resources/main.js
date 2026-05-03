// ==========================================
// 1. DYNAMIC HTML GENERATION
// ==========================================
function createPiezoPanelHTML(piezoIndex) {
    const channels = [
        { name: 'brightness', class: 'brightness', label: 'Brightness' },
        { name: 'intensity', class: 'intensity', label: 'Piezo Power' }
    ];

    const rowsHTML = channels.map(channel => {
        const defaultValue = channel.name === 'intensity' ? 100 : 0;
        const idBase = `piezo${piezoIndex}_${channel.name}`;

        return `
        <div class="channel-row">
            <span class="channel-label ${channel.class}">${channel.label}:</span>
            <input type="range" id="${idBase}_slider" min="0" max="100" value="${defaultValue}"
                   data-piezo="${piezoIndex}" data-channel="${channel.name}">
            <div class="intensity-input-group">
                <input type="number" id="${idBase}_input" class="intensity-input" min="0" max="100" value="${defaultValue}"
                       data-piezo="${piezoIndex}" data-channel="${channel.name}">
                <span class="percent-sign">%</span>
            </div>
        </div>`;
    }).join('');

    return `
     <div class="piezo-panel">
        <div class="piezo-title">
            Piezo ${piezoIndex}
            <span class="piezo-preview" id="preview_${piezoIndex}"></span>
        </div>

        <!-- MODE (RADIO BUTTONS) -->
        <div class="piezo-mode">
            <label><strong>Mode:</strong></label>

            <label>
                <input type="radio" name="mode_${piezoIndex}" value="off" data-piezo="${piezoIndex}">
                Off
            </label>

            <label>
                <input type="radio" name="mode_${piezoIndex}" value="manual" data-piezo="${piezoIndex}" checked>
                Manual
            </label>

            <label>
                <input type="radio" name="mode_${piezoIndex}" value="schedule" data-piezo="${piezoIndex}">
                Scheduling
            </label>

            <!-- SCHEDULE INPUTS -->
            <div id="schedule_${piezoIndex}" style="margin-top:10px;">
                <label>Start:</label>
                <input
                    type="time"
                    step="60"
                    min="00:00"
                    max="23:59"
                    data-piezo="${piezoIndex}"
                    class="schedule-start">

                <label>End:</label>
                <input
                    type="time"
                    step="60"
                    min="00:00"
                    max="23:59"
                    data-piezo="${piezoIndex}"
                    class="schedule-end">
            </div>
        </div>

        ${rowsHTML}
    </div>`;
}

// ==========================================
// 2. STATE & CORE LOGIC
// ==========================================
const piezosState = {
    0: {  brightness: 0, intensity: 100, timeout: null },
    1: {  brightness: 0, intensity: 100, timeout: null },
    2: {  brightness: 0, intensity: 100, timeout: null }
};
const fanState = { speed: 0, timeout: null };

function clamp(val) {
    const num = parseInt(val, 10);
    return isNaN(num) ? 0 : Math.max(0, Math.min(100, num));
}

// --- Piezo ---
function updatePiezoState(piezoIndex, channel, value) {
    const state = piezosState[piezoIndex];
    if (!state) return;
    if (channel === 'intensity') state.intensity = value;
    if (channel === 'brightness') state.brightness = value;
    state.lastLocalUpdate = Date.now();
}

function scheduleSend(piezoIndex) {
    const state = piezosState[piezoIndex];
    if (!state) return;
    clearTimeout(state.timeout);
    state.timeout = setTimeout(() => postPiezo(piezoIndex), 300);
}

function updatePreview(piezoIndex) {
    const state = piezosState[piezoIndex];
    if (!state) return;

    const b = Math.round(state.brightness * 2.55);

    const preview = document.getElementById(`preview_${piezoIndex}`);
    if (preview) preview.style.background = `rgb(${b}, ${b}, ${b})`;
}

async function postPiezo(piezoIndex) {
    try {
        const res = await fetch("/piezos", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                piezoNum: parseInt(piezoIndex),
                brightness: piezosState[piezoIndex].brightness,
                intensity: piezosState[piezoIndex].intensity
            })
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
    } catch (e) { console.error(`Failed to update Piezo ${piezoIndex}:`, e.message); }
}

// --- Fan ---
function scheduleFanSend() {
    clearTimeout(fanState.timeout);
    fanState.timeout = setTimeout(postFan, 300);
}

async function postFan() {
    try {
        const res = await fetch("/fan", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ speed: fanState.speed })
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
    } catch (e) { console.error("Failed to update fan:", e.message); }
}

// --- Network ---
async function fetchNetworkCbrightnessentials() {
    try {
        const res = await fetch("/cbrightnessentials");
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        const ssidInput = document.getElementById("network-ssid");
        if (ssidInput && data.ssid) ssidInput.value = data.ssid;
        // Password intentionally left blank for security
    } catch (e) { console.warn("Could not fetch network cbrightnessentials:", e.message); }
}

async function saveNetworkCbrightnessentials() {
    const ssid = document.getElementById("network-ssid").value.trim();
    const password = document.getElementById("network-password").value;
    const statusEl = document.getElementById("network-status");
    const saveBtn = document.getElementById("network-save-btn");

    if (!ssid) {
        statusEl.textContent = "SSID cannot be empty.";
        statusEl.className = "status-msg error";
        return;
    }

    saveBtn.disabled = true;
    saveBtn.textContent = "Saving...";
    statusEl.textContent = "";

    try {
        const res = await fetch("/cbrightnessentials", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ ssid, password })
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        statusEl.textContent = "✅ Cbrightnessentials saved. Device may reboot to apply.";
        statusEl.className = "status-msg success";
        document.getElementById("network-password").value = "";
    } catch (e) {
        statusEl.textContent = `❌ Failed: ${e.message}`;
        statusEl.className = "status-msg error";
    } finally {
        saveBtn.disabled = false;
        saveBtn.textContent = "Save Cbrightnessentials";
    }
}

// ==========================================
// 3. EVENT BINDING
// ==========================================
function bindPiezoControls() {
    function handlePiezoChange(element, isFinal = false) {
        const piezoIdx = element.dataset.piezo;
        const channel = element.dataset.channel;
        const isSlider = element.type === 'range';
        let value = isSlider ? parseInt(element.value, 10) : parseFloat(element.value);
        if (isNaN(value)) value = 0;
        const clamped = clamp(value);

        const counterpartId = `piezo${piezoIdx}_${channel}_${isSlider ? 'input' : 'slider'}`;
        const counterpart = document.getElementById(counterpartId);
        if (counterpart) counterpart.value = clamped;

        updatePiezoState(piezoIdx, channel, clamped);
        updatePreview(piezoIdx);
        if (isSlider || isFinal) scheduleSend(piezoIdx);
    }

    document.querySelectorAll('input[type="range"][data-piezo]').forEach(slider => {
        slider.addEventListener('input', e => handlePiezoChange(e.target, false));
    });
    document.querySelectorAll('input[type="number"][data-piezo]').forEach(input => {
        input.addEventListener('input', e => handlePiezoChange(e.target, false));
        input.addEventListener('change', e => handlePiezoChange(e.target, true));
        input.addEventListener('keydown', e => { if (e.key === 'Enter') input.blur(); });
    });
}

function bindFanControls() {
    function handleFanChange(element, isFinal = false) {
        const isSlider = element.type === 'range';
        let value = isSlider ? parseInt(element.value, 10) : parseFloat(element.value);
        if (isNaN(value)) value = 0;
        const clamped = clamp(value);

        const counterpartId = `fan_speed_${isSlider ? 'input' : 'slider'}`;
        const counterpart = document.getElementById(counterpartId);
        if (counterpart) counterpart.value = clamped;

        fanState.speed = clamped;
        if (isSlider || isFinal) scheduleFanSend();
    }

    const fanSlider = document.getElementById('fan_speed_slider');
    const fanInput = document.getElementById('fan_speed_input');
    if (fanSlider) fanSlider.addEventListener('input', e => handleFanChange(e.target, false));
    if (fanInput) {
        fanInput.addEventListener('input', e => handleFanChange(e.target, false));
        fanInput.addEventListener('change', e => handleFanChange(e.target, true));
        fanInput.addEventListener('keydown', e => { if (e.key === 'Enter') fanInput.blur(); });
    }
}

function bindNetworkControls() {
    const saveBtn = document.getElementById("network-save-btn");
    if (saveBtn) saveBtn.addEventListener("click", saveNetworkCbrightnessentials);
}

// ==========================================
// 4. UPTIME FEATURE
// ==========================================

async function fetchFan() {
    try {
        const res = await fetch("/fan");
        if (!res.ok) throw new Error(`HTTP ${res.status}`);

        const json = await res.json();

        const slider = document.getElementById("fan_speed_slider");
        const input = document.getElementById("fan_speed_input");

        if(slider) slider.value = json.speed
        if(input) input.value = json.speed
    } catch (e) { console.error("fan speed fetch error:", e.message); }
}

async function fetchUptime() {
    try {
        const res = await fetch("/uptime");
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const json = await res.json();
        const el = document.getElementById("uptime");
        if (el) el.textContent = `Uptime: ${json} milliseconds`;
    } catch (e) { console.error("Uptime fetch error:", e.message); }
}
async function fetchPiezos() {
    try {
        const res = await fetch("/piezos");
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        console.log("json is: ", data);
        if (data.piezos && Array.isArray(data.piezos)) {
            data.piezos.forEach((p, idx) => {
                if (idx >= 3) return;
                const state = piezosState[idx];
                if (!state) return;

                const RECENT_CHANGE_WINDOW_MS = 2000;
                if (Date.now() - state.lastLocalUpdate < RECENT_CHANGE_WINDOW_MS) {
                    return; // Don't overwrite user's recent change
                }

                const b = p.brightness;
                const intensity = p.intensity;

                state.brightness = b;
                state.intensity = intensity;
                state.lastLocalUpdate = 0; 

                // Update UI sliders/inputs
                const setVal = (channel, val) => {
                    const slider = document.getElementById(`piezo${idx}_${channel}_slider`);
                    const input = document.getElementById(`piezo${idx}_${channel}_input`);
                    if (slider) slider.value = val;
                    if (input) input.value = val;
                };

                setVal('brightness', b);
                setVal('intensity', intensity);
                updatePreview(idx);
            });
        }
    } catch (e) { console.error("Piezos fetch error:", e.message); }
}
// ==========================================
// PASSWORD TOGGLE LOGIC
// ==========================================
function bindPasswordToggle() {
    const pwdInput = document.getElementById('network-password');
    const toggleBtn = document.getElementById('password-toggle');
    if (!pwdInput || !toggleBtn) return;

    toggleBtn.addEventListener('click', () => {
        const isHidden = pwdInput.type === 'password';
        pwdInput.type = isHidden ? 'text' : 'password';
        toggleBtn.textContent = isHidden ? '🙈' : '👁️';
        toggleBtn.title = isHidden ? 'Hide password' : 'Show password';
        toggleBtn.setAttribute('aria-label', isHidden ? 'Hide password' : 'Show password');
    });
}
// ==========================================
// 5. INITIALIZATION
// ==========================================
document.addEventListener("DOMContentLoaded", () => {
    // Generate Piezo panels
    const container = document.getElementById('piezo-panels-container');
    if (container) {
        let panelsHTML = '';
        for (let i = 0; i < 3; i++) {
            panelsHTML += createPiezoPanelHTML(i);
            if (!piezosState[i]) piezosState[i] = { brightness: 0, intensity: 100, timeout: null };
        }
        container.innerHTML = panelsHTML;
    }

    // Bind all controls
    bindPiezoControls();
    bindFanControls();
    bindNetworkControls();
    bindPasswordToggle();

    // Init previews
    [0, 1, 2].forEach(updatePreview);

    // Fetch initial data
    // fetchNetworkCbrightnessentials();
    fetchUptime();
    setInterval(fetchUptime, 1000);
    fetchFan();
    setInterval(fetchFan, 500);
    fetchPiezos();
    setInterval(fetchPiezos, 500);
});
