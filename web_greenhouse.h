#pragma once

// ============================================================
// Greenhouse Control Page (GET /greenhouse)
// ============================================================
void sendGreenhousePage(WiFiClient& client) {
  sendCommonHead(client, "Greenhouse Control");
  client.println("<style>input[type=number]{width:60px}select{width:80px}");
  client.println(".curve-extra{display:none}.sec h3{margin:0 0 8px}</style></head><body>");
  client.println("<h2>Greenhouse Control</h2>");
  client.print(NAV_LINKS); client.println();
  client.println("<p class=note>Temperature-based proportional relay control. CCM commands take priority when active.</p>");
  client.println("<div class=sec id=ghstat>Loading...</div>");
  client.println("<div class=sec id=ghrun>Loading...</div>");

  // === Greenhouse Config form ===
  client.println("<div class=sec><h3>Greenhouse Rules</h3>");
  client.println("<form id=ghform action=/api/greenhouse onsubmit=\"return ghSubmit(this,this.querySelector('[type=submit]'))\">");
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    client.printf("<div class=sec><b>Rule %d</b><br>", i + 1);
    client.printf("<label><input type=checkbox name=en%d id=en%d aria-label='Enable rule %d' value=1%s> Enable</label> ", i, i, i+1, ghCtrl[i].enabled ? " checked" : "");
    // CH select
    client.printf("CH:<select name=ch%d>", i);
    client.printf("<option value=-1%s>-</option>", ghCtrl[i].ch < 0 ? " selected" : "");
    for (int c = 0; c < 8; c++)
      client.printf("<option value=%d%s>CH%d</option>", c, ghCtrl[i].ch == c ? " selected" : "", c + 1);
    client.printf("</select> ");
    // Sensor select
    client.printf("Sensor:<select name=ss%d>", i);
    client.printf("<option value=0%s>SHT40</option>", ghCtrl[i].sensor_src == 0 ? " selected" : "");
    client.printf("<option value=1%s>DS18B20</option>", ghCtrl[i].sensor_src == 1 ? " selected" : "");
    client.printf("</select><br>");
    client.printf("<label title='Relay starts opening at this temp'>Open(C):<input type=number name=to%d value=%.1f min=-10 max=60 step=0.5></label> ", i, ghCtrl[i].temp_open);
    client.printf("<label title='100%% duty at this temp'>Full(C):<input type=number name=tf%d value=%.1f min=-10 max=60 step=0.5></label> ", i, ghCtrl[i].temp_full);
    client.printf("<label>Cycle(s):<input type=number name=cy%d value=%d min=10 max=600></label><br>", i, ghCtrl[i].cycle_sec);
    // Curve mode
    client.printf("Curve:<select name=cm%d id=cm%d onchange=\"showCurve(%d,this.value)\">", i, i, i);
    client.printf("<option value=0%s>Linear</option>", ghCtrl[i].curve_mode==0?" selected":"");
    client.printf("<option value=1%s>Table</option>",  ghCtrl[i].curve_mode==1?" selected":"");
    client.printf("<option value=2%s>Sigmoid</option>",ghCtrl[i].curve_mode==2?" selected":"");
    client.printf("<option value=3%s>Exponential</option>",ghCtrl[i].curve_mode==3?" selected":"");
    client.printf("</select> ");
    // Sigmoid/Exponential coefficient
    client.printf("<span id=cc%d class=curve-extra><label>Coeff(k):<input type=number name=ck%d value=%.1f min=0.1 max=10 step=0.1></label></span>", i, i, ghCtrl[i].curve_coeff);
    // Table points
    client.printf("<span id=ct%d class=curve-extra><br>", i);
    client.printf("<label>Points:<input type=number name=cpn%d value=%d min=2 max=%d></label><br>", i, ghCtrl[i].curve_point_count, MAX_CURVE_POINTS);
    client.println("<table><tr><th>Temp(C)</th><th>Duty%</th></tr>");
    for (int p = 0; p < MAX_CURVE_POINTS; p++) {
      float t = (p < ghCtrl[i].curve_point_count) ? ghCtrl[i].curve_points[p].temp : (ghCtrl[i].temp_open + p * (ghCtrl[i].temp_full - ghCtrl[i].temp_open) / (MAX_CURVE_POINTS-1));
      float pct = (p < ghCtrl[i].curve_point_count) ? ghCtrl[i].curve_points[p].pct : (p * 100.0 / (MAX_CURVE_POINTS-1));
      client.printf("<tr><td><input type=number name=cpt%d_%d value=%.1f min=-10 max=60 step=0.5></td>", i, p, t);
      client.printf("<td><input type=number name=cpp%d_%d value=%.0f min=0 max=100></td></tr>", i, p, pct);
    }
    client.println("</table></span>");
    client.println("</div>");
  }
  client.println("<input type=submit value='Save Rules'>");
  client.println("</form></div>");

  // === Aperture (Side Window) Config form ===
  client.println("<div class=sec><h3>Side Window Aperture</h3>");
  client.println("<p class=note>Time-based aperture control. Define open/close durations per % range.</p>");
  client.println("<form id=aptform action=/api/aperture onsubmit=\"return submitForm(this,this.querySelector('[type=submit]'))\">");
  for (int i = 0; i < APT_SLOTS; i++) {
    client.printf("<div class=sec><b>Slot %d</b><br>", i + 1);
    client.printf("<label><input type=checkbox name=apt_en%d value=1%s> Enable</label> ", i, aptCtrl[i].enabled ? " checked" : "");
    // Open CH
    client.printf("Open CH:<select name=apt_ch%d>", i);
    client.printf("<option value=-1%s>-</option>", aptCtrl[i].ch < 0 ? " selected" : "");
    for (int c = 0; c < 8; c++)
      client.printf("<option value=%d%s>CH%d</option>", c, aptCtrl[i].ch == c ? " selected" : "", c + 1);
    client.printf("</select> ");
    // Close CH
    client.printf("Close CH:<select name=apt_cc%d>", i);
    client.printf("<option value=-1%s>OFF=close</option>", aptCtrl[i].close_ch < 0 ? " selected" : "");
    for (int c = 0; c < 8; c++)
      client.printf("<option value=%d%s>CH%d</option>", c, aptCtrl[i].close_ch == c ? " selected" : "", c + 1);
    client.printf("</select> ");
    // Limit DI
    client.printf("Limit DI:<select name=apt_di%d>", i);
    client.printf("<option value=-1%s>-</option>", aptCtrl[i].limit_di < 0 ? " selected" : "");
    for (int d = 0; d < 8; d++)
      client.printf("<option value=%d%s>DI%d</option>", d, aptCtrl[i].limit_di == d ? " selected" : "", d + 1);
    client.printf("</select><br>");
    client.printf("<label>Segments:<input type=number name=apt_sn%d value=%d min=2 max=%d></label><br>", i, aptCtrl[i].segment_count, MAX_APT_SEGMENTS);
    client.println("<table><tr><th>From%</th><th>To%</th><th>Sec</th></tr>");
    for (int j = 0; j < MAX_APT_SEGMENTS; j++) {
      client.printf("<tr><td><input type=number name=apt_sf%d_%d value=%.0f min=0 max=100></td>", i, j, aptCtrl[i].segments[j].from_pct);
      client.printf("<td><input type=number name=apt_st%d_%d value=%.0f min=0 max=100></td>", i, j, aptCtrl[i].segments[j].to_pct);
      client.printf("<td><input type=number name=apt_ss%d_%d value=%d min=0 max=300></td></tr>", i, j, aptCtrl[i].segments[j].seconds);
    }
    client.println("</table></div>");
  }
  client.println("<input type=submit value='Save Aperture'>");
  client.println("</form></div>");

  // Auto-refresh + form JS
  client.println("<script>");
  client.print(FORM_JS);
  // Curve show/hide
  client.println("function showCurve(i,v){");
  client.println("var cc=document.getElementById('cc'+i);var ct=document.getElementById('ct'+i);");
  client.println("if(cc)cc.style.display=(v=='2'||v=='3')?'inline':'none';");
  client.println("if(ct)ct.style.display=(v=='1')?'inline':'none';}");
  // Init curve visibility
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    client.printf("showCurve(%d,'%d');", i, ghCtrl[i].curve_mode);
  }
  // GH submit validation
  client.println("function ghSubmit(form,btn){");
  client.println("var anyEn=false;for(var i=0;i<4;i++){var e=form.querySelector('[name=en'+i+']');if(e&&e.checked)anyEn=true;}");
  client.println("if(!anyEn&&!confirm('All rules will be disabled. Continue?'))return false;");
  client.println("for(var i=0;i<4;i++){");
  client.println("var to=form.querySelector('[name=to'+i+']');");
  client.println("var tf=form.querySelector('[name=tf'+i+']');");
  client.println("var en=form.querySelector('[name=en'+i+']');");
  client.println("if(en&&en.checked&&to&&tf&&parseFloat(to.value)>=parseFloat(tf.value)){");
  client.println("alert('Rule '+(i+1)+': Open temp must be less than Full temp');");
  client.println("return false;}}");
  client.println("return submitForm(form,btn);}");
  // ghLoad: runtime display with aperture%
  client.println("function ghLoad(){");
  client.println("fetch('/api/state').then(function(r){return r.json();}).then(function(d){");
  // Sensor status
  client.println("var s='<h3>Sensors</h3>';");
  client.println("if(d.sht40_ok)s+='<b>SHT40:</b> <span class=on>'+(d.sensor.temp!==null?d.sensor.temp.toFixed(1)+'C':'OK')+'</span> ';");
  client.println("else s+='<b>SHT40:</b> <span class=off>none</span> ';");
  client.println("if(d.sensor.hum!==null)s+='<b>Hum:</b> '+d.sensor.hum.toFixed(1)+'% ';");
  client.println("if(d.ds18b20_ok)s+='| <b>DS18B20:</b> <span class=on>'+(d.sensor.ds18b20_temp!==null?d.sensor.ds18b20_temp.toFixed(1)+'C':'OK')+'</span>';");
  client.println("else s+='| <b>DS18B20:</b> <span class=off>none</span>';");
  // Aperture status
  client.println("var apt=d.aperture||[];var aptAny=false;for(var i=0;i<apt.length;i++)if(apt[i].enabled)aptAny=true;");
  client.println("if(aptAny){s+='<br><b>Aperture:</b> ';for(var i=0;i<apt.length;i++){var a=apt[i];if(!a.enabled)continue;");
  client.println("s+='Slot'+(i+1)+': <b>'+a.current_pct.toFixed(0)+'%</b>';");
  client.println("if(a.moving)s+='<span class=on> \u2192'+a.target_pct.toFixed(0)+'%</span>';");
  client.println("if(a.initializing)s+='<span class=note>(init)</span>';");
  client.println("s+=' ';}}");
  client.println("document.getElementById('ghstat').innerHTML=s;");
  // Runtime table
  client.println("var gh=d.greenhouse||[];var h='<h3>Runtime</h3>';");
  client.println("var any=false;for(var i=0;i<gh.length;i++)if(gh[i].enabled)any=true;");
  client.println("if(any){");
  client.println("h+='<table><tr><th>Rule</th><th>CH</th><th>Sensor</th><th>Temp</th><th>Range</th><th>Curve</th><th>Duty</th><th>State</th></tr>';");
  client.println("var cnames=['Linear','Table','Sigmoid','Exp'];");
  client.println("for(var i=0;i<gh.length;i++){var g=gh[i];if(!g.enabled)continue;");
  client.println("h+='<tr><td>'+(i+1)+'</td><td>CH'+(g.ch+1)+'</td><td>'+g.sensor+'</td>';");
  client.println("h+='<td style=\"font-size:1.3em;font-weight:bold\">'+(g.temp!==null?g.temp.toFixed(1)+'C':'-')+'</td>';");
  client.println("h+='<td>'+g.temp_open+' - '+g.temp_full+'C</td>';");
  client.println("h+='<td>'+(cnames[g.curve_mode||0]||'?')+'</td>';");
  client.println("h+='<td><div style=\"background:#2e2e2e;border-radius:3px;height:18px;width:80px;display:inline-block;vertical-align:middle\">';");
  client.println("h+='<div style=\"background:'+(g.duty>70?'#e53935':g.duty>30?'#ffa726':'#43a047')+';height:100%;width:'+g.duty+'%;border-radius:3px\"></div></div> '+g.duty+'%</td>';");
  client.println("h+='<td class='+(g.active?'on':'off')+'><b>'+(g.active?'\\u2713 ON':'\\u2717 OFF')+'</b></td></tr>';}");
  client.println("h+='</table>';}else{h+='<span class=off>No rules active</span>';}");
  client.println("document.getElementById('ghrun').innerHTML=h;");
  client.println("});}");
  client.println("ghLoad();setInterval(ghLoad,3000);");
  client.println("</script>");
  client.println("</body></html>");
}

