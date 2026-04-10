#pragma once

// ============================================================
// WebUI — Dashboard
// ============================================================
static const char DASHBOARD_JS[] PROGMEM = R"DASH_JS(
function relay(ch,v){
  fetch('/api/relay/'+ch,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({value:v})}).then(load);
}
function load(){
  fetch('/api/state').then(function(r){return r.json();}).then(function(d){
    var T=d.language==='jp'?{
      net:'\u30cd\u30c3\u30c8\u30ef\u30fc\u30af',devstat:'\u30c7\u30d0\u30a4\u30b9\u72b6\u614b',sensors:'\u30bb\u30f3\u30b5\u30fc',
      on:'\u30aa\u30f3',off:'\u30aa\u30d5',gh:'\u6e29\u5ba4\u5236\u5fa1',irri:'\u65e5\u5c04\u704c\u6c34',prot:'\u4fdd\u8b77\u5236\u5fa1',
      settings:'\u8a2d\u5b9a',notcfg:'\u672a\u8a2d\u5b9a',watering:'\u704c\u6c34\u4e2d',accum:'\u7a4d\u7b97\u4e2d',
      norules:'\u30eb\u30fc\u30eb\u672a\u8a2d\u5b9a',active:'\u7a3c\u50cd\u4e2d',standby:'\u5f85\u6a5f\u4e2d',normal:'\u6b63\u5e38',
      configure:'\u8a2d\u5b9a\u3059\u308b',solar:'\u65e5\u5c04',irriSolar:'\u65e5\u5c04\u704c\u6c34'
    }:{
      net:'Network',devstat:'Device Status',sensors:'Sensors',
      on:'ON',off:'OFF',gh:'Greenhouse Control',irri:'Solar Irrigation',prot:'Protection',
      settings:'Settings',notcfg:'Not configured',watering:'WATERING',accum:'Accumulating',
      norules:'No rules active',active:'ACTIVE',standby:'Standby',normal:'Normal',
      configure:'Configure',solar:'Solar',irriSolar:'Solar Irrigation'
    };
    document.getElementById('sys').innerHTML=
      '<b>Node:</b> '+d.node_id+' | <b>FW:</b> '+d.version+
      ' | <b>Protocol:</b> <span style="color:#ffa726">UECS-CCM</span>'+
      ' | <b>Uptime:</b> '+d.uptime+'s'+
      ' | <b>Last:</b> '+(d.last_updated||'-')+
      ' | <a href="/ccm">CCM</a> | <a href="/greenhouse">Greenhouse</a> | <a href="/irrigation">Irrigation</a> | <a href="/protection">Protection</a> | <a href="/config">Network</a> | <a href="/ota">FW</a>';
    var mdnsHost=d.mdns_hostname?(' | <b>mDNS:</b> '+d.mdns_hostname):'';
    document.getElementById('net').innerHTML=
      '<h3>'+T.net+'</h3><b>IP:</b> '+d.ip+
      ' | <b>GW:</b> '+d.gateway+mdnsHost+
      '<br><b>MAC:</b> '+d.mac+
      ' | <b>CCM:</b> 224.0.0.1:16520';
    var solSrc=d.ads1110_ok?'<span class=badge-ok>ADS1110 \u2713</span>':(d.sensor&&d.sensor.solar_wm2!==null?'<span class=badge-ok>CCM \u2713</span>':'<span class=badge-ng>none \u2717</span>');
    var solVal=(d.sensor&&d.sensor.solar_wm2!==null)?' '+d.sensor.solar_wm2.toFixed(0)+'W/m&sup2;':'';
    document.getElementById('devstat').innerHTML=
      '<h3>'+T.devstat+'</h3>'+
      '<b>I2C:</b> '+(d.sht40_ok?'<span class=badge-ok>SHT40 \u2713</span>':'<span class=badge-ng>SHT40 \u2717</span>')+
      (d.scd41_ok?' <span class=badge-ok>SCD41 \u2713</span>':' <span class=badge-ng>SCD41 \u2717</span>')+
      ' | <b>Solar:</b> '+solSrc+solVal+
      ' | <b>1-Wire:</b> '+(d.ds18b20_ok?'<span class=badge-ok>DS18B20 \u2713</span>':'<span class=badge-ng>DS18B20 \u2717</span>')+
      ' | <b>UART:</b> '+(d.sen0575_ok?'<span class=badge-ok>SEN0575 \u2713</span>':'<span class=badge-ng>SEN0575 \u2717</span>');
    var rt='';
    var ccm=d.ccm_map||[];
    for(var i=0;i<8;i++){
      var s=(d.relay_state>>(i))&1;
      var m=ccm[i]||{};
      var tname=m.type||'(unmapped)';
      rt+='<tr><td>'+(i+1)+'</td><td>'+tname+' <span class=ccm>R'+
        (m.room||'-')+'/Rg'+(m.region||'-')+'/O'+(m.order||'-')+'</span></td>'+
        '<td>'+(m.room||'-')+'</td>'+
        '<td class="'+(s?'on':'off')+'">'+(s?'\u2713 '+T.on:'\u2717 '+T.off)+'</td>'+
        '<td><button class=bon onclick="relay('+(i+1)+',1)">ON</button> '+
        '<button class=bof onclick="relay('+(i+1)+',0)">OFF</button></td></tr>';
    }
    document.getElementById('rtbl').innerHTML=rt;
    var dt='';
    for(var i=0;i<8;i++){
      var s=(d.di_state>>(i))&1;
      dt+='<tr><td>'+(i+1)+'</td><td class="'+(s?'on':'off')+'">'+(s?'\u2713 ON':'\u2717 OFF')+'</td></tr>';
    }
    document.getElementById('dtbl').innerHTML=dt;
    var sv='<h3>'+T.sensors+'</h3>';
    if(d.sensor&&d.sensor.temp!==null)sv+='<b>SHT40 Temp:</b> '+d.sensor.temp.toFixed(1)+'C ';
    if(d.sensor&&d.sensor.hum!==null)sv+='<b>Hum:</b> '+d.sensor.hum.toFixed(1)+'% ';
    if(d.sensor&&d.sensor.ds18b20_temp!==null)sv+='<b>DS18B20:</b> '+d.sensor.ds18b20_temp.toFixed(1)+'C ';
    if(d.sensor&&d.sensor.solar_wm2!==null)sv+='<b>Solar:</b> '+d.sensor.solar_wm2.toFixed(1)+' W/m&sup2; ';
    if(d.sensor&&d.sensor.co2!==undefined)sv+='<b>CO2:</b> '+d.sensor.co2+'ppm <b>SCD41:</b> '+d.sensor.scd41_temp.toFixed(1)+'C '+d.sensor.scd41_hum.toFixed(1)+'% ';
    if(d.sensor&&d.sensor.temp===null&&d.sensor.hum===null&&d.sensor.ds18b20_temp===null&&d.sensor.solar_wm2===null&&d.sensor.co2===undefined)sv+='<span class=off>No sensors</span>';
    document.getElementById('sens').innerHTML=sv;
    var cs=d.ccm_solar;
    if(cs&&cs.wm2!==undefined){
      var age=cs.age_sec;
      var cv='<h3>CCM InRadiation</h3>';
      if(age<0||age>60){
        cv+='<span class=off>No data'+(age>0?' ('+age+'s ago)':'')+'</span>';
      }else{
        cv+='<b>'+cs.wm2.toFixed(1)+' W/m&sup2;</b>';
        cv+=' | <span class=ccm>Room'+cs.room+'/Rg'+cs.region+'/O'+cs.order+'</span>';
        cv+=' | '+age+'s ago';
      }
      document.getElementById('ccm_solar').innerHTML=cv;
    }
    var gh=d.greenhouse||[];
    var anyGh=false;
    for(var i=0;i<gh.length;i++){if(gh[i].enabled)anyGh=true;}
    if(anyGh){
      var gv='<h3>'+T.gh+'</h3><table><tr><th>Rule</th><th>CH</th><th>Sensor</th><th>Temp</th><th>Range</th><th>Duty</th><th>State</th></tr>';
      for(var i=0;i<gh.length;i++){
        var g=gh[i];
        if(!g.enabled)continue;
        gv+='<tr><td>'+(i+1)+'</td><td>CH'+(g.ch+1)+'</td><td>'+g.sensor+'</td>';
        gv+='<td>'+(g.temp!==null?g.temp.toFixed(1)+'C':'-')+'</td>';
        gv+='<td>'+g.temp_open+'-'+g.temp_full+'C</td>';
        gv+='<td>'+g.duty+'%</td>';
        gv+='<td class='+(g.active?'on':'off')+'>'+(g.active?'\u2713 ON':'\u2717 OFF')+'</td></tr>';
      }
      gv+='</table><p><a href="/greenhouse">'+T.settings+'</a></p>';
      document.getElementById('gh').innerHTML=gv;
    } else {
      document.getElementById('gh').innerHTML='<h3>'+T.gh+'</h3><span class=off>'+T.norules+'</span> \u2014 <a href="/greenhouse">'+T.configure+'</a>';
    }
    var ir=d.irrigation||[];
    var anyIr=false;
    for(var i=0;i<ir.length;i++){if(ir[i].enabled)anyIr=true;}
    if(anyIr){
      var iv='<h3>'+T.irriSolar+'</h3>';
      if(d.sensor&&d.sensor.solar_wm2!==null)iv+='<b>'+T.solar+':</b> '+d.sensor.solar_wm2.toFixed(1)+' W/m&sup2; | ';
      iv+='<table><tr><th>Rule</th><th>CH</th><th>Accum</th><th>Threshold</th><th>State</th><th>Today</th></tr>';
      for(var i=0;i<ir.length;i++){var r=ir[i];if(!r.enabled)continue;
        iv+='<tr><td>'+(i+1)+'</td><td>CH'+(r.relay_ch+1)+'</td>';
        iv+='<td>'+r.accum_mj.toFixed(3)+' MJ</td><td>'+r.threshold_mj+' MJ</td>';
        iv+='<td class='+(r.irrigating?'on':'off')+'>'+(r.irrigating?'\u2713 '+T.watering:'\u2717 '+T.accum)+'</td>';
        iv+='<td><b>'+r.today_count+'</b></td></tr>';}
      iv+='</table><p><a href="/irrigation">'+T.settings+'</a></p>';
      document.getElementById('irri').innerHTML=iv;
    } else {
      document.getElementById('irri').innerHTML='<h3>'+T.irriSolar+'</h3><span class=off>'+T.notcfg+'</span> \u2014 <a href="/irrigation">'+T.configure+'</a>';
    }
    var p=d.protection||{};
    var pv='<h3>'+T.prot+'</h3>';
    var anyP=false;
    if(p.dew&&p.dew.enabled){anyP=true;
      pv+='<b>Dew:</b> '+(p.dew.active?'<span class=on>\u2713 ACTIVE</span>':'\u2717 Standby');
      if(p.dew.sunrise)pv+=' (sunrise '+p.dew.sunrise+') ';
      pv+=' | ';}
    if(p.rate&&p.rate.enabled){anyP=true;
      pv+='<b>Rate Guard:</b> '+(p.rate.active?'<span class=on>\u2713 ACTIVE</span>':'\u2717 Normal');
      if(p.rate.current_rate!==null)pv+=' ('+p.rate.current_rate.toFixed(1)+'C/min)';}
    if(anyP){pv+=' \u2014 <a href="/protection">'+T.settings+'</a>';
      document.getElementById('prot').innerHTML=pv;
    }else{
      document.getElementById('prot').innerHTML='<h3>'+T.prot+'</h3><span class=off>'+T.notcfg+'</span> \u2014 <a href="/protection">'+T.configure+'</a>';
    }
  });
}
load();setInterval(load,5000);
)DASH_JS";

