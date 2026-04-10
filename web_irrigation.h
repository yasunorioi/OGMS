#pragma once

// ============================================================
// Solar Irrigation Page (GET /irrigation)
// ============================================================
void sendIrrigationPage(WiFiClient& client) {
  sendCommonHead(client, L("Solar Irrigation", "日射灌水"));
  client.println("<style>input[type=number]{width:60px}select{width:80px}");
  client.println(".bar{background:#2e2e2e;border-radius:3px;height:18px;width:120px;display:inline-block;vertical-align:middle}");
  client.println(".fill{height:100%;border-radius:3px}</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("Solar Irrigation", "日射灌水"));
  printNavLinks(client);
  client.printf("<p class=note>%s</p>\n", L("Accumulated solar radiation triggers irrigation. Requires ADS1110 + PVSS-03 on I2C Grove.",
    "積算日射量で灌水をトリガーします。I2C GroveにADS1110+PVSS-03が必要。"));
  client.printf("<div class=sec id=solstat>%s</div>\n", L("Loading...","読み込み中..."));
  client.printf("<div class=sec id=irrirun>%s</div>\n", L("Loading...","読み込み中..."));

  // Config form
  client.printf("<h3>%s</h3>\n", L("Settings","設定"));
  client.println("<form action=/api/irrigation onsubmit=\"return irriSubmit(this,this.querySelector('[type=submit]'))\">");
  client.printf("<table><tr><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>Min W/m&sup2;</th></tr>\n",
    L("Rule","ルール"), L("Enable","有効"), L("Relay CH","リレーCH"),
    L("Mode","モード"), L("Threshold(MJ/m&sup2;)","閾値(MJ/m&sup2;)"));
  for (int i = 0; i < IRRI_SLOTS; i++) {
    client.printf("<tr><td>%d</td>", i + 1);
    client.printf("<td><input type=checkbox name=en%d id=irri_en%d aria-label='Enable rule %d' value=1%s></td>", i, i, i+1, irriCtrl[i].enabled ? " checked" : "");
    client.printf("<td><select name=rc%d>", i);
    client.printf("<option value=-1%s>-</option>", irriCtrl[i].relay_ch < 0 ? " selected" : "");
    for (int c = 0; c < 8; c++) {
      client.printf("<option value=%d%s>CH%d</option>", c, irriCtrl[i].relay_ch == c ? " selected" : "", c + 1);
    }
    client.printf("</select></td>");
    client.printf("<td><select name=md%d><option value=0%s>Timer</option><option value=1%s>Duty</option></select></td>",
                  i, irriCtrl[i].mode == 0 ? " selected" : "", irriCtrl[i].mode == 1 ? " selected" : "");
    client.printf("<td><input type=number name=th%d value=%.3f min=0.01 max=10 step=0.01></td>", i, irriCtrl[i].threshold_mj);
    client.printf("<td><input type=number name=mw%d value=%.0f min=0 max=500 step=10></td></tr>", i, irriCtrl[i].min_wm2);
  }
  client.println("</table>");

  client.printf("<h3>%s</h3>\n", L("Timer Mode (mode=0)", "タイマーモード (mode=0)"));
  client.printf("<table><tr><th>%s</th><th>%s</th><th>%s</th></tr>\n",
    L("Rule","ルール"), L("Duration(s)","継続時間(s)"), L("Drain Stop(s)","排液停止(s)"));
  for (int i = 0; i < IRRI_SLOTS; i++) {
    client.printf("<tr><td>%d</td>", i + 1);
    client.printf("<td><input type=number name=du%d value=%d min=10 max=3600></td>", i, irriCtrl[i].duration_sec);
    client.printf("<td><input type=number name=ds%d value=%d min=0 max=300 step=5></td></tr>", i, irriCtrl[i].drain_stop_sec);
  }
  client.println("</table>");

  client.printf("<h3>%s</h3>\n", L("Duty Mode (mode=1)", "デューティモード (mode=1)"));
  client.printf("<table><tr><th>%s</th><th>%s</th><th>Init%%</th><th>Min%%</th><th>Max%%</th><th>Step%%</th><th>%s</th><th>%s</th></tr>\n",
    L("Rule","ルール"), L("Cycle(s)","周期(s)"), L("Drain Lo%","排液Lo%"), L("Drain Hi%","排液Hi%"));
  for (int i = 0; i < IRRI_SLOTS; i++) {
    client.printf("<tr><td>%d</td>", i + 1);
    client.printf("<td><input type=number name=dc%d value=%d min=10 max=600></td>", i, irriCtrl[i].duty_cycle_sec);
    client.printf("<td><input type=number name=di%d value=%.0f min=0 max=100></td>", i, irriCtrl[i].duty_init * 100);
    client.printf("<td><input type=number name=dn%d value=%.0f min=0 max=100></td>", i, irriCtrl[i].duty_min * 100);
    client.printf("<td><input type=number name=dx%d value=%.0f min=0 max=100></td>", i, irriCtrl[i].duty_max * 100);
    client.printf("<td><input type=number name=dt%d value=%.0f min=1 max=50></td>", i, irriCtrl[i].duty_step * 100);
    client.printf("<td><input type=number name=dl%d value=%.0f min=0 max=100></td>", i, irriCtrl[i].drain_target_lo * 100);
    client.printf("<td><input type=number name=dh%d value=%.0f min=0 max=100></td></tr>", i, irriCtrl[i].drain_target_hi * 100);
  }
  client.println("</table>");

  client.printf("<h3>%s</h3>\n", L("Sensor Calibration", "センサーキャリブレーション"));
  client.printf("<table><tr><th>%s</th><th>Flow DI</th><th>mL/pulse</th><th>Drain mL/tip</th></tr>\n", L("Rule","ルール"));
  for (int i = 0; i < IRRI_SLOTS; i++) {
    client.printf("<tr><td>%d</td>", i + 1);
    client.printf("<td><select name=fd%d><option value=0%s>DI1</option><option value=1%s>DI2</option></select></td>",
                  i, irriCtrl[i].flow_di_ch == 0 ? " selected" : "", irriCtrl[i].flow_di_ch == 1 ? " selected" : "");
    client.printf("<td><input type=number name=fp%d value=%.3f min=0.01 max=100 step=0.01></td>", i, irriCtrl[i].flow_ml_per_pulse);
    client.printf("<td><input type=number name=dp%d value=%.2f min=0.1 max=100 step=0.1></td></tr>", i, irriCtrl[i].drain_ml_per_tip);
  }
  client.println("</table>");

  client.printf("<input type=submit value='%s'>\n", L("Save","保存"));
  client.println("</form>");
  client.printf("<p class=note><b>%s</b>: %s<br>\n",
    L("Timer mode","タイマーモード"),
    L("solar threshold triggers, runs for Duration seconds, optional drain stop.",
      "日射閾値でトリガー、継続時間秒間動作、排液停止オプションあり。"));
  client.printf("<b>%s</b>: %s<br>\n",
    L("Duty mode","デューティモード"),
    L("solar threshold triggers, then duty cycle adjusts based on drain rate feedback.",
      "日射閾値でトリガー後、排液率フィードバックでデューティサイクルを調整。"));
  client.println("Drain rate = (drain mL) / (flow mL). Duty increases when below Lo%, decreases above Hi%.<br>");
  client.println("Session ends when drain rate exceeds Hi% and duty reaches minimum.</p>");

  // Auto-refresh + form JS
  client.println("<script>");
  client.print(FORM_JS);
  client.println("function irriSubmit(form,btn){");
  client.println("var anyEn=false;for(var i=0;i<2;i++){var e=form.querySelector('[name=en'+i+']');if(e&&e.checked)anyEn=true;}");
  client.printf("if(!anyEn&&!confirm('%s'))return false;\n",
    L("All rules will be disabled. Continue?", "全ルールが無効になります。続けますか？"));
  client.println("for(var i=0;i<2;i++){");
  client.println("var th=form.querySelector('[name=th'+i+']');");
  client.println("var du=form.querySelector('[name=du'+i+']');");
  client.println("var en=form.querySelector('[name=en'+i+']');");
  client.println("if(en&&en.checked){");
  client.println("if(th&&parseFloat(th.value)<=0){alert('Rule '+(i+1)+': '+(LANG==='jp'?'閾値は0より大きく設定してください':'Threshold must be > 0'));return false;}");
  client.println("if(du&&parseInt(du.value)<=0){alert('Rule '+(i+1)+': '+(LANG==='jp'?'灌水時間は0より大きく設定してください':'Duration must be > 0'));return false;}");
  client.println("}}");
  client.println("return submitForm(form,btn);}");
  client.println("function irLoad(){");
  client.println("fetch('/api/state').then(function(r){return r.json();}).then(function(d){");
  // Solar sensor status
  client.println("var s='<h3>Solar Sensor</h3>';");
  client.println("if(d.ads1110_ok){");
  client.println("  var wm2=d.sensor.solar_wm2;");
  client.println("  s+='<b>ADS1110:</b> <span class=on>OK</span> | ';");
  client.println("  if(wm2!==null){");
  client.println("    s+='<b>Solar:</b> <span style=\"font-size:1.4em;font-weight:bold\">'+wm2.toFixed(1)+'</span> W/m&sup2;';");
  client.println("    var pct=Math.min(100,wm2/10);");
  client.println("    var col=wm2>600?'#ffa726':wm2>200?'#66bb6a':'#5e6ad2';");
  client.println("    s+=' <div class=bar><div class=fill style=\"background:'+col+';width:'+pct+'%\"></div></div>';");
  client.println("  } else s+='<span class=off>no reading</span>';");
  client.println("} else s+='<b>ADS1110:</b> <span class=off>not detected</span> — connect M5Stack ADC Unit to Grove I2C';");
  client.println("document.getElementById('solstat').innerHTML=s;");
  // Irrigation runtime
  client.println("var ir=d.irrigation||[];var h='<h3>Irrigation Status</h3>';");
  client.println("var any=false;for(var i=0;i<ir.length;i++)if(ir[i].enabled)any=true;");
  client.println("if(any){");
  client.println("h+='<table><tr><th>Rule</th><th>Relay</th><th>Accumulated</th><th>Threshold</th><th>Progress</th><th>State</th><th>Today</th></tr>';");
  client.println("for(var i=0;i<ir.length;i++){var r=ir[i];if(!r.enabled)continue;");
  client.println("var pct=Math.min(100,(r.accum_mj/r.threshold_mj)*100);");
  client.println("h+='<tr><td>'+(i+1)+'</td><td>CH'+(r.relay_ch+1)+'</td>';");
  client.println("h+='<td>'+r.accum_mj.toFixed(3)+' MJ/m&sup2;</td>';");
  client.println("h+='<td>'+r.threshold_mj+' MJ/m&sup2;</td>';");
  client.println("h+='<td><div class=bar><div class=fill style=\"background:'+(r.irrigating?'#42a5f5':'#43a047')+';width:'+pct+'%\"></div></div> '+pct.toFixed(0)+'%</td>';");
  client.println("var st=r.irrigating?'\\u2713 WATERING':'\\u2717 Accumulating';");
  client.println("if(r.irrigating&&r.mode==1)st='\\u2713 DUTY '+r.duty+'% (drain:'+r.drain_rate+'%)';");
  client.println("else if(r.irrigating&&r.remaining_sec)st='\\u2713 WATERING ('+r.remaining_sec+'s)';");
  client.println("h+='<td class='+(r.irrigating?'on':'off')+'><b>'+st+'</b></td>';");
  client.println("h+='<td><b>'+r.today_count+'</b></td></tr>';}");
  client.println("h+='</table>';}else{h+='<span class=off>No rules configured</span>';}");
  client.println("document.getElementById('irrirun').innerHTML=h;");
  client.println("});}");
  client.println("irLoad();setInterval(irLoad,3000);");
  client.println("</script>");
  client.println("</body></html>");
}

// ============================================================
// POST /api/irrigation
// ============================================================
void handleIrrigationPost(WiFiClient& client, const String& body) {
  auto getField = [&](const String& key) -> String {
    String search = key + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('&', idx);
    if (end < 0) end = body.length();
    return body.substring(idx, end);
  };

  for (int i = 0; i < IRRI_SLOTS; i++) {
    String en = getField(String("en") + i);
    irriCtrl[i].enabled = (en == "1");

    String rc = getField(String("rc") + i);
    if (rc.length() > 0) irriCtrl[i].relay_ch = rc.toInt();

    String th = getField(String("th") + i);
    if (th.length() > 0) irriCtrl[i].threshold_mj = th.toFloat();

    String du = getField(String("du") + i);
    if (du.length() > 0) irriCtrl[i].duration_sec = du.toInt();

    String mw = getField(String("mw") + i);
    if (mw.length() > 0) irriCtrl[i].min_wm2 = mw.toFloat();

    String ds = getField(String("ds") + i);
    if (ds.length() > 0) irriCtrl[i].drain_stop_sec = ds.toInt();

    String md = getField(String("md") + i);
    if (md.length() > 0) irriCtrl[i].mode = md.toInt();

    String dc = getField(String("dc") + i);
    if (dc.length() > 0) irriCtrl[i].duty_cycle_sec = dc.toInt();

    String di = getField(String("di") + i);
    if (di.length() > 0) irriCtrl[i].duty_init = di.toFloat() / 100.0;

    String dn = getField(String("dn") + i);
    if (dn.length() > 0) irriCtrl[i].duty_min = dn.toFloat() / 100.0;

    String dx = getField(String("dx") + i);
    if (dx.length() > 0) irriCtrl[i].duty_max = dx.toFloat() / 100.0;

    String dt_s = getField(String("dt") + i);
    if (dt_s.length() > 0) irriCtrl[i].duty_step = dt_s.toFloat() / 100.0;

    String dl = getField(String("dl") + i);
    if (dl.length() > 0) irriCtrl[i].drain_target_lo = dl.toFloat() / 100.0;

    String dh = getField(String("dh") + i);
    if (dh.length() > 0) irriCtrl[i].drain_target_hi = dh.toFloat() / 100.0;

    String fd = getField(String("fd") + i);
    if (fd.length() > 0) irriCtrl[i].flow_di_ch = fd.toInt();

    String fp = getField(String("fp") + i);
    if (fp.length() > 0) irriCtrl[i].flow_ml_per_pulse = fp.toFloat();

    String dp = getField(String("dp") + i);
    if (dp.length() > 0) irriCtrl[i].drain_ml_per_tip = dp.toFloat();

    // Reset runtime on config change
    memset(&irriRun[i], 0, sizeof(IrriRuntime));
  }

  saveIrrigationConfig();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true}");
}
