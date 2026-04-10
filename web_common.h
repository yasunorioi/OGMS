#pragma once

// ============================================================
// WebUI — Common CSS + Helper
// ============================================================
static const char COMMON_CSS[] PROGMEM = R"CSS(
body{font-family:sans-serif;margin:16px;background:#0f1011;color:#f7f8f8}
h2{color:#5e6ad2}h3{color:#d0d6e0}
.sec{background:#191a1b;border-radius:6px;padding:12px;margin:8px 0}
table{border-collapse:collapse;width:100%;margin:6px 0}
th,td{border:1px solid #2e2e2e;padding:5px 8px}
th{background:#191a1b;color:#d0d6e0}
.on{color:#66bb6a;font-weight:bold}.off{color:#ef5350}
input[type=number]{padding:3px;background:#1a1a1f;color:#eee;border:1px solid #3e3e44;border-radius:3px}
input[type=submit]{background:#1976d2;color:#fff;border:none;padding:8px 20px;border-radius:4px;cursor:pointer;margin-top:10px}
input[type=submit]:disabled{opacity:0.5;cursor:not-allowed}
select{padding:3px;background:#1a1a1f;color:#eee;border:1px solid #3e3e44;border-radius:3px}
a{color:#d0d6e0}.note{color:#8a8f98;font-size:0.85em}
.msg{margin:8px 0;padding:6px;font-weight:bold}
.badge-ok{background:#1b5e20;padding:2px 6px;border-radius:3px;font-size:0.85em}
.badge-ng{background:#b71c1c;padding:2px 6px;border-radius:3px;font-size:0.85em}
@media(max-width:600px){
  table{display:block;overflow-x:auto;-webkit-overflow-scrolling:touch}
  input[type=number],input[type=text],select{width:100%;box-sizing:border-box}
  body{margin:8px}
}
)CSS";

extern int g_language;

void printNavLinks(WiFiClient& client) {
  client.printf("<p>"
    "<a href='/'>%s</a> | "
    "<a href='/ccm'>%s</a> | "
    "<a href='/greenhouse'>%s</a> | "
    "<a href='/irrigation'>%s</a> | "
    "<a href='/protection'>%s</a> | "
    "<a href='/config'>%s</a> | "
    "<a href='/ota'>%s</a> | "
    "<a href='/api/language?lang=%s'>%s</a></p>\n",
    g_language==1 ? "ダッシュボード"  : "Dashboard",
    g_language==1 ? "CCM割当"         : "CCM",
    g_language==1 ? "温室制御"         : "Greenhouse",
    g_language==1 ? "灌水"             : "Irrigation",
    g_language==1 ? "保護制御"         : "Protection",
    g_language==1 ? "ネットワーク"     : "Network",
    g_language==1 ? "FW更新"           : "FW",
    g_language==0 ? "jp" : "en",
    g_language==0 ? "日本語"           : "English");
}

static const char FORM_JS[] PROGMEM = R"JS(
function submitForm(form,btn){
  btn.disabled=true;btn.dataset.orig=btn.value;btn.value=LANG==='jp'?'保存中...':'Saving...';
  var msg=form.querySelector('.msg');if(msg)msg.textContent='';
  fetch(form.action,{method:'POST',body:new URLSearchParams(new FormData(form))})
  .then(function(r){return r.json();})
  .then(function(d){
    btn.disabled=false;btn.value=btn.dataset.orig;
    if(!msg){msg=document.createElement('div');msg.className='msg';form.appendChild(msg);}
    if(d.ok){
      msg.style.color='#66bb6a';msg.textContent=LANG==='jp'?'\u2713 保存しました':'\u2713 Saved';
      if(d.reboot){msg.textContent=LANG==='jp'?'\u2713 保存しました。再起動中...':'\u2713 Saved. Rebooting...';setTimeout(function(){location.href='/';},3000);}
    }else{msg.style.color='#ef5350';msg.textContent=(LANG==='jp'?'\u2717 エラー: ':'\u2717 Error: ')+(d.error||'Unknown');}
    setTimeout(function(){msg.textContent='';},5000);
  })
  .catch(function(e){
    btn.disabled=false;btn.value=btn.dataset.orig;
    if(!msg){msg=document.createElement('div');msg.className='msg';form.appendChild(msg);}
    msg.style.color='#ef5350';msg.textContent=LANG==='jp'?'\u2717 接続エラー':'\u2717 Connection error';
  });
  return false;
}
)JS";

void sendCommonHead(WiFiClient& client, const char* title) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  client.printf("<!DOCTYPE html><html lang=\"%s\"><head><meta charset=UTF-8>", g_language==1?"ja":"en");
  client.printf("<meta name=viewport content='width=device-width,initial-scale=1'>");
  client.printf("<title>%s</title><style>", title);
  client.print(COMMON_CSS);
  client.printf("</style><script>var LANG='%s';</script>\n", g_language==1?"jp":"en");
}