// ============================================================
// POST /api/greenhouse
// ============================================================
void handleGreenhousePost(WiFiClient& client, const String& body) {
  auto getField = [&](const String& key) -> String {
    String search = key + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('&', idx);
    if (end < 0) end = body.length();
    return body.substring(idx, end);
  };

  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    ghCtrl[i].enabled = (getField(String("en") + i) == "1");
    String ch = getField(String("ch") + i);
    if (ch.length() > 0) ghCtrl[i].ch = ch.toInt();
    String ss = getField(String("ss") + i);
    if (ss.length() > 0) ghCtrl[i].sensor_src = ss.toInt();
    String to = getField(String("to") + i);
    if (to.length() > 0) ghCtrl[i].temp_open = to.toFloat();
    String tf = getField(String("tf") + i);
    if (tf.length() > 0) ghCtrl[i].temp_full = tf.toFloat();
    String cy = getField(String("cy") + i);
    if (cy.length() > 0) ghCtrl[i].cycle_sec = cy.toInt();
    // Curve
    String cm = getField(String("cm") + i);
    if (cm.length() > 0) ghCtrl[i].curve_mode = constrain(cm.toInt(), 0, 3);
    String ck = getField(String("ck") + i);
    if (ck.length() > 0) ghCtrl[i].curve_coeff = constrain(ck.toFloat(), 0.1, 10.0);
    String cpn = getField(String("cpn") + i);
    if (cpn.length() > 0) ghCtrl[i].curve_point_count = constrain(cpn.toInt(), 2, MAX_CURVE_POINTS);
    for (int p = 0; p < MAX_CURVE_POINTS; p++) {
      String t = getField(String("cpt") + i + "_" + p);
      if (t.length() > 0) ghCtrl[i].curve_points[p].temp = t.toFloat();
      String v = getField(String("cpp") + i + "_" + p);
      if (v.length() > 0) ghCtrl[i].curve_points[p].pct = constrain(v.toFloat(), 0.0, 100.0);
    }
    ghRun[i].cycleStart = 0;
  }

  saveGreenhouseConfig();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true}");
}

