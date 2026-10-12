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
function switchTab(name, button) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
  document.getElementById('tab-' + name).classList.add('active');
  button.classList.add('active');
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


let busy = false, initialized = false, latestState = null, revision = 0;
let pollInterval = null, polling = false;
let setupMode = null, refreshMs = 5000;
const el = id => document.getElementById(id);
function number(id, min, max) {
  const input = el(id), value = Number(input.value);
  if (input.value.trim() === '' || !Number.isInteger(value) || value < min || value > max)
    throw new Error('Enter a whole number from ' + min + ' to ' + max);
  return value;
}
async function request(path, body) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch(path, {method: body ? 'POST' : 'GET', cache: 'no-store',
      headers: body ? {'Content-Type':'application/json'} : {},
      body: body ? JSON.stringify(body) : undefined, signal: controller.signal});
    const text = await response.text();
    let data;
    try { data = JSON.parse(text); } catch { throw new Error('Invalid device response (' + response.status + ')'); }
    if (!response.ok) throw new Error(data.error || 'Device error (' + response.status + ')');
    return data;
  } catch (error) {
    if (error.name === 'AbortError') throw new Error('Request timed out. Reconnecting to check device state.');
    throw error;
  } finally { clearTimeout(timeout); }
}
async function command(button, path, body, message) {
  if (busy) return false;
  busy = true; revision++;
  const controls = [...document.querySelectorAll('#page-control input, #page-control button, #page-settings input, #page-settings button, #setup-form input, #setup-form button')];
  const disabled = controls.map(control => control.disabled);
  controls.forEach(control => control.disabled = true);
  try {
    const data = await request(path, body);
    updateUI(data, true); showToast(message); return true;
  } catch (error) {
    showToast(error.message || 'Check your connection'); return false;
  } finally {
    busy = false;
    controls.forEach((control, i) => control.disabled = disabled[i]);
    if (latestState) renderWiFiMode(latestState);
    fetchSensors();
  }
}
async function saveLEDs(button) {
  try {
    await command(button, '/api/leds', {red: number('slider-red-0',0,255), blue: number('slider-blue-0',0,255),
      autoLight: el('auto-light').checked, lightTarget: number('light-target',1,100)}, 'Lights saved');
  } catch (error) { showToast(error.message); }
}
async function togglePump(checkbox) {
  const requested = checkbox.checked;
  if (await command(checkbox, '/api/pump', {state: requested ? 1 : 0}, requested ? 'Pump ON' : 'Pump OFF')) {
    if (!requested) openSchedule(); else closeSchedule();
  } else if (latestState) checkbox.checked = latestState.pump === 1;
}
function openSchedule() {
  el('schedule-direction').textContent = latestState?.pump ? 'Turn off in' : 'Turn on again in';
  el('pump-schedule').classList.remove('hidden');
}
function closeSchedule() { el('pump-schedule').classList.add('hidden'); }
async function cancelSchedule(button) {
  if (await command(button, '/api/pump/schedule', {mode:'manual'}, 'Schedule stopped')) closeSchedule();
}
async function confirmSchedule(button) {
  try {
    const minutes = number('sched-hours',0,168) * 60 + number('sched-mins',0,59);
    if (minutes < 1 || minutes > 10080) throw new Error('Choose a delay from 1 minute to 7 days');
    if (await command(button, '/api/pump/schedule', {mode:'timer',minutes}, 'Next switch scheduled')) closeSchedule();
  } catch (error) { showToast(error.message); }
}
async function saveCycle(button) {
  try {
    if (await command(button, '/api/pump/schedule', {mode:'cycle', onMinutes:number('cycle-on',1,10080),
      offMinutes:number('cycle-off',1,10080)}, 'Automatic cycle started')) closeSchedule();
  } catch (error) { showToast(error.message); }
}
async function saveSettings(button) {
  try {
    const refresh = number('setting-refresh',1,60);
    if (await command(button, '/api/settings', {floors:number('setting-floors',1,3)}, 'Settings saved')) {
      localStorage.setItem('refreshSeconds', String(refresh)); restartPolling(refresh * 1000);
    }
  } catch (error) { showToast(error.message); }
}
async function saveWiFi(event, form) {
  event.preventDefault();
  if (await command(form.querySelector('button'), '/api/wifi', {ssid:el('wifi-ssid').value,
    password:el('wifi-password').value}, 'Connecting — watch network status below')) el('wifi-password').value = '';
}
async function saveSetupWiFi(event, form) {
  event.preventDefault();
  el('setup-error').textContent = '';
  if (await command(form.querySelector('button'), '/api/wifi', {ssid:el('setup-ssid').value,
    password:el('setup-password').value}, 'Credentials saved — connecting…')) el('setup-password').value = '';
}
function renderWiFiMode(data) {
  const supported = ['setup', 'connecting', 'ready'].includes(data.wifiMode);
  const mode = supported ? data.wifiMode : 'setup';
  el('garden-hub').hidden = mode !== 'ready';
  el('wifi-setup').hidden = mode === 'ready';
  el('setup-form').hidden = mode !== 'setup';
  el('setup-progress').hidden = mode === 'setup';
  el('setup-progress').textContent = mode === 'connecting'
    ? 'Connecting to ' + (data.ssid || 'your network') + '… This can take up to 20 seconds.'
    : 'Checking your AeroGarden…';
  if (mode === 'setup') {
    if (setupMode !== 'setup') {
      el('setup-ssid').value = ''; el('setup-password').value = '';
    }
    el('setup-error').textContent = data.wifiError || (supported ? '' : 'Device status is missing Wi-Fi setup information. Upload both the updated firmware and filesystem.');
  }
  if (mode !== setupMode) {
    if (mode !== 'ready') initialized = false;
    setupMode = mode;
    restartPolling(refreshMs);
  }
}
async function fetchSensors() {
  if (busy || polling) return;
  polling = true;
  const version = revision;
  try {
    const data = await request('/api/sensors');
    if (!busy && version === revision) updateUI(data);
    document.querySelector('.connected-label').textContent = '● Connected';
  } catch {
    document.querySelector('.connected-label').textContent = '● Reconnecting';
    if (setupMode !== 'ready') renderWiFiMode({wifiMode:'setup', wifiError:'Cannot read device status. Connect to AeroGarden-Setup and open http://192.168.4.1. Retrying automatically.'});
  } finally { polling = false; }
}
function updateUI(data, fromCommand = false) {
  latestState = data;
  renderWiFiMode(data);
  if (data.wifiMode !== 'ready') return;
  const lightPct = (data.light || [0,0,0,0]).map(v => Math.round(v/4095*100));
  lightPct.forEach((pct,i) => { el('bar-light-'+i).style.width=pct+'%'; });
  setBadge('badge-light', Math.round(lightPct.reduce((a,b)=>a+b,0)/lightPct.length)+'%');
  for (const [key,bar,label] of [['waterTemp','bar-water-temp','val-water-temp'],['ambientTemp','bar-ambient-temp','val-ambient-temp'],['humidity','bar-humidity','val-humidity']]) {
    const value=data[key];
    el(label).textContent=Number.isFinite(value)?value.toFixed(1):'–';
    el(bar).style.width=(Number.isFinite(value)?Math.max(0,Math.min(100,key==='humidity'?value:value*2)):0)+'%';
  }
  setBadge('badge-temp',Number.isFinite(data.ambientTemp)?data.ambientTemp.toFixed(1)+'°C':'–');
  setBadge('badge-humidity',Number.isFinite(data.humidity)?data.humidity.toFixed(1)+'%':'–');
  el('pump-toggle').checked=data.pump===1;
  el('pump-status').textContent=data.pumpMode==='manual'?'Manual control':
    (data.pumpMode==='cycle'?'Automatic cycle. ':'Timer. ')+'Turns '+(data.pump?'off':'on')+' in '+formatUptime(data.remainingSeconds);
  el('schedule-direction').textContent=data.pump?'Turn off in':'Turn on again in';
  el('light-output').textContent='Current output: red '+Math.round(data.actualRed/255*100)+'%, blue '+Math.round(data.actualBlue/255*100)+'%';
  el('wifi-status').textContent=data.wifiStatus+(data.ssid?' · '+data.ssid:'');
  el('status-ip').textContent=data.ip;
  el('status-uptime').textContent=formatUptime(data.uptime);
  // Initialize editable fields once; polling must not overwrite unsaved edits.
  if (!initialized) {
    el('slider-red-0').value=data.red; el('slider-blue-0').value=data.blue;
    el('auto-light').checked=data.autoLight; el('light-target').value=data.lightTarget;
    el('cycle-on').value=data.onMinutes; el('cycle-off').value=data.offMinutes;
    el('setting-floors').value=data.floors; el('wifi-ssid').value=data.ssid||'';
    updateSliderPreview(el('slider-red-0'),'red'); updateSliderPreview(el('slider-blue-0'),'blue');
    initialized=true;
  }
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
  refreshMs = intervalMs;
  if (pollInterval) clearInterval(pollInterval);
  pollInterval = setInterval(fetchSensors, setupMode === 'ready' ? intervalMs : 1000);
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
  let saved = 0;
  try { saved = Number(localStorage.getItem('refreshSeconds')); } catch { /* Storage may be unavailable in private browsing. */ }
  const refresh = Number.isInteger(saved) && saved >= 1 && saved <= 60 ? saved : 5;
  el('setting-refresh').value = refresh;
  restartPolling(refresh * 1000);
});
