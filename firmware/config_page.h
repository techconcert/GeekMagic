// The settings page (GET /config), one static block in flash, streamed in 400-byte pieces by config.cpp. It contains no
// value taken from the clock: the JavaScript reads /config.json and fills the fields through .value and .textContent, so
// nothing from the clock or the owner is ever put into HTML (no escaping needed, no String of the page).
// test/config_page_server.py serves this very text (it extracts the block between the HTML raw-string markers).
// City search runs in the owner's browser against Open-Meteo's geocoding API over HTTPS; the clock never sees the name.
#pragma once
#include <Arduino.h>

static const char CONFIG_PAGE[] PROGMEM = R"HTML(<style>
.r{display:flex;gap:8px}.r input{flex:1}#res button{display:block;width:100%;text-align:left}label{display:block;margin:6px 0}
label.c{display:inline-block;margin:4px 12px 4px 0}input[type=time]{width:auto;margin-left:8px}input[type=radio]{width:auto;margin:0 6px 0 0}
small a{background:none;color:#e89a3c;padding:0}h3{margin:0}[hidden]{display:none!important}#msg{margin:10px 0;min-height:1.3em}
</style>
<h2>Clock settings</h2>
<form id="f" method="post" action="/config"><input type="hidden" name="f" value="1">
<div class="box"><h3>Weather</h3>
<label class="c"><input type="checkbox" name="wx" value="1" id="wx"> Use live weather</label>
<div class="r"><input id="q" maxlength="40" placeholder="Search for a city"><button type="button" id="go">Search</button></div>
<div id="res"></div>
<label>Latitude<input name="lat" id="lat" maxlength="14" placeholder="38.72"></label>
<label>Longitude<input name="lon" id="lon" maxlength="14" placeholder="-9.14"></label>
<label>Place name<input name="place" id="place" maxlength="23"></label>
<label class="c"><input type="radio" name="unit" value="C" id="uc">Celsius</label><label class="c"><input type="radio" name="unit" value="F" id="uf">Fahrenheit</label>
<p id="now"></p><p id="wxs"><small></small></p>
<button type="button" data-u="/config/retry" id="retry" hidden>Try weather again</button>
<p><small>Weather data by <a href="https://open-meteo.com/">Open-Meteo.com</a> (CC BY 4.0), every 30 minutes, place rounded to about 1 km.</small></p></div>
<div class="box"><h3>Sleep</h3>
<label class="c"><input type="checkbox" name="sl" value="1" id="sl"> Use sleep schedule</label>
<label class="c">Sleep time<input type="time" name="st" id="st"></label><label class="c">Wake time<input type="time" name="wt" id="wt"></label>
<p><button type="button" data-u="/config/night" data-c="wake">Wake now</button> <button type="button" data-u="/config/night" data-c="sleep">Sleep now</button></p></div>
<div class="box"><small id="stor"></small></div>
<p><button>Save settings</button> <a href="/">Back</a></p><div id="msg" role="status"></div></form>
<script>
const $=i=>document.getElementById(i),f=$('f'),W=['Clear','Cloudy','Rain','Storm','Fog','Snow','Windy'];
function say(t,ok){$('msg').textContent=t;$('msg').className=ok?'ok':'warn'}
function stat(j){let t='',r=j.reading,c;if(r){c=r.t10/10;if($('uf').checked)c=c*9/5+32;t='Now: '+W[r.wx]+', '+c.toFixed(1)+($('uf').checked?' F':' C')}
 else if(j.weather)t='No weather reading yet.';
 $('now').textContent=t;$('wxs').firstChild.textContent=j.status;$('retry').hidden=!j.retry}
function fill(j){$('wx').checked=!!j.weather;$('lat').value=j.lat;$('lon').value=j.lon;$('place').value=j.place;
 $(j.unit=='F'?'uf':'uc').checked=true;$('sl').checked=!!j.sleep;$('st').value=j.sleep_time;$('wt').value=j.wake_time;
 $('stor').textContent=j.storage=='rtc'?"Kept in the clock's memory: survives restarts and updates, not unplugging.":'Stored permanently.';stat(j)}
async function load(all){try{const j=await(await fetch('/config.json')).json();all?fill(j):stat(j)}catch(e){say('Could not read the settings.',0)}}
function clean(s){return s.normalize('NFD').replace(/[̀-ͯ]/g,'').replace(/[^A-Za-z0-9 ,.'-]/g,'').trim().slice(0,23)}
async function find(){const q=$('q').value.trim(),r=$('res');if(!q)return;r.textContent='Searching...';
 try{const x=await fetch('https://geocoding-api.open-meteo.com/v1/search?name='+encodeURIComponent(q)+'&count=5&language=en&format=json');
  if(!x.ok)throw 0;const j=await x.json();r.textContent='';
  if(!j.results)r.textContent='No place found.';
  else j.results.forEach(p=>{const b=document.createElement('button'),n=[p.name,p.admin1,p.country].filter(Boolean).join(', ');
   b.type='button';b.textContent=n+' ('+p.latitude.toFixed(2)+', '+p.longitude.toFixed(2)+')';
   b.onclick=()=>{$('lat').value=p.latitude.toFixed(2);$('lon').value=p.longitude.toFixed(2);
    let s=clean(p.name+(p.country?', '+p.country:''));if(s.length>=23)s=clean(p.name);$('place').value=s;$('wx').checked=true;
    r.textContent='Chosen: '+n+'. Press Save settings.'};r.appendChild(b)})
 }catch(e){r.textContent='No internet on this device. Type latitude and longitude, or open this page over your home WiFi.'}}
$('go').onclick=find;$('q').onkeydown=e=>{if(e.key=='Enter'){e.preventDefault();find()}};
async function post(u,p){p.set('j','1');say('Working...',1);
 try{const x=await fetch(u,{method:'POST',body:p});say(await x.text(),x.ok);if(x.ok)setTimeout(()=>load(u=='/config'),700)}
 catch(e){say('The clock did not answer.',0)}}
f.onsubmit=e=>{e.preventDefault();post('/config',new URLSearchParams(new FormData(f)))};
document.querySelectorAll('[data-u]').forEach(b=>b.onclick=()=>post(b.dataset.u,new URLSearchParams(b.dataset.c?{cmd:b.dataset.c}:{})));
load(1);
</script>)HTML";