// ============================================================
// POST /api/aperture
// ============================================================
void handleAperturePost(WiFiClient& client, const String& body) {
  auto getField = [&](const String& key) -> String {
    String search = key + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('&', idx);
    if (end < 0) end = body.length();
    return body.substring(idx, end);
  };

  for (int i = 0; i < APT_SLOTS; i++) {
    aptCtrl[i].enabled  = (getField(String("apt_en") + i) == "1");
    String ch = getField(String("apt_ch") + i);
    if (ch.length() > 0) aptCtrl[i].ch = ch.toInt();
    String cc = getField(String("apt_cc") + i);
    if (cc.length() > 0) aptCtrl[i].close_ch = cc.toInt();
    String di = getField(String("apt_di") + i);
    if (di.length() > 0) aptCtrl[i].limit_di = di.toInt();
    String sn = getField(String("apt_sn") + i);
    if (sn.length() > 0) aptCtrl[i].segment_count = constrain(sn.toInt(), 2, MAX_APT_SEGMENTS);
    for (int j = 0; j < MAX_APT_SEGMENTS; j++) {
      String sf = getField(String("apt_sf") + i + "_" + j);
      if (sf.length() > 0) aptCtrl[i].segments[j].from_pct = constrain(sf.toFloat(), 0.0, 100.0);
      String st = getField(String("apt_st") + i + "_" + j);
      if (st.length() > 0) aptCtrl[i].segments[j].to_pct = constrain(st.toFloat(), 0.0, 100.0);
      String ss = getField(String("apt_ss") + i + "_" + j);
      if (ss.length() > 0) aptCtrl[i].segments[j].seconds = constrain(ss.toInt(), 0, 300);
    }
    // Re-initialize on config change
    if (aptCtrl[i].enabled) {
      aptRun[i].initializing = true;
      aptRun[i].moving = false;
    }
  }

  saveApertureConfig();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true}");
}
