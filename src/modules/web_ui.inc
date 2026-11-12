// ---- Web UI ----
const char SHARED_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>ElementIQ</title>
<style>
:root{--bg:#05080f;--card:#0a1628;--primary:#1e6bb8;--primary-bright:#3b9eff;--accent:#ff8c00;--accent-hot:#ff5a00;--text:#e8f0fa;--muted:#6b8aad;--track:#123050;--drawer:#0c1a2e}
body.theme-dark{--bg:#0a0a0a;--card:#161616;--primary:#3b82f6;--primary-bright:#60a5fa;--track:#2a2a2a;--drawer:#111;--muted:#888;--text:#f0f0f0}
body.theme-light{--bg:#e8eef5;--card:#fff;--primary:#1e6bb8;--primary-bright:#0284c7;--text:#0f172a;--muted:#64748b;--track:#cbd5e1;--drawer:#f1f5f9;--accent:#ea580c;--accent-hot:#dc2626}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding:16px 12px 40px}
.topbar{display:flex;align-items:center;justify-content:space-between;max-width:440px;margin:0 auto 16px}
.brand{display:flex;align-items:center;gap:10px}
.brand img{width:48px;height:48px;border-radius:10px;object-fit:contain}
.brand span{font-weight:700;color:var(--primary-bright)}
.hamburger{width:42px;height:42px;border:none;border-radius:10px;background:var(--card);cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px}
.hamburger span{display:block;width:20px;height:2px;background:var(--text)}
.overlay{position:fixed;inset:0;background:rgba(0,0,0,.45);z-index:40;opacity:0;pointer-events:none;transition:.25s}
.overlay.open{opacity:1;pointer-events:auto}
.drawer{position:fixed;top:0;right:0;width:min(300px,88vw);height:100%;background:var(--drawer);z-index:50;transform:translateX(100%);transition:.28s;overflow-y:auto;padding:20px 18px 40px}
.drawer.open{transform:translateX(0)}
.drawer h2{font-size:1rem;margin-bottom:16px;color:var(--primary-bright)}
.drawer nav a{display:block;padding:12px 14px;border-radius:10px;color:var(--text);text-decoration:none;margin-bottom:4px;font-weight:600}
.drawer nav a:hover,.drawer nav a.active{background:var(--primary);color:#fff}
.drawer nav .sub{padding-left:28px;font-size:.88rem;font-weight:500;color:var(--muted)}
.card{background:var(--card);border-radius:20px;padding:20px;max-width:440px;margin:0 auto;box-shadow:0 12px 40px rgba(0,0,0,.35)}
.card h1{font-size:1.1rem;margin-bottom:14px;color:var(--primary-bright)}
label{display:block;font-size:.72rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:12px 0 6px}
label.row{display:flex;align-items:center;justify-content:space-between;text-transform:none;font-size:.9rem;letter-spacing:0;color:var(--text)}
input,select{width:100%;padding:10px;border-radius:8px;border:1px solid var(--track);background:var(--bg);color:var(--text);font-size:.95rem}
button.save,button.primary{width:100%;padding:12px;border:none;border-radius:10px;background:var(--primary);color:#fff;font-weight:600;cursor:pointer;margin-top:12px}
button.danger{background:#b91c1c}
.hint{font-size:.75rem;color:var(--muted);margin-top:6px}
.footer{max-width:440px;margin:18px auto 0;text-align:center;font-size:.75rem;color:var(--muted)}
.footer #clock{font-size:1.1rem;font-weight:600;color:var(--primary-bright);margin-bottom:4px}
.badge{display:inline-block;padding:2px 8px;border-radius:6px;font-size:.7rem;font-weight:700;background:#b91c1c;color:#fff;margin-left:6px}
.dial-wrap{position:relative;width:260px;height:260px;margin:0 auto 12px;user-select:none;touch-action:none}
.dial-svg{width:100%;height:100%}
.dial-track{fill:none;stroke:var(--track);stroke-width:18}
.dial-progress{fill:none;stroke:var(--primary);stroke-width:18;stroke-linecap:round}
.dial-progress.heating{stroke:var(--accent)}.dial-progress.boost{stroke:var(--accent-hot)}
.dial-center{position:absolute;inset:48px;border-radius:50%;background:var(--bg);display:flex;flex-direction:column;align-items:center;justify-content:center}
.dial-temp{font-size:2.8rem;font-weight:700;line-height:1}
.dial-unit{font-size:.85rem;color:var(--muted)}
.dial-target{font-size:.8rem;color:var(--primary-bright);margin-top:8px;font-weight:600}
.dial-action{font-size:.7rem;text-transform:uppercase;letter-spacing:.1em;margin-top:4px;color:var(--muted)}
.dial-action.heating{color:var(--accent)}.dial-action.boost{color:var(--accent-hot)}
.modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px}
.presets{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.mode-btn,.preset-btn{background:var(--bg);border:1px solid transparent;color:var(--muted);border-radius:12px;padding:10px 4px;font-size:.72rem;font-weight:700;text-transform:uppercase;cursor:pointer}
.mode-btn.active{background:var(--primary);color:#fff}
.mode-btn.active[data-mode=off]{background:#475569}
.mode-btn.active[data-mode=boost]{background:var(--accent-hot);color:#fff}
.preset-btn.active{background:var(--primary);color:#fff}
canvas.chart{width:100%;height:160px;background:var(--bg);border-radius:12px;margin-top:8px}
.stats{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:12px;text-align:center}
.stats div{background:var(--bg);border-radius:10px;padding:10px 4px}
.stats .n{font-size:1.1rem;font-weight:700;color:var(--primary-bright)}
.stats .l{font-size:.65rem;color:var(--muted);text-transform:uppercase}
</style></head><body>
)rawliteral";

const char NAV_SCRIPT[] PROGMEM = R"rawliteral(
<script>
function toggleMenu(){document.getElementById('drawer').classList.toggle('open');document.getElementById('overlay').classList.toggle('open')}
function applyTheme(id){document.body.classList.remove('theme-dark','theme-light');if(id==1)document.body.classList.add('theme-dark');if(id==2)document.body.classList.add('theme-light')}
async function saveSettings(data){try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
const d=await r.json();if(!r.ok)throw new Error(d.error||'Settings could not be saved');return true}
catch(e){alert('Save failed: '+e.message);return false}}
async function loadTheme(){try{const r=await fetch('/api/status');const d=await r.json();applyTheme(Number(d.theme||0));const c=document.getElementById('clock');if(c)c.textContent=d.time||'--:--'}catch(e){}}
loadTheme();
</script>
)rawliteral";

String navHtml(const char* active) {
  // Use custom raw-string delimiter — plain R"( breaks on onclick="...()"
  String s = R"nav(
  <div class="topbar"><div class="brand"><img src="/logo.png" alt="" onerror="this.style.display='none'"><span>ElementIQ</span></div>
  <button class="hamburger" onclick="toggleMenu()"><span></span><span></span><span></span></button></div>
  <div class="overlay" id="overlay" onclick="toggleMenu()"></div>
  <aside class="drawer" id="drawer"><h2>Menu</h2><nav>
  <a href="/" class=")nav";
  if (!strcmp(active,"home")) s += "active";
  s += R"nav(">Home</a><a href="/connections" class=")nav";
  if (!strcmp(active,"connections")) s += "active";
  s += R"nav(">Connections</a><a href="/connections#ap" class="sub">AP</a><a href="/connections#mqtt" class="sub">MQTT</a>
  <a href="/time" class=")nav";
  if (!strcmp(active,"time")) s += "active";
  s += R"nav(">Time</a><a href="/temperature" class=")nav";
  if (!strcmp(active,"temperature")) s += "active";
  s += R"nav(">Temperature</a><a href="/schedule" class=")nav";
  if (!strcmp(active,"schedule")) s += "active";
  s += R"nav(">Schedule</a><a href="/stats" class=")nav";
  if (!strcmp(active,"stats")) s += "active";
  s += R"nav(">Stats / Graph</a><a href="/update" class=")nav";
  if (!strcmp(active,"update")) s += "active";
  s += R"nav(">Update</a></nav></aside>
)nav";
  return s;
}

const char INDEX_BODY[] PROGMEM = R"rawliteral(
<div class="card">
  <div id="fault" style="display:none" class="hint"><span class="badge">SENSOR FAULT</span> Heater locked off</div>
  <div class="dial-wrap" id="dial">
    <svg class="dial-svg" viewBox="0 0 260 260">
      <circle class="dial-track" cx="130" cy="130" r="105" stroke-dasharray="494" transform="rotate(135 130 130)"/>
      <circle class="dial-progress" id="arc" cx="130" cy="130" r="105" stroke-dasharray="494" stroke-dashoffset="494" transform="rotate(135 130 130)"/>
    </svg>
    <div class="dial-center">
      <div class="dial-temp" id="temp">--.-</div><div class="dial-unit">C</div>
      <div class="dial-target" id="target">Set 55.0</div>
      <div class="dial-action" id="action">idle</div>
    </div>
  </div>
  <div class="modes">
    <button class="mode-btn" data-mode="off" onclick="setMode('off')">Off</button>
    <button class="mode-btn" data-mode="auto" onclick="setMode('auto')">Auto</button>
    <button class="mode-btn" data-mode="boost" onclick="setMode('boost')">Boost</button>
  </div>
  <div class="presets">
    <button class="preset-btn" data-p="home" onclick="setPreset('home')">Home</button>
    <button class="preset-btn" data-p="away" onclick="setPreset('away')">Away</button>
    <button class="preset-btn" data-p="eco" onclick="setPreset('eco')">Eco</button>
  </div>
  <div class="hint" id="schedHint" style="margin-top:10px;text-align:center"></div>
</div>
<div class="footer"><div id="clock">--:--</div><div id="info"></div></div>
<script>
const MIN=30,MAX=100,SPAN=MAX-MIN,CIRC=494.8;let setpoint=55,dragging=false;
const arc=document.getElementById('arc'),dial=document.getElementById('dial');
function tempToOffset(t){return CIRC*(1-Math.max(0,Math.min(1,(t-MIN)/SPAN))*0.75)}
function setArc(t,h,b){arc.style.strokeDashoffset=tempToOffset(t);arc.className='dial-progress'+(b?' boost':h?' heating':'')}
function angleFromEvent(e){const r=dial.getBoundingClientRect(),cx=r.left+r.width/2,cy=r.top+r.height/2,pt=e.touches?e.touches[0]:e;let a=Math.atan2(pt.clientY-cy,pt.clientX-cx)*180/Math.PI;a=(a+360+135)%360;if(a>270)a=a>315?0:270;return a}
function onStart(e){dragging=true;onMove(e);e.preventDefault()}
function onMove(e){if(!dragging)return;const a=angleFromEvent(e);setpoint=Math.round((MIN+(a/270)*SPAN)*2)/2;document.getElementById('target').textContent='Set '+setpoint.toFixed(1);setArc(setpoint,false,false);e.preventDefault()}
function onEnd(){if(!dragging)return;dragging=false;fetch('/api/setpoint',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({setpoint})})}
dial.addEventListener('mousedown',onStart);dial.addEventListener('touchstart',onStart,{passive:false});
window.addEventListener('mousemove',onMove);window.addEventListener('touchmove',onMove,{passive:false});
window.addEventListener('mouseup',onEnd);window.addEventListener('touchend',onEnd);
async function setMode(m){await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:m})});update()}
async function setPreset(p){await fetch('/api/preset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({preset:p})});update()}
async function update(){try{
  const d=await(await fetch('/api/status')).json();
  const t=d.temperature!=null?Number(d.temperature):null;
  document.getElementById('temp').textContent=t!=null?t.toFixed(1):'--.-';
  setpoint=Number(d.setpoint);const tgt=d.mode==='boost'?Number(d.boost_temp):setpoint;
  document.getElementById('target').textContent=(d.mode==='boost'?'Boost ':'Set ')+tgt.toFixed(1);
  const act=document.getElementById('action');act.textContent=d.mode==='boost'?'boost':(d.heater?'heating':'idle');
  act.className='dial-action'+(d.mode==='boost'?' boost':d.heater?' heating':'');
  if(!dragging)setArc(tgt,d.heater,d.mode==='boost');
  document.querySelectorAll('.mode-btn').forEach(b=>b.classList.toggle('active',b.dataset.mode===d.mode));
  document.querySelectorAll('.preset-btn').forEach(b=>b.classList.toggle('active',b.dataset.p===d.preset));
  document.getElementById('fault').style.display=d.sensor_fault?'block':'none';
  document.getElementById('schedHint').textContent=d.schedule_enabled?(d.schedule_active?'Schedule: in window':'Schedule: outside window – heating blocked'):'';
  document.getElementById('clock').textContent=d.time||'--:--';
  document.getElementById('info').textContent='IP '+(d.ip||'—')+'  ·  v'+d.version;
  applyTheme(Number(d.theme||0));
}catch(e){}}
setInterval(update,2000);update();
</script></body></html>
)rawliteral";

const char CONNECTIONS_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Connections</h1>
<div class="hint" id="wifi_status" style="margin-bottom:14px;font-size:.9rem;color:var(--primary-bright)">-</div>
<section id="ap"><h3 style="font-size:.8rem;color:var(--muted)">Access Point</h3>
<label class="row">Enable AP when online <input type="checkbox" id="ap_enabled"></label>
<label>AP Name</label><input type="text" id="ap_name" maxlength="31">
<label>AP Password</label><input type="password" id="ap_pass" maxlength="31" autocomplete="new-password" placeholder="Leave blank to keep current">
<label class="row">Use open AP (clear password) <input type="checkbox" id="ap_open"></label>
<div class="hint">A password must be 8–31 characters. New devices start with an open AP.</div></section>
<section id="mqtt" style="margin-top:16px"><h3 style="font-size:.8rem;color:var(--muted)">MQTT</h3>
<label class="row">Enable MQTT <input type="checkbox" id="mqtt_enabled"></label>
<label>Broker host</label><input type="text" id="mqtt_host">
<label>Port</label><input type="text" id="mqtt_port" value="1883">
<label>User</label><input type="text" id="mqtt_user">
<label>Password</label><input type="password" id="mqtt_pass" placeholder="Leave blank to keep">
<div class="hint" id="mqtt_status">-</div></section>
<label>Friendly name</label><input type="text" id="friendly_name" maxlength="39">
<label>Theme</label><select id="theme"><option value="0">Blue</option><option value="1">Dark</option><option value="2">Light</option></select>
<button class="save" onclick="save()">Save</button>
<div class="hint" style="margin-top:12px"><a href="/api/resetwifi" style="color:var(--primary-bright)" onclick="return confirm('Forget WiFi?')">Reset WiFi</a></div>
</div><div class="footer"><div id="clock">--:--</div></div>
<script>
async function load(){const d=await(await fetch('/api/status')).json();
document.getElementById('ap_enabled').checked=!!d.ap_enabled;document.getElementById('ap_name').value=d.ap_name||'';
document.getElementById('ap_open').checked=!d.ap_password_configured;
document.getElementById('mqtt_enabled').checked=d.mqtt_enabled!==false;document.getElementById('mqtt_host').value=d.mqtt_host||'';
document.getElementById('mqtt_port').value=d.mqtt_port||'1883';document.getElementById('mqtt_user').value=d.mqtt_user||'';
document.getElementById('friendly_name').value=d.friendly_name||'';document.getElementById('theme').value=String(d.theme||0);
document.getElementById('mqtt_status').textContent=d.mqtt_enabled===false?'MQTT: disabled':(d.mqtt_host?('MQTT: '+(d.mqtt_connected?'ok':'down')):'not set');
const ip=d.ip||'';
document.getElementById('wifi_status').textContent=ip&&ip!=='0.0.0.0'?('WiFi IP: '+ip+'  ·  mDNS: ElementIQ.local'):'Not connected to WiFi';
if(d.settings_saved===false)document.getElementById('wifi_status').textContent+=' · Settings not persisted';
document.getElementById('clock').textContent=d.time||'--:--';applyTheme(Number(d.theme||0))}
async function save(){const data={
ap_enabled:document.getElementById('ap_enabled').checked,ap_name:document.getElementById('ap_name').value,
mqtt_enabled:document.getElementById('mqtt_enabled').checked,mqtt_host:document.getElementById('mqtt_host').value,mqtt_port:document.getElementById('mqtt_port').value,
mqtt_user:document.getElementById('mqtt_user').value,mqtt_pass:document.getElementById('mqtt_pass').value,
friendly_name:document.getElementById('friendly_name').value,theme:parseInt(document.getElementById('theme').value)||0};
const apPass=document.getElementById('ap_pass').value;
if(apPass||document.getElementById('ap_open').checked)data.ap_pass=apPass;
if(!await saveSettings(data))return;
document.getElementById('mqtt_pass').value='';document.getElementById('ap_pass').value='';alert('Saved');load()}
load();
</script></body></html>
)rawliteral";

const char TIME_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Time</h1>
<label>Timezone offset (hours UTC)</label><input type="number" id="tz" min="-12" max="14" value="2">
<div class="hint">SAST = 2 | Current: <span id="clock2">--:--</span></div>
<button class="save" onclick="save()">Save</button></div>
<div class="footer"><div id="clock">--:--</div></div>
<script>
async function load(){const d=await(await fetch('/api/status')).json();
document.getElementById('tz').value=d.tz!=null?d.tz:2;document.getElementById('clock').textContent=d.time||'--:--';
document.getElementById('clock2').textContent=d.time||'--:--';applyTheme(Number(d.theme||0))}
async function save(){if(!await saveSettings({tz:parseInt(document.getElementById('tz').value)||2}))return;alert('Saved');load()}
load();setInterval(load,30000);
</script></body></html>
)rawliteral";

const char TEMP_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Temperature</h1>
<label>Auto setpoint (°C)</label><input type="number" id="setpoint" min="30" max="100" step="0.5">
<label>Boost temperature (°C)</label><input type="number" id="boost_temp" min="30" max="100" step="0.5">
<label>Soft min for boost (°C, 0=off)</label><input type="number" id="boost_min" min="0" max="100" step="0.5">
<div class="hint">Boost only starts if current temp is below this (avoids useless boosts)</div>
<label>Hysteresis (°C under to re-on)</label><input type="number" id="hysteresis" min="0.5" max="20" step="0.5">
<label>Home preset (°C)</label><input type="number" id="home_temp" min="30" max="100" step="0.5">
<label>Away preset (°C)</label><input type="number" id="away_temp" min="30" max="100" step="0.5">
<label>Eco preset (°C)</label><input type="number" id="eco_temp" min="30" max="100" step="0.5">
<label>Min ON (s)</label><input type="number" id="min_on" min="5" max="600">
<label>Min OFF (s)</label><input type="number" id="min_off" min="5" max="600">
<button class="save" onclick="save()">Save</button></div>
<div class="footer"><div id="clock">--:--</div></div>
<script>
async function load(){const d=await(await fetch('/api/status')).json();
document.getElementById('setpoint').value=d.setpoint;document.getElementById('boost_temp').value=d.boost_temp;
document.getElementById('boost_min').value=d.boost_min_temp;document.getElementById('hysteresis').value=d.hysteresis;
document.getElementById('home_temp').value=d.home_temp;document.getElementById('away_temp').value=d.away_temp;
document.getElementById('eco_temp').value=d.eco_temp;
document.getElementById('min_on').value=Math.round((d.min_on_ms||30000)/1000);
document.getElementById('min_off').value=Math.round((d.min_off_ms||30000)/1000);
document.getElementById('clock').textContent=d.time||'--:--';applyTheme(Number(d.theme||0))}
async function save(){if(!await saveSettings({
setpoint:+document.getElementById('setpoint').value,boost_temp:+document.getElementById('boost_temp').value,
boost_min_temp:+document.getElementById('boost_min').value,hysteresis:+document.getElementById('hysteresis').value,
home_temp:+document.getElementById('home_temp').value,away_temp:+document.getElementById('away_temp').value,
eco_temp:+document.getElementById('eco_temp').value,
min_on_ms:+document.getElementById('min_on').value*1000,min_off_ms:+document.getElementById('min_off').value*1000}))return;
alert('Saved');load()}
load();
</script></body></html>
)rawliteral";

