#include "web.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include "alarm.h"
#include "board/board.h"
#include "mc_clock.h"
#include "settings.h"
#include "timekeeping.h"

#define HOSTNAME       "clock"
#define PIN_MAX_FAILS  5
#define PIN_LOCK_MS    60000

static WebServer server(80);
static bool     started   = false;
static bool     mdns_up   = false;
static uint8_t  pin_fails = 0;
static uint32_t lock_start = 0;

static const char* const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock</title>
<style>
:root{color-scheme:dark;--bg:#101820;--card:#1b2630;--fg:#eef2f5;--dim:#9aa7b2;--accent:#4caf3a;--bad:#ff5a4a}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;max-width:560px;margin:auto}
h1{font-size:22px;margin:4px 0 12px}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:14px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.dim{color:var(--dim);font-size:14px}
input,button{font:inherit;color:var(--fg);background:#26343f;border:1px solid #34444f;border-radius:8px;padding:8px 10px}
input[type=time]{font-size:22px;padding:6px 8px}
input[type=range]{flex:1;padding:0}
button{background:var(--accent);border:0;font-weight:600}
button.sec{background:#34444f}
button.del{background:transparent;color:var(--bad);border:1px solid var(--bad)}
.days{display:flex;gap:4px;margin:10px 0}
.days button{flex:1;padding:8px 0;background:#26343f;font-weight:500}
.days button.on{background:var(--accent)}
.alarm{border-top:1px solid #34444f;padding-top:12px;margin-top:12px}
.alarm:first-child{border:0;margin:0;padding:0}
.label{flex:1;min-width:120px}
#msg{min-height:22px;margin:0 0 10px;font-weight:600}
.bad{color:var(--bad)}.ok{color:var(--accent)}
label.sw{display:flex;align-items:center;gap:6px}
.set{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;padding:8px 0;border-top:1px solid #34444f}
select{font:inherit;color:var(--fg);background:#26343f;border:1px solid #34444f;border-radius:8px;padding:8px}
#nt input[type=time]{font-size:16px}
input[type=checkbox]{width:22px;height:22px}
</style></head><body>
<h1>Clock</h1>
<p id="msg"></p>
<div class="card"><div class="row"><b id="now">--:--</b><span class="dim" id="state"></span></div>
<div class="row" style="margin-top:8px"><span id="next" class="dim"></span><span style="flex:1"></span>
<button id="skipb" class="sec" onclick="skip()"></button></div></div>
<div class="card"><b>Alarms</b><div id="list"></div>
<div class="row" style="margin-top:12px"><button class="sec" id="add" onclick="addAlarm()">+ Add alarm</button></div></div>
<div class="card"><b>Volume</b> <span id="volv" class="dim"></span>
<div class="row" style="margin-top:8px"><input type="range" id="vol" min="0" max="100" step="5" oninput="volv.textContent=this.value+'%'">
<button class="sec" onclick="post('/api/beep',{})">Test</button><button onclick="post('/api/volume',{v:vol.value})">Save</button></div>
<p class="dim">Test beeps at the saved volume. The alarm starts at 15% of it and gets louder over a minute.</p></div>
<div class="card"><b>Settings</b>
<div class="set"><span>Alarm gets louder over</span><select id="ramp"><option value="0">no ramp</option><option value="30">30 s</option><option value="60">1 min</option><option value="120">2 min</option><option value="300">5 min</option></select></div>
<div class="set"><span>Snooze length</span><select id="snz"></select></div>
<div class="set"><span>Snoozes per alarm</span><select id="snzn"></select></div>
<div class="set"><span>Problems up to</span><select id="mmax"></select></div>
<div class="set"><span>Problems to solve</span><select id="mcnt"></select></div>
<div class="set"><span>Faces</span><span id="faces" class="row"></span></div>
<div class="set"><span>Day brightness <span id="brv" class="dim"></span></span><input type="range" id="br" min="10" max="100" step="5" oninput="brv.textContent=this.value+'%'" style="max-width:160px"></div>
<div class="set"><label class="sw"><input type="checkbox" id="non" onchange="nt.style.opacity=this.checked?1:.4">Screen off at night</label>
<span id="nt" class="row"><input type="time" id="ns"> to <input type="time" id="ne"></span></div>
<div class="row" style="margin-top:12px"><span style="flex:1"></span><button onclick="saveSettings()">Save settings</button></div></div>
<div class="card"><b>Parent PIN</b>
<div class="row" style="margin-top:8px"><input id="pin" type="password" inputmode="numeric" maxlength="8" placeholder="PIN" style="width:120px">
<button class="sec" onclick="savePin()">Remember</button></div>
<p class="dim" id="pinhint">Needed to save changes. Remembered on this phone only.</p></div>
<script>
const D=['S','M','T','W','T','F','S'];let S=null,SK=false,dirty=false;   // dirty: unsaved edits, keep them on refresh
document.addEventListener('input',e=>{if(e.target.id!='pin')dirty=true});
function opts(el,from,to,f){for(let v=from;v<=to;v++){const o=document.createElement('option');o.value=v;o.textContent=f(v);el.appendChild(o)}}
opts(snz,1,30,v=>v+' min');opts(snzn,0,5,v=>v?v:'none');opts(mmax,5,12,v=>v+' x '+v);opts(mcnt,1,5,v=>v);
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
const mins=t=>{const[h,m]=t.split(':');return h*60+ +m};
function showSettings(s,names){ramp.value=s.ramp;snz.value=s.snooze;snzn.value=s.snoozes;mmax.value=s.math_max;mcnt.value=s.math_count;
 br.value=s.brightness;brv.textContent=s.brightness+'%';non.checked=s.night_start!=s.night_end;
 ns.value=hm(non.checked?s.night_start:1320);ne.value=hm(non.checked?s.night_end:360);nt.style.opacity=non.checked?1:.4;
 faces.innerHTML='';names.forEach((n,k)=>{const l=document.createElement('label');l.className='sw';
  l.innerHTML='<input type="checkbox">'+n;l.firstChild.checked=!!(s.faces&(1<<k));faces.appendChild(l)})}
function saveSettings(){let f=0;faces.querySelectorAll('input').forEach((c,k)=>{if(c.checked)f|=1<<k});
 if(!f)return say('Pick at least one face','bad');
 const st=non.checked?mins(ns.value):0,en=non.checked?mins(ne.value):0;
 if(non.checked&&st==en)return say('Night start and end are the same','bad');
 post('/api/settings',{ramp:ramp.value,snooze:snz.value,snoozes:snzn.value,math_max:mmax.value,math_count:mcnt.value,faces:f,brightness:br.value,night_start:st,night_end:en})}
pin.value=localStorage.getItem('pin')||'';
function savePin(){localStorage.setItem('pin',pin.value);say('PIN remembered','ok')}
function say(t,c){msg.textContent=t;msg.className=c||''}
async function load(){try{const r=await fetch('/api/state');show(await r.json())}catch(e){say('Clock not reachable','bad')}}
async function post(p,a){a.pin=pin.value;
 const r=await fetch(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(a)});
 if(r.ok){dirty=false;show(await r.json());say('Saved','ok');setTimeout(()=>say(''),2000)}else say(await r.text(),'bad')}
function show(s){now.textContent=s.valid?s.time:'--:--';
 state.textContent=s.ringing?'ringing':s.snoozed?'snoozed':'';
 next.textContent=s.next?('Next: '+s.next+(s.skip?' (skipped)':'')):'No alarm set';
 skipb.textContent=s.skip?'Undo skip':'Skip next';skipb.style.display=s.next?'':'none';SK=s.skip;
 if(dirty)return;S=s;   // below: editable parts, left alone while there are unsaved edits
 vol.value=s.volume;volv.textContent=s.volume+'%';
 pinhint.textContent=s.pin_set?'Needed to save changes. Remembered on this phone only.':'No PIN set on the clock yet: set one over USB serial (pin 1234).';
 add.style.display=s.alarms.length<s.max?'':'none';
 list.innerHTML='';s.alarms.forEach((a,i)=>list.appendChild(row(a,i)));showSettings(s.settings,s.faces)}
function row(a,i){const d=document.createElement('div');d.className='alarm';
 d.innerHTML='<div class="row"><input type="time" class="t"><label class="sw"><input type="checkbox" class="on">On</label></div>'+
 '<div class="days"></div><div class="row"><input class="label" maxlength="15" placeholder="Label"><button class="del">Delete</button><button class="save">Save</button></div>';
 const q=c=>d.querySelector(c);q('.t').value=a.time;q('.on').checked=!!a.on;q('.label').value=a.label;let days=a.days;
 D.forEach((n,k)=>{const b=document.createElement('button');b.textContent=n;b.className=days&(1<<k)?'on':'';
  b.onclick=()=>{days^=1<<k;b.className=days&(1<<k)?'on':'';dirty=true};q('.days').appendChild(b)});
 q('.save').onclick=()=>post('/api/alarm',{i:i,time:q('.t').value,days:days,on:q('.on').checked?1:0,label:q('.label').value});
 q('.del').onclick=()=>{if(i<0)load();else if(confirm('Delete this alarm?'))post('/api/delete',{i:i})};return d}
function addAlarm(){dirty=true;S.alarms.push({time:'07:00',days:62,on:1,label:''});const r=row(S.alarms.at(-1),-1);list.appendChild(r);add.style.display='none'}
function skip(){post('/api/skip',{on:SK?0:1})}
load();setInterval(load,30000);
</script></body></html>
)HTML";

static void json_escape(String& out, const char* s) {
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += c;
    }
}

static void send_state(void) {
    char buf[64];
    String j;
    j.reserve(1024);
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "{\"time\":\"%02d:%02d\",\"valid\":%s,", t.tm_hour, t.tm_min,
             time_is_valid() ? "true" : "false");
    j += buf;
    snprintf(buf, sizeof(buf), "\"ringing\":%s,\"snoozed\":%s,\"skip\":%s,\"volume\":%u,",
             alarm_is_ringing() ? "true" : "false", alarm_snooze_until() ? "true" : "false",
             alarm_skip_is_set() ? "true" : "false", settings_volume());
    j += buf;
    snprintf(buf, sizeof(buf), "\"pin_set\":%s,\"max\":%d,\"next\":", settings_pin_is_set() ? "true" : "false",
             ALARM_MAX);
    j += buf;
    int32_t when;
    int idx;
    alarm_t a;
    if (alarm_next(&when, &idx) && alarm_get(idx, &a)) {
        time_t ts = (time_t)when * 60;
        localtime_r(&ts, &t);
        snprintf(buf, sizeof(buf), "\"%s %02d:%02d ", DAY[t.tm_wday], t.tm_hour, t.tm_min);
        j += buf;
        json_escape(j, a.label);
        j += "\"";
    } else {
        j += "null";
    }
    j += ",\"alarms\":[";
    for (int i = 0; i < alarm_count(); i++) {
        alarm_get(i, &a);
        snprintf(buf, sizeof(buf), "%s{\"on\":%d,\"time\":\"%02d:%02d\",\"days\":%d,\"label\":\"", i ? "," : "",
                 a.enabled, a.minutes / 60, a.minutes % 60, a.days);
        j += buf;
        json_escape(j, a.label);
        j += "\"}";
    }
    const settings_t* st = settings_get();
    snprintf(buf, sizeof(buf), "],\"settings\":{\"ramp\":%u,\"snooze\":%u,\"snoozes\":%u,", st->ramp_s,
             st->snooze_min, st->snoozes);
    j += buf;
    snprintf(buf, sizeof(buf), "\"math_max\":%u,\"math_count\":%u,\"faces\":%u,\"brightness\":%u,",
             st->math_max, st->math_count, st->faces, st->brightness);
    j += buf;
    snprintf(buf, sizeof(buf), "\"night_start\":%u,\"night_end\":%u},\"faces\":[", st->night_start,
             st->night_end);
    j += buf;
    for (int i = 0; i < mc_clock_face_count(); i++) {
        j += i ? ",\"" : "\"";
        json_escape(j, mc_clock_face_name(i));
        j += "\"";
    }
    j += "]}";
    server.send(200, "application/json", j);
}

// Every change needs the PIN. Too many wrong tries lock changes for PIN_LOCK_MS.
static bool pin_ok(void) {
    if (pin_fails >= PIN_MAX_FAILS) {
        if (millis() - lock_start < PIN_LOCK_MS) {
            server.send(429, "text/plain", "Too many wrong PINs. Wait a minute.");
            return false;
        }
        pin_fails = 0;
    }
    if (!settings_pin_is_set()) {
        server.send(403, "text/plain", "No PIN set on the clock yet (USB serial: pin 1234)");
        return false;
    }
    if (!settings_check_pin(server.arg("pin").c_str())) {
        if (++pin_fails >= PIN_MAX_FAILS) lock_start = millis();
        Serial.printf("web: wrong PIN (%d)\n", pin_fails);
        server.send(403, "text/plain", "Wrong PIN");
        return false;
    }
    pin_fails = 0;
    return true;
}

static void bad(const char* why) { server.send(400, "text/plain", why); }

// Copies at most ALARM_LABEL_LEN - 1 bytes without cutting a UTF-8 character in half.
static void copy_label(char* dst, const char* src) {
    size_t n = strlen(src);
    if (n > ALARM_LABEL_LEN - 1) {
        n = ALARM_LABEL_LEN - 1;
        while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void handle_alarm(void) {
    if (!pin_ok()) return;
    int i = server.arg("i").toInt();
    int h, m;
    if (sscanf(server.arg("time").c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
        return bad("Bad time");
    int days = server.arg("days").toInt();
    if (days < 0 || days > 0x7F) return bad("Bad days");
    alarm_t a = {};
    a.enabled = server.arg("on").toInt() ? 1 : 0;
    a.days = (uint8_t)days;
    a.minutes = (int16_t)(h * 60 + m);
    copy_label(a.label, server.arg("label").c_str());
    bool ok = (i < 0) ? alarm_add(&a) >= 0 : alarm_update(i, &a);
    if (!ok) return bad(i < 0 ? "Alarm list is full" : "No such alarm");
    Serial.printf("web: alarm %s %02d:%02d days 0x%02x \"%s\" %s\n", i < 0 ? "added" : "updated", h, m,
                  days, a.label, a.enabled ? "on" : "off");
    send_state();
}

static void handle_delete(void) {
    if (!pin_ok()) return;
    if (!alarm_remove(server.arg("i").toInt())) return bad("No such alarm");
    Serial.println("web: alarm deleted");
    send_state();
}

static void handle_skip(void) {
    if (!pin_ok()) return;
    bool on = server.arg("on").toInt() != 0;
    if (!alarm_skip_next(on)) return bad("No next alarm to skip");
    Serial.printf("web: skip next %s\n", on ? "on" : "off");
    send_state();
}

static void handle_volume(void) {
    if (!pin_ok()) return;
    int v = server.arg("v").toInt();
    if (v < 0 || v > 100) return bad("Bad volume");
    settings_set_volume((uint8_t)v);
    Serial.printf("web: volume %d\n", v);
    send_state();
}

static void handle_settings(void) {
    if (!pin_ok()) return;
    settings_t st = *settings_get();
    st.ramp_s = (uint16_t)server.arg("ramp").toInt();
    st.snooze_min = (uint8_t)server.arg("snooze").toInt();
    st.snoozes = (uint8_t)server.arg("snoozes").toInt();
    st.math_max = (uint8_t)server.arg("math_max").toInt();
    st.math_count = (uint8_t)server.arg("math_count").toInt();
    st.faces = (uint8_t)server.arg("faces").toInt();
    st.brightness = (uint8_t)server.arg("brightness").toInt();
    st.night_start = (uint16_t)server.arg("night_start").toInt();
    st.night_end = (uint16_t)server.arg("night_end").toInt();
    if (!settings_set(&st)) return bad("A setting is out of range");
    Serial.print("web: ");
    settings_print();
    send_state();
}

static void handle_beep(void) {
    if (!pin_ok()) return;
    if (!alarm_is_ringing()) sound_beep(880, 400);
    send_state();
}

static void start(void) {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", PAGE); });
    server.on("/api/state", HTTP_GET, send_state);
    server.on("/api/alarm", HTTP_POST, handle_alarm);
    server.on("/api/delete", HTTP_POST, handle_delete);
    server.on("/api/skip", HTTP_POST, handle_skip);
    server.on("/api/volume", HTTP_POST, handle_volume);
    server.on("/api/beep", HTTP_POST, handle_beep);
    server.on("/api/settings", HTTP_POST, handle_settings);
    server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
    server.begin();
    started = true;
}

void web_tick(void) {
    // The network stack exists only after Wi-Fi started (not in a CLOCK_NO_WIFI build).
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !started) start();
    if (connected && !mdns_up) {
        mdns_up = MDNS.begin(HOSTNAME);
        if (mdns_up) MDNS.addService("http", "tcp", 80);
        Serial.printf("web: http://%s.local or http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());
    } else if (!connected && mdns_up) {
        MDNS.end();                     // started again with the new address after reconnecting
        mdns_up = false;
    }
    if (started) server.handleClient();
}

void web_print_status(void) {
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("web: http://%s.local or http://%s, PIN %s\n", HOSTNAME,
                      WiFi.localIP().toString().c_str(), settings_pin_is_set() ? "set" : "NOT SET");
    else
        Serial.println("web: Wi-Fi not connected");
}
