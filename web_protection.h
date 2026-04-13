#pragma once

// ============================================================
// Protection Page (GET /protection) — Dew + Rate Guard
// ============================================================
void sendProtectionPage(WiFiClient& client) {
  sendCommonHead(client, L("Protection", "保護制御"));
  client.println("<style>input[type=number]{width:60px}select{width:80px}");
  client.println("fieldset{border:1px solid #3e3e44;border-radius:6px;padding:12px;margin:12px 0}");
  client.println("legend{color:#5e6ad2;font-weight:bold}</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("Protection", "保護制御"));
  printNavLinks(client);
  client.printf("<div class=sec id=pstat>%s</div>\n", L("Loading...","読み込み中..."));

  client.println("<form action=/api/protection onsubmit=\"return protSubmit(this,this.querySelector('[type=submit]'))\">");
  client.printf("<fieldset><legend>%s</legend>\n", L("Dew Prevention", "結露対策"));
  client.printf("<p class=note>%s</p>\n", L("Runs circulation fan / heater before sunrise to prevent condensation.",
    "結露防止のため、日の出前に循環ファン/ヒーターを稼働します。"));
  client.println("<table>");
  client.printf("<tr><th>%s</th><td><input type=checkbox name=dew_en aria-label='%s' value=1%s></td></tr>\n", L("Enable","有効"), L("Enable dew prevention","結露対策を有効"), dewCtrl.enabled ? " checked" : "");
  client.printf("<tr><th>%s</th><td><input type=number name=lat value=%.4f min=-90 max=90 step=0.01></td></tr>\n", L("Latitude","緯度"), dewCtrl.latitude);
  client.printf("<tr><th>%s</th><td><input type=number name=lon value=%.4f min=-180 max=180 step=0.01></td></tr>\n", L("Longitude","経度"), dewCtrl.longitude);
  client.printf("<tr><th>%s</th><td><input type=number name=tz value=%d min=-12 max=14></td></tr>\n", L("Timezone (UTC+)","タイムゾーン (UTC+)"), dewCtrl.timezone_h);
  client.printf("<tr><th>%s</th><td><input type=number name=bmin value=%d min=0 max=180></td></tr>\n", L("Before sunrise (min)","日の出前(分)"), dewCtrl.before_sunrise_min);
  client.printf("<tr><th>%s</th><td><input type=number name=amin value=%d min=0 max=180></td></tr>\n", L("After sunrise (min)","日の出後(分)"), dewCtrl.after_sunrise_min);

  // Fan relay select
  client.printf("<tr><th>%s</th><td><select name=dfan>", L("Fan relay","ファンリレー"));
  client.printf("<option value=-1%s>-</option>", dewCtrl.fan_relay_ch < 0 ? " selected" : "");
  for (int c = 0; c < 8; c++)
    client.printf("<option value=%d%s>CH%d</option>", c, dewCtrl.fan_relay_ch == c ? " selected" : "", c + 1);
  client.printf("</select></td></tr>\n");

  // Heater relay select
  client.printf("<tr><th>%s</th><td><select name=dheat>", L("Heater relay","ヒーターリレー"));
  client.printf("<option value=-1%s>-</option>", dewCtrl.heater_relay_ch < 0 ? " selected" : "");
  for (int c = 0; c < 8; c++)
    client.printf("<option value=%d%s>CH%d</option>", c, dewCtrl.heater_relay_ch == c ? " selected" : "", c + 1);
  client.printf("</select></td></tr>\n");

  // Side window aperture for dew prevention
  client.printf("<tr><th>%s</th><td><select name=dapt>", L("Side window slot","側窓スロット"));
  client.printf("<option value=-1%s>-</option>", dewCtrl.apt_slot < 0 ? " selected" : "");
  for (int s = 0; s < APT_SLOTS; s++)
    client.printf("<option value=%d%s>Slot %d</option>", s, dewCtrl.apt_slot == s ? " selected" : "", s + 1);
  client.printf("</select></td></tr>\n");
  client.printf("<tr><th>%s</th><td><input type=number name=dapo value=%.0f min=0 max=100></td></tr>\n", L("Apt open %","側窓開度%"), dewCtrl.apt_open_pct);
  client.printf("<tr><th>%s</th><td><input type=number name=dapc value=%.0f min=0 max=100></td></tr>\n", L("Apt close %","終了時開度%"), dewCtrl.apt_close_pct);
  client.printf("<tr><th>%s</th><td><input type=number name=damt value=%.1f min=-10 max=40 step=0.5></td></tr>\n", L("Min temp (C)","低温ガード(C)"), dewCtrl.apt_min_temp);
  client.println("</table></fieldset>");

  client.printf("<fieldset><legend>%s</legend>\n", L("Temp Rate Guard", "温度急変対策"));
  client.printf("<p class=note>%s</p>\n", L("Activates fan when temperature rises too fast (e.g. sudden sun exposure).",
    "温度が急上昇した際（例：急激な日射）にファンを稼働します。"));
  client.println("<table>");
  client.printf("<tr><th>%s</th><td><input type=checkbox name=rate_en aria-label='%s' value=1%s></td></tr>\n", L("Enable","有効"), L("Enable rate guard","温度急変対策を有効"), rateGuard.enabled ? " checked" : "");
  client.printf("<tr><th>%s</th><td><input type=number name=rthr value=%.1f min=0.5 max=10 step=0.1></td></tr>\n", L("Threshold (C/min)","閾値(C/min)"), rateGuard.rate_threshold);

  // Sensor select
  client.printf("<tr><th>%s</th><td><select name=rsrc>", L("Sensor","センサー"));
  client.printf("<option value=0%s>SHT40</option>", rateGuard.sensor_src == 0 ? " selected" : "");
  client.printf("<option value=1%s>DS18B20</option>", rateGuard.sensor_src == 1 ? " selected" : "");
  client.printf("</select></td></tr>\n");

  // Fan relay
  client.printf("<tr><th>%s</th><td><select name=rfan>", L("Fan relay","ファンリレー"));
  client.printf("<option value=-1%s>-</option>", rateGuard.fan_relay_ch < 0 ? " selected" : "");
  for (int c = 0; c < 8; c++)
    client.printf("<option value=%d%s>CH%d</option>", c, rateGuard.fan_relay_ch == c ? " selected" : "", c + 1);
  client.printf("</select></td></tr>\n");

  client.printf("<tr><th>%s</th><td><input type=number name=rhld value=%d min=30 max=600></td></tr>\n", L("Hold time (s)","保持時間(s)"), rateGuard.hold_sec);
  client.println("</table></fieldset>");

  client.printf("<fieldset><legend>%s</legend>\n", L("CO2 Guard (requires SCD41)", "CO2ガード（SCD41必要）"));
  client.println("<table>");
  client.printf("<tr><th>%s</th><td><input type=checkbox name=co2_en aria-label='%s' value=1%s></td></tr>\n", L("Enable","有効"), L("Enable CO2 guard","CO2ガードを有効"), co2Guard.enabled ? " checked" : "");
  client.printf("<tr><th>%s</th><td><input type=number name=co2thr value=%d min=50 max=1000></td></tr>\n", L("Threshold (ppm)","閾値(ppm)"), co2Guard.threshold_ppm);
  client.println("</table>");
  client.printf("<p class=note>%s</p>\n", L("Actions: when CO2 &le; threshold, each relay turns ON for its own duration.",
    "動作: CO2が閾値以下になると各リレーが指定時間ONになります。"));
  client.printf("<table><tr><th>#</th><th>%s</th><th>%s</th></tr>\n", L("Relay CH","リレーCH"), L("Duration(s)","持続時間(s)"));
  for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
    client.printf("<tr><td>%d</td><td><select name=ca%d>", i + 1, i);
    client.printf("<option value=-1%s>-</option>", co2Guard.actions[i].relay_ch < 0 ? " selected" : "");
    for (int c = 0; c < 8; c++)
      client.printf("<option value=%d%s>CH%d</option>", c, co2Guard.actions[i].relay_ch == c ? " selected" : "", c + 1);
    client.printf("</select></td>");
    client.printf("<td><input type=number name=cd%d value=%d min=10 max=7200></td></tr>\n", i, co2Guard.actions[i].duration_sec);
  }
  client.println("</table></fieldset>");

  client.printf("<input type=submit value='%s'>\n", L("Save All","全て保存"));
  client.println("</form>");

  // Auto-refresh + form JS
  client.println("<script>");
  client.print(FORM_JS);
  client.println("function protSubmit(form,btn){");
  client.println("var dew=form.querySelector('[name=dew_en]'),rate=form.querySelector('[name=rate_en]'),co2=form.querySelector('[name=co2_en]');");
  client.printf("if(!(dew&&dew.checked)&&!(rate&&rate.checked)&&!(co2&&co2.checked)&&!confirm('%s'))return false;\n",
    L("All protection rules will be disabled. Continue?", "全保護ルールが無効になります。続けますか？"));
  client.println("var rthr=form.querySelector('[name=rthr]');");
  client.println("if(rthr&&parseFloat(rthr.value)<=0){alert(LANG==='jp'?'変化率閾値は0より大きく設定してください':'Rate threshold must be > 0');return false;}");
  client.println("return submitForm(form,btn);}");
  client.println("function pLoad(){");
  client.println("fetch('/api/state').then(function(r){return r.json();}).then(function(d){");
  client.println("var p=d.protection||{};var s='<h3>Status</h3>';");
  client.println("if(p.dew){var dw=p.dew;");
  client.println("  s+='<b>Dew:</b> '+(dw.enabled?'<span class=on>\\u2713 Enabled</span>':'<span class=off>\\u2717 Disabled</span>');");
  client.println("  if(dw.sunrise)s+=' | <b>Sunrise:</b> '+dw.sunrise;");
  client.println("  s+=' | <b>State:</b> '+(dw.active?'<span class=on>ACTIVE</span>':'<span class=off>Standby</span>');");
  client.println("  s+='<br>';}");
  client.println("if(p.rate){var rt=p.rate;");
  client.println("  s+='<b>Rate Guard:</b> '+(rt.enabled?'<span class=on>\\u2713 Enabled</span>':'<span class=off>\\u2717 Disabled</span>');");
  client.println("  if(rt.current_rate!==null)s+=' | <b>Rate:</b> '+rt.current_rate.toFixed(2)+' C/min';");
  client.println("  s+=' | <b>State:</b> '+(rt.active?'<span class=on>ACTIVE</span>':'<span class=off>Normal</span>');}");
  client.println("if(p.co2){var c2=p.co2;s+='<br>';");
  client.println("  s+='<b>CO2 Guard:</b> '+(c2.enabled?'<span class=on>Enabled</span>':'<span class=off>Disabled</span>');");
  client.println("  s+=' | <b>Threshold:</b> '+c2.threshold_ppm+'ppm';");
  client.println("  s+=' | <b>State:</b> '+(c2.active?'<span class=on>ACTIVE</span>':'<span class=off>Normal</span>');}");
  client.println("document.getElementById('pstat').innerHTML=s;");
  client.println("});}");
  client.println("pLoad();setInterval(pLoad,3000);");
  client.println("</script>");
  client.println("</body></html>");
}

