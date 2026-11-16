// ---- Web UI ----
const char SHARED_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>ElementIQ</title>
<style>
:root{
  --bg:#0b1220;--sidebar:#0e1626;--card:#121c2e;--card-border:#1c2a42;
  --primary:#3b9eff;--primary-dim:#1e6bb8;--accent:#ff8c00;--accent-hot:#ff5a00;
  --text:#e8f0fa;--muted:#7a92b0;--track:#1a2a40;--row:#0f1828;--hover:#18253a;
}
body.theme-dark{--bg:#0a0a0a;--sidebar:#111;--card:#161616;--card-border:#222;--primary:#60a5fa;--primary-dim:#3b82f6;--track:#2a2a2a;--row:#121212;--hover:#1a1a1a;--muted:#888;--text:#f0f0f0}
body.theme-light{--bg:#e8eef5;--sidebar:#f1f5f9;--card:#fff;--card-border:#dbe3ee;--primary:#0284c7;--primary-dim:#1e6bb8;--text:#0f172a;--muted:#64748b;--track:#cbd5e1;--row:#f8fafc;--hover:#e2e8f0;--accent:#ea580c;--accent-hot:#dc2626}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);display:flex;min-height:100vh}
/* ---- Sidebar (Shelly-style) ---- */
.sidebar{width:220px;min-width:220px;background:var(--sidebar);border-right:1px solid var(--card-border);display:flex;flex-direction:column;padding:16px 12px;position:fixed;top:0;left:0;bottom:0;z-index:30;transition:transform .25s}
.brand{display:flex;align-items:center;gap:12px;padding:8px 10px 18px;border-bottom:1px solid var(--card-border);margin-bottom:12px}
.brand img{width:48px;height:48px;object-fit:contain;flex-shrink:0}
.brand .brand-text{display:flex;flex-direction:column;line-height:1.15}
.brand .brand-text strong{font-size:1.26rem;font-weight:700;background:linear-gradient(90deg,#3b9eff,#ff8c00);-webkit-background-clip:text;background-clip:text;color:transparent}
.brand .brand-text small{font-size:.82rem;color:var(--muted);font-weight:500}
.sidebar nav{display:flex;flex-direction:column;gap:2px;flex:1;overflow-y:auto}
.sidebar nav a{display:flex;align-items:center;gap:10px;padding:10px 12px;border-radius:10px;color:var(--muted);text-decoration:none;font-size:.9rem;font-weight:600;transition:background .15s,color .15s}
.sidebar nav a:hover{background:var(--hover);color:var(--text)}
.sidebar nav a.active{background:rgba(59,158,255,.15);color:var(--primary)}
.sidebar nav a .ico{width:18px;text-align:center;opacity:.85}
.sidebar .side-foot{padding:12px 10px 4px;border-top:1px solid var(--card-border);font-size:.72rem;color:var(--muted)}
/* ---- Main ---- */
.main{flex:1;margin-left:220px;min-width:0;display:flex;flex-direction:column;min-height:100vh}
.topbar{display:flex;align-items:center;justify-content:space-between;padding:14px 22px;border-bottom:1px solid var(--card-border);background:var(--sidebar);position:sticky;top:0;z-index:20}
.topbar .page-title{font-size:1rem;font-weight:600;color:var(--text)}
.topbar .status-icons{display:flex;align-items:center;gap:14px;font-size:.8rem;color:var(--muted)}
.topbar .status-icons .conn-ico{width:22px;height:22px;display:inline-flex;align-items:center;justify-content:center;opacity:.35;color:var(--muted);transition:opacity .2s,color .2s}
.topbar .status-icons .conn-ico.on{opacity:1;color:var(--primary)}
.topbar .status-icons .conn-ico svg{width:20px;height:20px;fill:currentColor}
.topbar .status-icons #clock{font-variant-numeric:tabular-nums;min-width:3rem;text-align:right}
.hamburger{display:none;width:40px;height:40px;border:none;border-radius:10px;background:var(--card);cursor:pointer;flex-direction:column;align-items:center;justify-content:center;gap:5px}
.hamburger span{display:block;width:18px;height:2px;background:var(--text)}
.overlay{position:fixed;inset:0;background:rgba(0,0,0,.45);z-index:25;opacity:0;pointer-events:none;transition:.25s}
.overlay.open{opacity:1;pointer-events:auto}
.content{padding:20px 22px 40px;max-width:720px;width:100%}
.card{background:var(--card);border:1px solid var(--card-border);border-radius:14px;padding:18px 20px;margin-bottom:16px}
.card h1,.card h2{font-size:1rem;font-weight:600;margin-bottom:14px;color:var(--text)}
.kv{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid var(--card-border);font-size:.92rem}
.kv:last-child{border-bottom:none}
.kv .k{color:var(--muted)}
.kv .v{color:var(--text);font-weight:600}
label{display:block;font-size:.72rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:12px 0 6px}
label.row{display:flex;align-items:center;justify-content:space-between;text-transform:none;font-size:.9rem;letter-spacing:0;color:var(--text)}
input,select{width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--card-border);background:var(--row);color:var(--text);font-size:.95rem}
button.save,button.primary{width:100%;padding:12px;border:none;border-radius:10px;background:var(--primary-dim);color:#fff;font-weight:600;cursor:pointer;margin-top:12px}
button.save:hover,button.primary:hover{background:var(--primary)}
button.danger{background:#b91c1c}
.hint{font-size:.75rem;color:var(--muted);margin-top:6px}
.footer{padding:8px 22px 20px;font-size:.72rem;color:var(--muted)}
.footer #clock{font-size:.95rem;font-weight:600;color:var(--primary);margin-bottom:2px}
.badge{display:inline-block;padding:2px 8px;border-radius:6px;font-size:.7rem;font-weight:700;background:#b91c1c;color:#fff;margin-left:6px}
.dial-wrap{position:relative;width:240px;height:240px;margin:0 auto 12px;user-select:none;touch-action:none}
.dial-svg{width:100%;height:100%}
.dial-track{fill:none;stroke:var(--track);stroke-width:16}
.dial-progress{fill:none;stroke:var(--primary);stroke-width:16;stroke-linecap:round}
.dial-progress.heating{stroke:var(--accent)}.dial-progress.boost{stroke:var(--accent-hot)}
.dial-center{position:absolute;inset:44px;border-radius:50%;background:var(--row);display:flex;flex-direction:column;align-items:center;justify-content:center}
.dial-temp{font-size:2.6rem;font-weight:700;line-height:1}
.dial-unit{font-size:.85rem;color:var(--muted)}
.dial-target{font-size:.8rem;color:var(--primary);margin-top:8px;font-weight:600}
.dial-action{font-size:.7rem;text-transform:uppercase;letter-spacing:.1em;margin-top:4px;color:var(--muted)}
.dial-action.heating{color:var(--accent)}.dial-action.boost{color:var(--accent-hot)}
.modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px}
.presets{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.mode-btn,.preset-btn{background:var(--row);border:1px solid var(--card-border);color:var(--muted);border-radius:10px;padding:10px 4px;font-size:.72rem;font-weight:700;text-transform:uppercase;cursor:pointer}
.mode-btn.active{background:var(--primary-dim);color:#fff;border-color:transparent}
.mode-btn.active[data-mode=off]{background:#475569}
.mode-btn.active[data-mode=boost]{background:var(--accent-hot);color:#fff}
.preset-btn.active{background:var(--primary-dim);color:#fff;border-color:transparent}
canvas.chart{width:100%;height:160px;background:var(--row);border-radius:10px;margin-top:8px}
.stats{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:12px;text-align:center}
.stats div{background:var(--row);border-radius:10px;padding:10px 4px;border:1px solid var(--card-border)}
.stats .n{font-size:1.1rem;font-weight:700;color:var(--primary)}
.stats .l{font-size:.65rem;color:var(--muted);text-transform:uppercase}
.list{display:flex;flex-direction:column;gap:8px;margin-top:8px}
.list a.row-link,.list .row-static{display:flex;align-items:center;gap:12px;padding:14px 16px;background:var(--card);border:1px solid var(--card-border);border-radius:12px;text-decoration:none;color:var(--text);cursor:pointer}
.list a.row-link:hover{background:var(--hover)}
.list .row-ico{width:36px;height:36px;border-radius:10px;background:rgba(59,158,255,.12);display:flex;align-items:center;justify-content:center;color:var(--primary);flex-shrink:0;font-size:1rem}
.list .row-meta{flex:1;min-width:0}
.list .row-meta strong{display:block;font-size:.95rem;font-weight:600}
.list .row-meta small{display:block;color:var(--muted);font-size:.78rem;margin-top:2px}
.list .chev{color:var(--muted);font-size:1.1rem}
.back-btn{display:inline-flex;align-items:center;gap:6px;background:none;border:none;color:var(--primary);font-weight:600;cursor:pointer;padding:0;margin-bottom:12px;font-size:.9rem}
.sec-title{font-size:.75rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:18px 0 8px}
.subview{display:none}
.subview.active{display:block}
#settings-menu.active,#conn-menu.active{display:block}

/* Mobile: sidebar becomes drawer */
@media(max-width:800px){
  .sidebar{transform:translateX(-100%)}
  .sidebar.open{transform:translateX(0)}
  .main{margin-left:0}
  .hamburger{display:flex}
  .content{padding:16px 14px 32px}
}
</style></head><body>
)rawliteral";

const char NAV_SCRIPT[] PROGMEM = R"rawliteral(
<script>
function toggleMenu(){
  document.getElementById('sidebar').classList.toggle('open');
  document.getElementById('overlay').classList.toggle('open');
}
function applyTheme(id){
  document.body.classList.remove('theme-dark','theme-light');
  if(id==1)document.body.classList.add('theme-dark');
  if(id==2)document.body.classList.add('theme-light');
}
async function saveSettings(data){
  try{
    const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||'Settings could not be saved');
    return true;
  }catch(e){alert('Save failed: '+e.message);return false}
}
function setConnIcon(id, on, title){
  const el=document.getElementById(id);
  if(!el)return;
  el.classList.toggle('on', !!on);
  if(title) el.title=title;
}
async function refreshTopStatus(){
  try{
    const r=await fetch('/api/status');const d=await r.json();
    applyTheme(Number(d.theme||0));
    const c=document.getElementById('clock');if(c)c.textContent=d.time||'--:--';
    const t=document.getElementById('top-title');
    if(t&&d.friendly_name)t.textContent=d.friendly_name;
    const ip=d.ip||'';
    const wifiOn=ip&&ip!=='0.0.0.0'&&!String(ip).startsWith('192.168.4.');
    setConnIcon('ico-wifi', wifiOn, wifiOn?('Wi‑Fi '+ip+(d.rssi!=null?' · '+d.rssi+' dBm':'')):'Wi‑Fi disconnected');
    const mqttOn=!!d.mqtt_connected;
    setConnIcon('ico-mqtt', mqttOn, mqttOn?('MQTT connected'+(d.mqtt_host?' · '+d.mqtt_host:'')):(d.mqtt_enabled===false?'MQTT disabled':'MQTT offline'));
    setConnIcon('ico-ap', !!d.ap_enabled, d.ap_enabled?('AP on'+(d.ap_name?' · '+d.ap_name:'')):'AP off');
  }catch(e){}
}
async function loadTheme(){await refreshTopStatus()}
loadTheme();
setInterval(refreshTopStatus,5000);
</script>
)rawliteral";

String navHtml(const char* active) {
  auto cls = [&](const char* id) -> const char* {
    return strcmp(active, id) == 0 ? "active" : "";
  };
  String s;
  s += R"nav(
<div class="overlay" id="overlay" onclick="toggleMenu()"></div>
<aside class="sidebar" id="sidebar">
  <div class="brand">
    <img src="/logo.png" alt="ElementIQ" onerror="this.style.display='none'">
    <div class="brand-text"><strong>ElementIQ</strong><small>Heat control</small></div>
  </div>
  <nav>
)nav";
  s += String("<a href=\"/\" class=\"") + cls("home") + "\"><span class=\"ico\">⌂</span>Home</a>";
  s += String("<a href=\"/schedule\" class=\"") + cls("schedule") + "\"><span class=\"ico\">📅</span>Schedule</a>";
  s += String("<a href=\"/connections\" class=\"") + cls("connections") + "\"><span class=\"ico\">🔗</span>Connections</a>";
  s += String("<a href=\"/settings\" class=\"") + cls("settings") + "\"><span class=\"ico\">⚙</span>Settings</a>";
  s += String("<a href=\"/update\" class=\"") + cls("update") + "\"><span class=\"ico\">⟳</span>Update</a>";
  s += R"nav(
  </nav>
  <div class="side-foot">ElementIQ · v1.0.1</div>
</aside>
<div class="main">
  <div class="topbar">
    <button class="hamburger" onclick="toggleMenu()" aria-label="Menu"><span></span><span></span><span></span></button>
    <div class="page-title" id="top-title">ElementIQ</div>
    <div class="status-icons">
      <span class="conn-ico" id="ico-wifi" title="Wi‑Fi"><svg viewBox="0 0 24 24"><path d="M12 18.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3zm-4.07-3.03a1 1 0 0 0 1.41 0 4 4 0 0 1 5.32 0 1 1 0 1 0 1.41-1.41 6 6 0 0 0-8.14 0 1 1 0 0 0 0 1.41zm-2.83-2.83a1 1 0 0 0 1.41 0 8 8 0 0 1 11 0 1 1 0 1 0 1.41-1.41 10 10 0 0 0-13.82 0 1 1 0 0 0 0 1.41z"/></svg></span>
      <span class="conn-ico" id="ico-mqtt" title="MQTT"><svg viewBox="0 0 24 24"><path d="M5 3v18h2v-7.5c0-2.5 1.5-3.5 3.5-3.5H13V8H9.5C6.5 8 5 10 5 13V3zm9 5v2h2.5c1.4 0 2.5 1.1 2.5 2.5V21h2V12.5C21 9.5 18.5 8 16 8H14z"/></svg></span>
      <span class="conn-ico" id="ico-ap" title="Access point"><svg viewBox="0 0 24 24"><path d="M12 3a9 9 0 0 0-9 9h2a7 7 0 0 1 14 0h2a9 9 0 0 0-9-9zm0 4a5 5 0 0 0-5 5h2a3 3 0 0 1 6 0h2a5 5 0 0 0-5-5zm0 4a1 1 0 1 0 0 2 1 1 0 0 0 0-2zm-1 3v7h2v-7h-2z"/></svg></span>
      <span id="clock">--:--</span>
    </div>
  </div>
  <div class="content">
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

<div class="card"><h2>Runtime</h2>
<div class="stats">
  <div><div class="n" id="rt_today">0</div><div class="l">Today (h)</div></div>
  <div><div class="n" id="rt_week">0</div><div class="l">Week (h)</div></div>
  <div><div class="n" id="rt_total">0</div><div class="l">Total (h)</div></div>
</div>
<canvas class="chart" id="chart" width="640" height="160"></canvas>
<div class="hint" id="chartHint">Temperature history</div>
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
  const hrs=s=>((Number(s)||0)/3600).toFixed(1);
  const el=id=>document.getElementById(id);
  if(el('rt_today')){el('rt_today').textContent=hrs(d.runtime_today_sec);el('rt_week').textContent=hrs(d.runtime_week_sec);el('rt_total').textContent=hrs(d.runtime_total_sec);}
  applyTheme(Number(d.theme||0));
  if(window._lastHist===undefined){window._lastHist=0}
  if(Date.now()-window._lastHist>30000){window._lastHist=Date.now();fetch('/api/history').then(r=>r.json()).then(h=>{
    const c=document.getElementById('chart');if(!c||!h.temps)return;const ctx=c.getContext('2d');
    const w=c.width,hgt=c.height;ctx.clearRect(0,0,w,hgt);const temps=h.temps;if(!temps.length)return;
    let mn=Math.min.apply(null,temps),mx=Math.max.apply(null,temps);if(mx-mn<2){mx+=1;mn-=1;}
    ctx.strokeStyle=getComputedStyle(document.body).getPropertyValue('--primary')||'#3b9eff';ctx.lineWidth=2;ctx.beginPath();
    temps.forEach((v,i)=>{const x=i/(temps.length-1||1)*(w-8)+4;const y=hgt-4-((v-mn)/(mx-mn||1))*(hgt-12);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke();
    document.getElementById('chartHint').textContent=temps.length+' samples · '+mn.toFixed(1)+'–'+mx.toFixed(1)+'°C';
  }).catch(()=>{});}

}catch(e){}}
setInterval(update,2000);update();
</script>
</div><!--content-->
</div><!--main-->
</body></html>
)rawliteral";

const char CONNECTIONS_BODY[] PROGMEM = R"rawliteral(
<div id="conn-menu" class="subview active">
  <div class="sec-title">Status</div>
  <div class="list">
    <div class="row-static"><div class="row-ico">📶</div><div class="row-meta"><strong>Wi‑Fi</strong><small id="wifi_status">-</small></div></div>
    <div class="row-static"><div class="row-ico">📡</div><div class="row-meta"><strong>MQTT</strong><small id="mqtt_status">-</small></div></div>
  </div>
  <div class="sec-title">Network settings</div>
  <div class="list">
    <a class="row-link" href="#" onclick="showConn('ap');return false"><div class="row-ico">📻</div><div class="row-meta"><strong>Access point</strong><small>AP name, password, enable when online</small></div><span class="chev">›</span></a>
    <a class="row-link" href="#" onclick="showConn('mqtt');return false"><div class="row-ico">🔗</div><div class="row-meta"><strong>MQTT</strong><small>Broker, credentials, enable</small></div><span class="chev">›</span></a>
  </div>
</div>
<div id="conn-ap" class="subview">
  <button class="back-btn" onclick="showConn('menu')">‹ Back</button>
  <div class="card"><h1>Access point</h1>
  <label class="row">Enable AP when online <input type="checkbox" id="ap_enabled"></label>
  <label>AP Name</label><input type="text" id="ap_name" maxlength="31">
  <label>AP Password</label><input type="password" id="ap_pass" maxlength="31" autocomplete="new-password" placeholder="Leave blank to keep current">
  <label class="row">Use open AP (clear password) <input type="checkbox" id="ap_open"></label>
  <div class="hint">Password must be 8–31 characters, or leave open.</div>
  <button class="save" onclick="saveAp()">Save</button></div>
</div>
<div id="conn-mqtt" class="subview">
  <button class="back-btn" onclick="showConn('menu')">‹ Back</button>
  <div class="card"><h1>MQTT</h1>
  <label class="row">Enable MQTT <input type="checkbox" id="mqtt_enabled"></label>
  <label>Broker host</label><input type="text" id="mqtt_host">
  <label>Port</label><input type="text" id="mqtt_port" value="1883">
  <label>User</label><input type="text" id="mqtt_user">
  <label>Password</label><input type="password" id="mqtt_pass" placeholder="Leave blank to keep">
  <button class="save" onclick="saveMqtt()">Save</button></div>
</div>
<div class="footer"><div id="clock">--:--</div></div>
<script>
function showConn(id){
  document.querySelectorAll('#conn-menu,#conn-ap,#conn-mqtt').forEach(e=>e.classList.remove('active'));
  document.getElementById(id==='menu'?'conn-menu':'conn-'+id).classList.add('active');
}
async function load(){
  const d=await(await fetch('/api/status')).json();
  const ip=d.ip||'';
  document.getElementById('wifi_status').textContent=ip&&ip!=='0.0.0.0'?('Connected · '+ip+(d.rssi!=null?' · '+d.rssi+' dBm':'')):'Not connected';
  document.getElementById('mqtt_status').textContent=d.mqtt_enabled===false?'Disabled':(d.mqtt_host?(d.mqtt_connected?'Connected to '+d.mqtt_host:'Configured · offline'):'Not set');
  document.getElementById('ap_enabled').checked=!!d.ap_enabled;
  document.getElementById('ap_name').value=d.ap_name||'';
  document.getElementById('ap_open').checked=!d.ap_password_configured;
  document.getElementById('mqtt_enabled').checked=d.mqtt_enabled!==false;
  document.getElementById('mqtt_host').value=d.mqtt_host||'';
  document.getElementById('mqtt_port').value=d.mqtt_port||'1883';
  document.getElementById('mqtt_user').value=d.mqtt_user||'';
  document.getElementById('clock').textContent=d.time||'--:--';
  applyTheme(Number(d.theme||0));
}
async function saveAp(){
  if(!await saveSettings({ap_enabled:document.getElementById('ap_enabled').checked,ap_name:document.getElementById('ap_name').value,
    ap_pass:document.getElementById('ap_open').checked?'':document.getElementById('ap_pass').value}))return;
  alert('Saved');load();showConn('menu');
}
async function saveMqtt(){
  if(!await saveSettings({mqtt_enabled:document.getElementById('mqtt_enabled').checked,mqtt_host:document.getElementById('mqtt_host').value,
    mqtt_port:document.getElementById('mqtt_port').value,mqtt_user:document.getElementById('mqtt_user').value,
    mqtt_pass:document.getElementById('mqtt_pass').value}))return;
  alert('Saved');load();showConn('menu');
}
load();
</script>
</div><!--content-->
</div><!--main-->
</body></html>
)rawliteral";

const char SCHEDULE_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Schedule</h1>
<label class="row">Enable schedule <input type="checkbox" id="schedule_enabled"></label>
<div class="hint">Auto heats only inside a day's windows (24h). Boost always works. Up to 3 windows per day.</div>
<div id="days"></div>
<button class="save" onclick="save()">Save</button>
<div class="hint" id="status" style="margin-top:12px"></div>
</div><div class="footer"><div id="clock">--:--</div></div>
<script>
const DAY_NAMES=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
let entries=[];
function emptyDay(d){return {day:d,enabled:true,on:'05:00',off:'22:00'}}
function render(){
  const box=document.getElementById('days');box.innerHTML='';
  for(let d=0;d<7;d++){
    const dayEntries=entries.filter(e=>e.day===d);
    let html=`<div class="sec-title">${DAY_NAMES[d]}</div><div class="list">`;
    if(!dayEntries.length){
      html+=`<div class="row-static"><div class="row-meta"><strong>No windows</strong><small>Heating blocked if schedule on</small></div>
        <button type="button" class="save" style="width:auto;margin:0;padding:8px 12px" onclick="addEntry(${d})">Add</button></div>`;
    } else {
      dayEntries.forEach((e,i)=>{
        const idx=entries.indexOf(e);
        html+=`<div class="row-static" style="flex-wrap:wrap">
          <label class="row" style="margin:0;gap:8px"><input type="checkbox" ${e.enabled?'checked':''} onchange="entries[${idx}].enabled=this.checked"> On</label>
          <input type="time" value="${e.on||'05:00'}" onchange="entries[${idx}].on=this.value" style="width:auto">
          <span class="hint" style="margin:0">to</span>
          <input type="time" value="${e.off||'22:00'}" onchange="entries[${idx}].off=this.value" style="width:auto">
          <button type="button" style="margin-left:auto;background:none;border:none;color:#f87171;cursor:pointer;font-size:1.1rem" onclick="removeEntry(${idx})" title="Remove">✕</button>
        </div>`;
      });
      if(dayEntries.length<3){
        html+=`<a class="row-link" href="#" onclick="addEntry(${d});return false"><div class="row-meta"><strong>+ Add window</strong></div></a>`;
      }
    }
    html+='</div>';
    box.innerHTML+=html;
  }
}
function addEntry(d){
  if(entries.filter(e=>e.day===d).length>=3)return;
  entries.push(emptyDay(d));render();
}
function removeEntry(i){entries.splice(i,1);render()}
async function load(){
  const d=await(await fetch('/api/status')).json();
  document.getElementById('schedule_enabled').checked=!!d.schedule_enabled;
  entries=(d.schedule||[]).map(e=>({day:Number(e.day),enabled:!!e.enabled,on:e.on||'05:00',off:e.off||'22:00'}));
  // ensure each day visible even if empty
  render();
  document.getElementById('status').textContent=d.schedule_enabled?(d.schedule_active?'Currently IN heating window':'Currently OUTSIDE heating window'):'Schedule off';
  document.getElementById('clock').textContent=d.time||'--:--';applyTheme(Number(d.theme||0));
}
async function save(){
  if(!await saveSettings({schedule_enabled:document.getElementById('schedule_enabled').checked,schedule:entries}))return;
  alert('Saved');load();
}
load();setInterval(async()=>{const d=await(await fetch('/api/status')).json();
  document.getElementById('status').textContent=d.schedule_enabled?(d.schedule_active?'Currently IN heating window':'Currently OUTSIDE heating window'):'Schedule off';
  document.getElementById('clock').textContent=d.time||'--:--';},15000);
</script>
</div><!--content-->
</div><!--main-->
</body></html>
)rawliteral";

const char SETTINGS_BODY[] PROGMEM = R"rawliteral(
<div id="settings-menu" class="subview active">
  <div class="sec-title">Device settings</div>
  <div class="list">
    <a class="row-link" href="#" onclick="showSet('device');return false"><div class="row-ico">🏷️</div><div class="row-meta"><strong>Device name</strong><small id="sum_name">-</small></div><span class="chev">›</span></a>
    <a class="row-link" href="#" onclick="showSet('time');return false"><div class="row-ico">🌐</div><div class="row-meta"><strong>Location and time</strong><small id="sum_tz">Timezone</small></div><span class="chev">›</span></a>
    <a class="row-link" href="#" onclick="showSet('temp');return false"><div class="row-ico">🌡️</div><div class="row-meta"><strong>Temperature</strong><small>Setpoints, hysteresis, presets</small></div><span class="chev">›</span></a>
    <a class="row-link" href="#" onclick="showSet('appear');return false"><div class="row-ico">🎨</div><div class="row-meta"><strong>Appearance</strong><small id="sum_theme">Theme</small></div><span class="chev">›</span></a>
    <a class="row-link" href="/update"><div class="row-ico">💾</div><div class="row-meta"><strong>Firmware</strong><small id="sum_ver">Update & reset</small></div><span class="chev">›</span></a>
  </div>
</div>
<div id="set-device" class="subview">
  <button class="back-btn" onclick="showSet('menu')">‹ Back</button>
  <div class="card"><h1>Device name</h1>
  <label>Friendly name</label><input type="text" id="friendly_name" maxlength="39">
  <button class="save" onclick="saveDevice()">Save</button></div>
</div>
<div id="set-time" class="subview">
  <button class="back-btn" onclick="showSet('menu')">‹ Back</button>
  <div class="card"><h1>Location and time</h1>
  <label>Timezone offset (hours from UTC)</label><input type="number" id="tz" min="-12" max="14" step="1">
  <div class="hint">Example: SAST = 2 · Current time: <span id="time_now">--:--</span></div>
  <button class="save" onclick="saveTime()">Save</button></div>
</div>
<div id="set-temp" class="subview">
  <button class="back-btn" onclick="showSet('menu')">‹ Back</button>
  <div class="card"><h1>Temperature</h1>
  <label>Auto setpoint (°C)</label><input type="number" id="setpoint" min="30" max="100" step="0.5">
  <label>Boost temperature (°C)</label><input type="number" id="boost_temp" min="30" max="100" step="0.5">
  <label>Boost only if below (°C, 0=always)</label><input type="number" id="boost_min" min="0" max="100" step="0.5">
  <label>Hysteresis (°C under to re-on)</label><input type="number" id="hysteresis" min="0.5" max="20" step="0.5">
  <label>Home preset (°C)</label><input type="number" id="home_temp" min="30" max="100" step="0.5">
  <label>Away preset (°C)</label><input type="number" id="away_temp" min="30" max="100" step="0.5">
  <label>Eco preset (°C)</label><input type="number" id="eco_temp" min="30" max="100" step="0.5">
  <label>Min on time (seconds)</label><input type="number" id="min_on" min="5" max="600" step="1">
  <label>Min off time (seconds)</label><input type="number" id="min_off" min="5" max="600" step="1">
  <button class="save" onclick="saveTemp()">Save</button></div>
</div>
<div id="set-appear" class="subview">
  <button class="back-btn" onclick="showSet('menu')">‹ Back</button>
  <div class="card"><h1>Appearance</h1>
  <label>Theme</label><select id="theme"><option value="0">Blue</option><option value="1">Dark</option><option value="2">Light</option></select>
  <button class="save" onclick="saveAppear()">Save</button></div>
</div>
<div class="footer"><div id="clock">--:--</div></div>
<script>
function showSet(id){
  document.querySelectorAll('#settings-menu,#set-device,#set-time,#set-temp,#set-appear').forEach(e=>e.classList.remove('active'));
  document.getElementById(id==='menu'?'settings-menu':'set-'+id).classList.add('active');
}
const themeNames=['Blue','Dark','Light'];
async function load(){
  const d=await(await fetch('/api/status')).json();
  document.getElementById('friendly_name').value=d.friendly_name||'';
  document.getElementById('sum_name').textContent=d.friendly_name||'ElementIQ';
  document.getElementById('tz').value=d.tz??2;
  document.getElementById('sum_tz').textContent='UTC'+(d.tz>=0?'+':'')+d.tz;
  document.getElementById('time_now').textContent=d.time||'--:--';
  document.getElementById('setpoint').value=d.setpoint;
  document.getElementById('boost_temp').value=d.boost_temp;
  document.getElementById('boost_min').value=d.boost_min_temp??0;
  document.getElementById('hysteresis').value=d.hysteresis;
  document.getElementById('home_temp').value=d.home_temp;
  document.getElementById('away_temp').value=d.away_temp;
  document.getElementById('eco_temp').value=d.eco_temp;
  document.getElementById('min_on').value=Math.round((d.min_on_ms||0)/1000);
  document.getElementById('min_off').value=Math.round((d.min_off_ms||0)/1000);
  document.getElementById('theme').value=String(d.theme||0);
  document.getElementById('sum_theme').textContent=themeNames[d.theme||0]||'Blue';
  document.getElementById('sum_ver').textContent=d.version||'';
  document.getElementById('clock').textContent=d.time||'--:--';
  applyTheme(Number(d.theme||0));
}
async function saveDevice(){if(!await saveSettings({friendly_name:document.getElementById('friendly_name').value}))return;alert('Saved');load();showSet('menu')}
async function saveTime(){if(!await saveSettings({tz:parseInt(document.getElementById('tz').value)||0}))return;alert('Saved');load();showSet('menu')}
async function saveTemp(){if(!await saveSettings({
  setpoint:+document.getElementById('setpoint').value,boost_temp:+document.getElementById('boost_temp').value,
  boost_min_temp:+document.getElementById('boost_min').value,hysteresis:+document.getElementById('hysteresis').value,
  home_temp:+document.getElementById('home_temp').value,away_temp:+document.getElementById('away_temp').value,
  eco_temp:+document.getElementById('eco_temp').value,
  min_on_ms:+document.getElementById('min_on').value*1000,min_off_ms:+document.getElementById('min_off').value*1000
}))return;alert('Saved');load();showSet('menu')}
async function saveAppear(){const th=parseInt(document.getElementById('theme').value)||0;if(!await saveSettings({theme:th}))return;applyTheme(th);alert('Saved');load();showSet('menu')}
load();
</script>
</div><!--content-->
</div><!--main-->
</body></html>
)rawliteral";

const char UPDATE_BODY[] PROGMEM = R"rawliteral(
<div class="card"><h1>Update & reset</h1>
<p class="hint">Version: <strong id="ver">-</strong></p>
<p class="hint" id="ota_msg">-</p>
<p class="hint">Project: <a href="https://github.com/GeckoZA/ElementIQ" style="color:var(--primary)">github.com/GeckoZA/ElementIQ</a></p>
<label>GitHub owner</label><input type="text" id="github_owner" placeholder="GeckoZA">
<label>GitHub repo</label><input type="text" id="github_repo" placeholder="ElementIQ">
<div class="hint">Latest release must include asset <code>firmware.bin</code></div>
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
document.getElementById('github_owner').value=d.github_owner||'GeckoZA';document.getElementById('github_repo').value=d.github_repo||'ElementIQ';
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
</script>
</div><!--content-->
</div><!--main-->
</body></html>
)rawliteral";

void sendPage(AsyncWebServerRequest *req, const char* active, const char* body) {
  String navigation = navHtml(active);
  AsyncResponseStream *response = req->beginResponseStream("text/html");
  response->print(FPSTR(SHARED_HEAD));
  response->print(FPSTR(NAV_SCRIPT));
  response->print(navigation);
  response->print(FPSTR(body));
  // Close .content and .main opened by navHtml (page bodies end with </body></html>)
  // Bodies already contain their own scripts and closing tags.
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
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "settings", SETTINGS_BODY); });
  server.on("/schedule", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "schedule", SCHEDULE_BODY); });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, "update", UPDATE_BODY); });
  // Legacy redirects
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/settings"); });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/settings"); });
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *req) { req->redirect("/"); });
}