void sendDashboard(WiFiClient& client) {
  sendCommonHead(client, L("CCM Relay Node", "CCMリレーノード"));
  client.println("<style>"
    ".bon{background:#43a047;color:#fff;border:none;padding:8px 16px;border-radius:3px;cursor:pointer}"
    ".bof{background:#e53935;color:#fff;border:none;padding:8px 16px;border-radius:3px;cursor:pointer}"
    ".ccm{color:#ffa726;font-size:0.85em}"
    "</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("CCM Relay Node (UECS)", "CCMリレーノード (UECS)"));
  printNavLinks(client);
  client.printf("<div class=sec id=sys>%s</div>\n", L("Loading...","読み込み中..."));
  client.printf("<div class=sec id=net>%s</div>\n", L("Loading...","読み込み中..."));
  client.printf("<div class=sec id=devstat>%s</div>\n", L("Loading...","読み込み中..."));
  client.println("<div class=sec>");
  client.printf("<h3>%s</h3>\n", L("Relay / CCM Mapping", "リレー/CCM割当"));
  client.printf("<table><tr><th>CH</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th></tr>\n",
    L("CCM Type","CCMタイプ"), L("Room","部屋"), L("State","状態"), L("Control","制御"));
  client.println("<tbody id=rtbl></tbody></table></div>");
  client.println("<div class=sec>");
  client.printf("<h3>%s</h3>\n", L("Digital Input", "デジタル入力"));
  client.printf("<table><tr><th>CH</th><th>%s</th></tr>\n", L("State","状態"));
  client.println("<tbody id=dtbl></tbody></table></div>");
  client.println("<div class=sec id=sens></div>");
  client.println("<div class=sec id=ccm_solar></div>");
  client.println("<div class=sec id=gh></div>");
  client.println("<div class=sec id=irri></div>");
  client.println("<div class=sec id=prot></div>");
  client.println("<script>");
  client.print(DASHBOARD_JS);
  client.println("</script></body></html>");
}
