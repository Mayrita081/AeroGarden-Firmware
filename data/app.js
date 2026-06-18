// ── NAVIGATION ──
function showPage(name, btn) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
  document.getElementById('page-' + name).classList.add('active');
  btn.classList.add('active');
}

// ── SENSOR GROUP TOGGLE ──
function toggleGroup(id) {
  document.getElementById(id).classList.toggle('open');
}

// ── TAB SWITCHING ──
function switchTab(name) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
  document.getElementById('tab-' + name).classList.add('active');
  event.currentTarget.classList.add('active');
}

// ── SLIDER PREVIEW ──
function updateSliderPreview(slider, color) {
  const pct = (slider.value / 255) * 100;
  if (color === 'red') {
    slider.style.background =
      `linear-gradient(90deg, #1e6e14 0%, #ff6b6b ${pct}%, #e0e0e0 ${pct}%)`;
  } else {
    slider.style.background =
      `linear-gradient(90deg, #1e6e14 0%, #6b9fff ${pct}%, #e0e0e0 ${pct}%)`;
  }
}

// ── SAVE LEDs ──
function saveLEDs() {
  const red  = document.getElementById('slider-red-0').value;
  const blue = document.getElementById('slider-blue-0').value;
  fetch('/api/leds', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ red: parseInt(red), blue: parseInt(blue) })
  })
  .then(r => r.ok ? showToast('LEDs saved') : showToast('Error saving LEDs'))
  .catch(() => showToast('Error – check connection'));
}

// ── PUMP CONTROL ──
function togglePump(checkbox) {
  const isOn = checkbox.checked;
  const schedule = document.getElementById('pump-schedule');

  fetch('/api/pump', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ state: isOn ? 1 : 0 })
  })
  .then(r => {
    if (r.ok) {
      showToast(isOn ? 'Pump ON' : 'Pump OFF');
      if (!isOn) schedule.classList.remove('hidden');
      else schedule.classList.add('hidden');
    } else {
      showToast('Error toggling pump');
      checkbox.checked = !isOn; // revert
    }
  })
  .catch(() => {
    showToast('Error – check connection');
    checkbox.checked = !isOn;
  });
}

function cancelSchedule() {
  document.getElementById('pump-schedule').classList.add('hidden');
}

function confirmSchedule() {
  const h = parseInt(document.getElementById('sched-hours').value) || 0;
  const m = parseInt(document.getElementById('sched-mins').value) || 0;
  const totalMins = h * 60 + m;
  fetch('/api/pump/schedule', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ minutes: totalMins })
  })
  .then(r => r.ok ? showToast(`Schedule set: ${h}h ${m}m`) : showToast('Schedule error'))
  .catch(() => showToast('Error – check connection'));
  document.getElementById('pump-schedule').classList.add('hidden');
}

// ── SETTINGS ──
function saveSettings() {
  const floors  = parseInt(document.getElementById('setting-floors').value);
  const refresh = parseInt(document.getElementById('setting-refresh').value);
  fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ floors, refresh })
  })
  .then(r => r.ok ? showToast('Settings saved') : showToast('Error saving'))
  .catch(() => showToast('Error – check connection'));

  // Update local refresh rate
  restartPolling(refresh * 1000);
}

// ── SENSOR DATA POLLING ──
let pollInterval = null;

function fetchSensors() {
  fetch('/api/sensors')
    .then(r => r.json())
    .then(data => updateUI(data))
    .catch(() => {}); // silent fail – device may be busy
}

function updateUI(data) {
  // Light sensors (raw 0–4095 ADC → percentage)
  const lightPct = (data.light || [0,0,0,0]).map(v => Math.round((v / 4095) * 100));
  lightPct.forEach((pct, i) => {
    const bar = document.getElementById('bar-light-' + i);
    if (bar) bar.style.width = pct + '%';
  });
  const avgLight = lightPct.reduce((a,b) => a+b, 0) / lightPct.length;
  setBadge('badge-light', Math.round(avgLight) + '%');

  // Temperature
  const waterT   = data.waterTemp ?? null;
  const ambientT = data.ambientTemp ?? null;
  if (waterT !== null) {
    document.getElementById('bar-water-temp').style.width   = Math.min(100, Math.max(0, (waterT / 50) * 100)) + '%';
    document.getElementById('val-water-temp').textContent   = waterT.toFixed(1);
  }
  if (ambientT !== null) {
    document.getElementById('bar-ambient-temp').style.width = Math.min(100, Math.max(0, (ambientT / 50) * 100)) + '%';
    document.getElementById('val-ambient-temp').textContent = ambientT.toFixed(1);
    setBadge('badge-temp', ambientT.toFixed(1) + '°C');
  }

  // Humidity
  const hum = data.humidity ?? null;
  if (hum !== null) {
    document.getElementById('bar-humidity').style.width   = Math.min(100, hum) + '%';
    document.getElementById('val-humidity').textContent   = hum.toFixed(1);
    setBadge('badge-humidity', hum.toFixed(1) + '%');
  }

  // Pump state
  if (data.pump !== undefined) {
    document.getElementById('pump-toggle').checked = data.pump === 1;
  }

  // Status
  if (data.ip)     document.getElementById('status-ip').textContent     = data.ip;
  if (data.uptime) document.getElementById('status-uptime').textContent = formatUptime(data.uptime);
}

function setBadge(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h}h ${m}m ${s}s`;
}

function restartPolling(intervalMs) {
  if (pollInterval) clearInterval(pollInterval);
  pollInterval = setInterval(fetchSensors, intervalMs);
}

// ── TOAST ──
let toastTimer = null;
function showToast(msg) {
  let toast = document.getElementById('toast');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'toast';
    toast.className = 'toast';
    document.body.appendChild(toast);
  }
  toast.textContent = msg;
  toast.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('show'), 2500);
}

// ── INIT ──
document.addEventListener('DOMContentLoaded', () => {
  fetchSensors();
  restartPolling(5000);
});