// ============================================================
// POST /api/protection
// ============================================================
void handleProtectionPost(WiFiClient& client, const String& body) {
  auto getField = [&](const String& key) -> String {
    String search = key + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('&', idx);
    if (end < 0) end = body.length();
    return body.substring(idx, end);
  };

  // Dew
  dewCtrl.enabled = (getField("dew_en") == "1");
  String lat = getField("lat");  if (lat.length() > 0) dewCtrl.latitude = lat.toFloat();
  String lon = getField("lon");  if (lon.length() > 0) dewCtrl.longitude = lon.toFloat();
  String tz  = getField("tz");   if (tz.length() > 0)  dewCtrl.timezone_h = tz.toInt();
  String bm  = getField("bmin"); if (bm.length() > 0)  dewCtrl.before_sunrise_min = bm.toInt();
  String am  = getField("amin"); if (am.length() > 0)  dewCtrl.after_sunrise_min = am.toInt();
  String df  = getField("dfan"); if (df.length() > 0)  dewCtrl.fan_relay_ch = df.toInt();
  String dh  = getField("dheat");if (dh.length() > 0)  dewCtrl.heater_relay_ch = dh.toInt();
  String da  = getField("dapt"); if (da.length() > 0)  dewCtrl.apt_slot = constrain(da.toInt(), -1, APT_SLOTS - 1);
  String dao = getField("dapo"); if (dao.length() > 0) dewCtrl.apt_open_pct = constrain(dao.toFloat(), 0.0, 100.0);
  String dac = getField("dapc"); if (dac.length() > 0) dewCtrl.apt_close_pct = constrain(dac.toFloat(), 0.0, 100.0);
  String dmt = getField("damt"); if (dmt.length() > 0) dewCtrl.apt_min_temp = dmt.toFloat();

  // Rate Guard
  rateGuard.enabled = (getField("rate_en") == "1");
  String rt = getField("rthr"); if (rt.length() > 0) rateGuard.rate_threshold = rt.toFloat();
  String rs = getField("rsrc"); if (rs.length() > 0) rateGuard.sensor_src = rs.toInt();
  String rf = getField("rfan"); if (rf.length() > 0) rateGuard.fan_relay_ch = rf.toInt();
  String rh = getField("rhld"); if (rh.length() > 0) rateGuard.hold_sec = rh.toInt();

  // CO2 Guard
  co2Guard.enabled = (getField("co2_en") == "1");
  String co2t = getField("co2thr"); if (co2t.length() > 0) co2Guard.threshold_ppm = co2t.toInt();
  for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
    String ca = getField(String("ca") + i); if (ca.length() > 0) co2Guard.actions[i].relay_ch = ca.toInt();
    String cd = getField(String("cd") + i); if (cd.length() > 0) co2Guard.actions[i].duration_sec = cd.toInt();
  }

  // Reset runtimes
  dewRun.last_calc_day = -1;  // force recalc
  dewRun.active = false;
  dewRun.apt_opened = false;
  rateRun = {NAN, 0, 0.0, false, 0};
  co2Run = {};

  saveDewConfig();
  saveRateGuardConfig();
  saveCO2GuardConfig();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true}");
}