const char SCHEDULE_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Schedule</h1>
<label class="row">Enable schedule <input type="checkbox" id="schedule_enabled"></label>
<div class="hint">When enabled, Auto only heats inside the window. Boost still works anytime. Outside window heater is forced off.</div>
<label>Heat allowed from (hour)</label><input type="number" id="on_h" min="0" max="23">
<label>From (minute)</label><input type="number" id="on_m" min="0" max="59">
<label>Until (hour)</label><input type="number" id="off_h" min="0" max="23">
<label>Until (minute)</label><input type="number" id="off_m" min="0" max="59">
<div class="hint">Overnight windows OK (e.g. 22:00 → 06:00)</div>
<button class="save" onclick="save()">Save</button>
<div class="hint" id="status" style="margin-top:12px"></div>
</div><div class="footer"><div id="clock">--:--</div></div>
<script>
async function load(){const d=await(await fetch('/api/status')).json();
document.getElementById('schedule_enabled').checked=!!d.schedule_enabled;
document.getElementById('on_h').value=d.sched_on_h;document.getElementById('on_m').value=d.sched_on_m;
document.getElementById('off_h').value=d.sched_off_h;document.getElementById('off_m').value=d.sched_off_m;
document.getElementById('status').textContent=d.schedule_enabled?(d.schedule_active?'Currently IN heating window':'Currently OUTSIDE heating window'):'Schedule off';
document.getElementById('clock').textContent=d.time||'--:--';applyTheme(Number(d.theme||0))}
async function save(){if(!await saveSettings({
schedule_enabled:document.getElementById('schedule_enabled').checked,
sched_on_h:+document.getElementById('on_h').value,sched_on_m:+document.getElementById('on_m').value,
sched_off_h:+document.getElementById('off_h').value,sched_off_m:+document.getElementById('off_m').value}))return;
alert('Saved');load()}
load();setInterval(load,15000);
</script></body></html>
)rawliteral";

