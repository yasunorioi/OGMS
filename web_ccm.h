#pragma once

// ============================================================
// CCM Config Page (GET /ccm)
// ============================================================
void sendCcmConfigPage(WiFiClient& client) {
  sendCommonHead(client, L("CCM Config", "CCM設定"));
  client.println("<style>input[type=number]{width:60px}select{width:140px}</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("CCM Channel Mapping", "CCMチャンネル割当"));
  printNavLinks(client);
  client.printf("<p class=note>%s</p>\n", L("Map each relay channel to a UECS-CCM actuator type. Blank = unmapped (inactive).",
    "各リレーチャンネルにUECS-CCMアクチュエータタイプを割り当てます。空白=未割当（無効）。"));
  client.printf("<div class=sec><h3>%s</h3>\n", L("Bulk Set", "一括設定"));
  client.printf("<label>%s: <input type=number id=bulkRoom min=1 max=999 style='width:60px'></label>", L("Room","部屋"));
  client.printf(" <button type=button onclick=\"var v=document.getElementById('bulkRoom').value;if(v)for(var i=0;i<8;i++)document.getElementsByName('room'+i)[0].value=v;\">%s</button>", L("Apply to All","全適用"));
  client.printf(" &nbsp; <label>%s: <input type=number id=bulkRegion min=1 max=999 style='width:60px'></label>", L("Region","リージョン"));
  client.printf(" <button type=button onclick=\"var v=document.getElementById('bulkRegion').value;if(v)for(var i=0;i<8;i++)document.getElementsByName('region'+i)[0].value=v;\">%s</button>\n", L("Apply to All","全適用"));
  client.println("</div>");
  client.println("<form action=/api/ccm onsubmit=\"return submitForm(this,this.querySelector('[type=submit]'))\">");
  client.printf("<table><tr><th>CH</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>WDT(s)</th><th>%s</th></tr>\n",
    L("CCM Type","CCMタイプ"), L("Room","部屋"), L("Region","リージョン"),
    L("Order","順序"), L("Priority","優先度"), L("DI Link","DIリンク"));

  for (int i = 0; i < 8; i++) {
    client.printf("<tr><td>%d</td><td><select name=type%d>", i + 1, i);
    for (int t = 0; t < CCM_ACTUATOR_TYPES_COUNT; t++) {
      bool sel = (strcmp(ccmMap[i].ccmType, CCM_ACTUATOR_TYPES[t]) == 0);
      client.printf("<option value='%s'%s>%s</option>",
                    CCM_ACTUATOR_TYPES[t],
                    sel ? " selected" : "",
                    CCM_ACTUATOR_TYPES[t][0] == '\0' ? "(none)" : CCM_ACTUATOR_TYPES[t]);
    }
    client.printf("</select></td>");
    client.printf("<td><input type=number name=room%d value=%d min=1 max=999></td>", i, ccmMap[i].room);
    client.printf("<td><input type=number name=region%d value=%d min=1 max=999></td>", i, ccmMap[i].region);
    client.printf("<td><input type=number name=order%d value=%d min=1 max=99></td>", i, ccmMap[i].order);
    client.printf("<td><input type=number name=pri%d value=%d min=1 max=99></td>", i, ccmMap[i].priority);
    client.printf("<td><input type=number name=wdt%d value=%d min=0 max=3600></td>", i, ccmMap[i].watchdog_sec);
    // DI Link dropdown: -1=none, 0-7=DI1-8, + invert checkbox
    client.printf("<td><select name=dil%d>", i);
    client.printf("<option value=-1%s>(none)</option>", ccmMap[i].di_link < 0 ? " selected" : "");
    for (int d = 0; d < 8; d++) {
      client.printf("<option value=%d%s>DI%d</option>", d, ccmMap[i].di_link == d ? " selected" : "", d + 1);
    }
    client.printf("</select> <label><input type=checkbox name=dii%d aria-label='Invert DI %d' value=1%s>inv</label></td></tr>",
                  i, i + 1, ccmMap[i].di_invert ? " checked" : "");
  }

  client.println("</table>");
  client.printf("<input type=submit value='%s'>\n", L("Save CCM Mapping","CCM割当を保存"));
  client.println("</form>");
  client.printf("<p class=note>%s</p>\n", L("Changes take effect immediately (no reboot required).",
    "変更は即時反映されます（再起動不要）。"));
  client.println("<script>");
  client.print(FORM_JS);
  client.println("</script>");
  client.println("</body></html>");
}

// ============================================================
// POST /api/ccm — save CCM mapping (no reboot)
// ============================================================
void handleCcmConfigPost(WiFiClient& client, const String& body) {
  auto getField = [&](const String& key) -> String {
    String search = key + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('&', idx);
    if (end < 0) end = body.length();
    String val = body.substring(idx, end);
    val.replace('+', ' ');
    String decoded;
    for (int i = 0; i < (int)val.length(); i++) {
      if (val[i] == '%' && i + 2 < (int)val.length()) {
        char hex[3] = { val[i+1], val[i+2], '\0' };
        decoded += (char)strtol(hex, nullptr, 16);
        i += 2;
      } else {
        decoded += val[i];
      }
    }
    return decoded;
  };

  for (int i = 0; i < 8; i++) {
    String t = getField(String("type") + i);
    strncpy(ccmMap[i].ccmType, t.c_str(), sizeof(ccmMap[i].ccmType) - 1);
    ccmMap[i].ccmType[sizeof(ccmMap[i].ccmType) - 1] = '\0';

    String r = getField(String("room") + i);
    if (r.length() > 0) ccmMap[i].room = r.toInt();

    String rg = getField(String("region") + i);
    if (rg.length() > 0) ccmMap[i].region = rg.toInt();

    String o = getField(String("order") + i);
    if (o.length() > 0) ccmMap[i].order = o.toInt();

    String p = getField(String("pri") + i);
    if (p.length() > 0) ccmMap[i].priority = p.toInt();

    String w = getField(String("wdt") + i);
    if (w.length() > 0) ccmMap[i].watchdog_sec = w.toInt();

    String dl = getField(String("dil") + i);
    if (dl.length() > 0) ccmMap[i].di_link = dl.toInt();

    // Checkbox: present=1, absent=not in form data
    String di = getField(String("dii") + i);
    ccmMap[i].di_invert = (di == "1");
  }

  saveCcmMapping();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true}");
}
