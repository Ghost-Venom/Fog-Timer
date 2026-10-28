// Phone/desktop control page served at http://<device-ip>/ (or http://fogtimer.local/)
#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="Fog Timer">
<meta name="theme-color" content="#111111">
<title>Fog Timer</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee}
main{max-width:520px;margin:auto;padding:12px}
.card{background:#1d1d20;border-radius:14px;padding:14px;margin-bottom:12px}
h2{font-size:13px;font-weight:600;color:#999;margin:0 0 10px;text-transform:uppercase;letter-spacing:.05em}
#status{display:flex;justify-content:space-between;align-items:center;padding:12px 14px;border-radius:12px;background:#444;font-size:24px;font-weight:700}
#prog{height:8px;background:#333;border-radius:4px;margin-top:8px;overflow:hidden}
#prog div{height:100%;width:0;background:#fffa;transition:width .9s linear}
.sub{color:#999;font-size:14px;margin-top:8px}
.muted{color:#999;font-size:14px}
button{font:inherit;border:0;border-radius:10px;padding:12px;color:#fff;background:#3a3a40;cursor:pointer}
button:active{opacity:.7}
.seg{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.seg button.on{background:#2f7de1}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}
.fog{background:#d9691a;font-size:20px;font-weight:700;padding:18px}
.stop{background:#a8282b;font-size:20px;font-weight:700;padding:18px}
.adj{display:grid;grid-template-columns:64px 56px 1fr 56px;gap:8px;align-items:center;margin:6px 0}
.adj span{text-align:center;font-size:22px;font-weight:600}
.slot{border-top:1px solid #333;padding:10px 0}
.days{display:grid;grid-template-columns:repeat(7,1fr);gap:4px;margin-bottom:8px}
.days button{padding:8px 0;font-size:13px}
.days button.on{background:#7a4fd0}
.times{display:grid;grid-template-columns:1fr auto 1fr auto auto;gap:6px;align-items:center}
input[type=time],input[type=text],input[type=password]{font:inherit;background:#2a2a2e;color:#eee;border:1px solid #444;border-radius:8px;padding:8px;width:100%}
label.chk{display:flex;gap:4px;align-items:center;font-size:14px}
#conn{color:#f0a030;text-align:center;font-size:14px;min-height:1.2em}
</style></head><body><main>
<div id="conn"></div>

<div class="card">
  <div id="status"><span id="st">...</span><span id="cd"></span></div>
  <div id="prog"><div></div></div>
  <div class="sub" id="info"></div>
</div>

<div class="card">
  <h2>Mode</h2>
  <div class="seg"><button data-m="0">Off</button><button data-m="1">On</button><button data-m="2">Schedule</button></div>
  <div class="grid2"><button class="fog" onclick="cmd({a:'fog'})">Fog now</button><button class="stop" onclick="cmd({a:'stop'})">Stop</button></div>
</div>

<div class="card">
  <h2>Timing</h2>
  <div class="adj"><b>Blast</b><button onclick="adj('blast',-1)">&minus;</button><span id="blast"></span><button onclick="adj('blast',1)">+</button></div>
  <div class="adj"><b>Pause</b><button onclick="adj('pause',-1)">&minus;</button><span id="pause"></span><button onclick="adj('pause',1)">+</button></div>
</div>

<div class="card">
  <h2>Schedule</h2>
  <div class="muted">In Schedule mode, the blast/pause cycle runs only during these windows. An end time earlier than the start runs past midnight.</div>
  <div id="slots"></div>
  <div class="grid2"><button onclick="addSlot()">+ Add window</button><button style="background:#2f7de1" onclick="saveSched()">Save schedule</button></div>
  <div class="muted" id="schedMsg" style="margin-top:8px"></div>
</div>

<div class="card">
  <h2>Remote codes</h2>
  <div class="muted" id="codes"></div>
  <div class="grid2"><button onclick="cmd({a:'learn',v:1})">Learn ON</button><button onclick="cmd({a:'learn',v:2})">Learn OFF</button>
  <button onclick="cmd({a:'test',v:1})">Test ON</button><button onclick="cmd({a:'test',v:2})">Test OFF</button></div>
  <div class="muted" id="learnMsg" style="margin-top:8px"></div>
</div>

<div class="card">
  <h2>WiFi</h2>
  <div class="muted" id="wifiInfo"></div>
  <input id="ssid" type="text" placeholder="Network name" style="margin-top:8px" autocapitalize="off">
  <input id="pass" type="password" placeholder="Password" style="margin-top:8px">
  <button style="margin-top:8px;width:100%" onclick="saveWifi()">Save and restart</button>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const DAYS=['S','M','T','W','T','F','S'];
let S=null,slots=[],loaded=false;

function dur(s,tenths){s=+s;if(s>=60){const m=Math.floor(s/60),r=Math.floor(s%60);return m+':'+String(r).padStart(2,'0')}return tenths?s.toFixed(1)+' s':Math.round(s)+' s'}
function hm(m){return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0')}
function mins(v){const[h,m]=v.split(':').map(Number);return h*60+m}

async function cmd(p){try{const r=await fetch('/api/cmd',{method:'POST',body:new URLSearchParams(p)});S=await r.json();render()}catch(e){}}
async function poll(){try{const r=await fetch('/api/state');S=await r.json();$('conn').textContent='';render()}catch(e){$('conn').textContent='Not connected to the Fog Timer'}}

function render(){
  if(!S)return;
  let label,col;
  if(S.fogging){label='Fogging';col='#c8322a'}
  else if(S.active){label='Waiting';col='#1e64c8'}
  else if(S.mode==2){label=S.time?'Scheduled':'No clock';col='#6e46be'}
  else{label='Off';col='#444'}
  const run=S.fogging||S.active;
  $('st').textContent=label;$('status').style.background=col;
  $('cd').textContent=run?dur(Math.ceil(S.left)):'';
  document.querySelector('#prog div').style.width=run?(100*(1-S.left/S.len))+'%':'0';
  let info=S.time||'Clock not set yet';
  if(S.mode==2&&!S.active&&S.next)info+=' · next run '+S.next;
  $('info').textContent=info;
  document.querySelectorAll('.seg button').forEach(b=>b.classList.toggle('on',+b.dataset.m===S.mode));
  $('blast').textContent=dur(S.blast,true);
  $('pause').textContent=dur(S.pause,false);
  const c=x=>x.v?x.v+' ('+x.b+' bits, protocol '+x.p+', '+x.l+' µs)':'not learned';
  $('codes').textContent='ON: '+c(S.on)+' · OFF: '+c(S.off);
  $('learnMsg').textContent=S.learn?'Hold the remote near the receiver and press its '+(S.learn==1?'ON':'OFF')+' button ('+S.learnLeft+' s)':(S.last.v?'Last code heard: '+S.last.v:'');
  $('wifiInfo').textContent='Address: '+S.ip+(S.ssid?' · network: '+S.ssid:'');
  if(!loaded){slots=S.slots.map(a=>({d:a[0],s:a[1],e:a[2],en:a[3]}));drawSlots();loaded=true}
}

function drawSlots(){
  $('slots').innerHTML=slots.map((s,i)=>
    '<div class="slot"><div class="days">'+DAYS.map((d,j)=>'<button class="'+((s.d>>j)&1?'on':'')+'" onclick="tog('+i+','+j+')">'+d+'</button>').join('')+'</div>'+
    '<div class="times"><input type="time" value="'+hm(s.s)+'" onchange="slots['+i+'].s=mins(this.value)"><span>to</span>'+
    '<input type="time" value="'+hm(s.e)+'" onchange="slots['+i+'].e=mins(this.value)">'+
    '<label class="chk"><input type="checkbox" '+(s.en?'checked':'')+' onchange="slots['+i+'].en=this.checked?1:0">On</label>'+
    '<button onclick="slots.splice('+i+',1);drawSlots()" aria-label="Delete">&times;</button></div></div>').join('')
    ||'<div class="muted" style="padding:10px 0">No windows yet.</div>';
}
function tog(i,j){slots[i].d^=1<<j;drawSlots()}
function addSlot(){if(slots.length>=8)return;slots.push({d:127,s:18*60,e:22*60,en:1});drawSlots()}
async function saveSched(){
  await cmd({a:'sched',s:slots.map(s=>[s.d,s.s,s.e,s.en].join(',')).join(';')});
  $('schedMsg').textContent='Saved.';setTimeout(()=>$('schedMsg').textContent='',2500);
}

function adj(k,d){
  if(!S)return;let v=S[k];
  if(k=='blast')v=Math.min(30,Math.max(.5,v+.5*d));
  else{const st=d>0?(v<60?5:v<300?15:60):(v<=60?5:v<=300?15:60);v=Math.min(1800,Math.max(5,v+d*st))}
  S[k]=v;render();cmd({a:'set',[k]:v});
}

function saveWifi(){
  const ssid=$('ssid').value.trim();
  if(!ssid){$('ssid').focus();return}
  if(!confirm('Save WiFi settings and restart the Fog Timer?'))return;
  fetch('/api/cmd',{method:'POST',body:new URLSearchParams({a:'wifi',ssid,pass:$('pass').value})}).catch(()=>{});
  $('conn').textContent='Restarting. Reconnect your phone to home WiFi and open the address shown on the screen.';
}

document.querySelectorAll('.seg button').forEach(b=>b.onclick=()=>cmd({a:'mode',v:b.dataset.m}));
poll();setInterval(poll,1000);
</script></body></html>)rawliteral";