const char STATS_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Stats & temperature</h1>
<div class="stats">
  <div><div class="n" id="rtoday">-</div><div class="l">Today (h)</div></div>
  <div><div class="n" id="rweek">-</div><div class="l">Week (h)</div></div>
  <div><div class="n" id="rtotal">-</div><div class="l">Total (h)</div></div>
</div>
<canvas class="chart" id="chart" width="400" height="160"></canvas>
<div class="hint" id="chartHint">Loading history…</div>
</div><div class="footer"><div id="clock">--:--</div></div>
<script>
function drawChart(temps){
  const c=document.getElementById('chart'),ctx=c.getContext('2d');
  const w=c.width,h=c.height;ctx.clearRect(0,0,w,h);
  if(!temps||temps.length<2){document.getElementById('chartHint').textContent='Not enough samples yet';return}
  let mn=Math.min(...temps),mx=Math.max(...temps);if(mx-mn<2){mn-=1;mx+=1}
  ctx.strokeStyle=getComputedStyle(document.body).getPropertyValue('--track')||'#123050';
  ctx.beginPath();ctx.moveTo(0,h-1);ctx.lineTo(w,h-1);ctx.stroke();
  ctx.strokeStyle=getComputedStyle(document.body).getPropertyValue('--primary-bright')||'#3b9eff';
  ctx.lineWidth=2;ctx.beginPath();
  temps.forEach((t,i)=>{const x=i/(temps.length-1)*w,y=h-4-((t-mn)/(mx-mn))*(h-12);
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});
  ctx.stroke();
  document.getElementById('chartHint').textContent=temps.length+' samples · min '+mn.toFixed(1)+' · max '+mx.toFixed(1)+' °C';
}
async function load(){
  const d=await(await fetch('/api/status')).json();
  document.getElementById('rtoday').textContent=(d.runtime_today_sec/3600).toFixed(2);
  document.getElementById('rweek').textContent=(d.runtime_week_sec/3600).toFixed(2);
  document.getElementById('rtotal').textContent=(d.runtime_total_sec/3600).toFixed(1);
  document.getElementById('clock').textContent=d.time||'--:--';applyTheme(Number(d.theme||0));
  const h=await(await fetch('/api/history')).json();
  drawChart((h.temps||[]).map(Number));
}
load();setInterval(load,10000);
</script></body></html>
)rawliteral";

const char UPDATE_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Update & reset</h1>
<p class="hint">Version: <strong id="ver">-</strong></p>
<p class="hint" id="ota_msg">-</p>
<label>GitHub owner</label><input type="text" id="github_owner">
<label>GitHub repo</label><input type="text" id="github_repo">
<div class="hint">Release asset must be <code>firmware.bin</code></div>
<label class="row">Auto-check on boot <input type="checkbox" id="ota_auto"></label>
<button class="save" onclick="saveGh()">Save GitHub</button>
<button class="save" style="background:#0d9488" onclick="check()">Check update</button>
<button class="save danger" onclick="install()">Install update</button>
<hr style="border:none;border-top:1px solid var(--track);margin:20px 0">
<button class="save danger" onclick="factory()">Factory reset</button>
<div class="hint">Clears all settings + WiFi. Or hold BOOT (GPIO9) for 5s at boot.</div>
<div class="hint" style="margin-top:12px">Local OTA: PlatformIO → ElementIQ.local</div>
</div><div class="footer"><div id="clock">--:--</div></div>
<script>
async function load(){const d=await(await fetch('/api/status')).json();
document.getElementById('ver').textContent=d.version;document.getElementById('ota_msg').textContent=d.ota_msg||'Idle';
document.getElementById('github_owner').value=d.github_owner||'';document.getElementById('github_repo').value=d.github_repo||'';
document.getElementById('ota_auto').checked=!!d.ota_auto;document.getElementById('clock').textContent=d.time||'--:--';
applyTheme(Number(d.theme||0))}
async function saveGh(){if(!await saveSettings({
github_owner:document.getElementById('github_owner').value,github_repo:document.getElementById('github_repo').value,
ota_auto:document.getElementById('ota_auto').checked}))return;alert('Saved');load()}
async function watchOta(){const d=await(await fetch('/api/status')).json();
document.getElementById('ota_msg').textContent=d.ota_msg||'Idle';
if(d.ota_busy)setTimeout(watchOta,1500)}
async function check(){document.getElementById('ota_msg').textContent='Checking...';
const r=await fetch('/api/ota/check');if(!r.ok){const d=await r.json();document.getElementById('ota_msg').textContent=d.error||'Could not queue check';return}
document.getElementById('ota_msg').textContent='Check queued';watchOta()}
async function install(){if(!confirm('Install from GitHub and reboot?'))return;
document.getElementById('ota_msg').textContent='Installing...';const r=await fetch('/api/ota/install',{method:'POST'});
if(!r.ok){const d=await r.json();document.getElementById('ota_msg').textContent=d.error||'Could not queue install';return}watchOta()}
async function factory(){if(!confirm('FACTORY RESET all settings and WiFi?'))return;
await fetch('/api/factoryreset',{method:'POST'})}
load();
</script></body></html>
)rawliteral";

void sendPage(AsyncWebServerRequest *req, const char* active, const char* body) {
  String navigation = navHtml(active);
  AsyncResponseStream *response = req->beginResponseStream("text/html");
  response->print(FPSTR(SHARED_HEAD));
  response->print(FPSTR(NAV_SCRIPT));
  response->print(navigation);
  response->print(FPSTR(body));
  req->send(response);
}

void setupWebUI() {
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/logo.png")) {
      AsyncWebServerResponse *res = req->beginResponse(LittleFS, "/logo.png", "image/png");
      res->addHeader("Cache-Control", "public, max-age=86400");
      req->send(res);
    } else req->send(404);
  });
  server.on("/connections", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "connections", CONNECTIONS_BODY); });
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "time", TIME_BODY); });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "temperature", TEMP_BODY); });
  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "schedule", SCHEDULE_BODY); });
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "stats", STATS_BODY); });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "update", UPDATE_BODY); });
}

