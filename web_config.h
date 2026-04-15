#pragma once

// ============================================================
// Network Config Page (GET /config)
// ============================================================
void sendConfigPage(WiFiClient& client) {
  String curIp, curSubnet = DEFAULT_SUBNET, curGateway = DEFAULT_GATEWAY, curDns = DEFAULT_DNS;
  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    if (f) {
      JsonDocument cfgDoc;
      if (!deserializeJson(cfgDoc, f)) {
        curIp      = (const char*)(cfgDoc["ip"]      | "");
        curSubnet  = (const char*)(cfgDoc["subnet"]  | DEFAULT_SUBNET);
        curGateway = (const char*)(cfgDoc["gateway"] | DEFAULT_GATEWAY);
        curDns     = (const char*)(cfgDoc["dns"]     | DEFAULT_DNS);
      }
      f.close();
    }
  }

  sendCommonHead(client, L("Config - OGMS", "設定 - OGMS"));
  client.println("<style>label{display:block;margin:6px 0 2px}");
  client.println("input[type=text],input[type=number]{width:220px;padding:4px;background:#1a1a1f;color:#eee;border:1px solid #3e3e44;border-radius:3px}</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("Network Configuration", "ネットワーク設定"));
  printNavLinks(client);
  client.printf("<form action=/api/config onsubmit=\"if(!confirm('%s'))return false;return submitForm(this,this.querySelector('[type=submit]'))\">\n",
    L("Save and reboot?", "保存して再起動しますか？"));
  client.printf("<div class=sec><h3>%s</h3>\n", L("Identity", "識別情報"));
  client.printf("<label for=node_id>node_id</label><input id=node_id type=text name=node_id value='%s'>\n", nodeId.c_str());
  client.printf("<label for=node_name>node_name</label><input id=node_name type=text name=node_name value='%s'>\n", nodeName.c_str());
  client.println("</div>");
  client.printf("<div class=sec><h3>%s</h3>\n", L("IP Address", "IPアドレス"));
  client.printf("<p class=note>%s</p>\n", L("Leave IP blank for DHCP.", "IPを空白にするとDHCPになります。"));
  client.printf("<label>IP<input type=text name=ip value='%s' placeholder='DHCP'></label>\n", curIp.c_str());
  client.printf("<label>Subnet<input type=text name=subnet value='%s'></label>\n", curSubnet.c_str());
  client.printf("<label>Gateway<input type=text name=gateway value='%s'></label>\n", curGateway.c_str());
  client.printf("<label>DNS<input type=text name=dns value='%s'></label>\n", curDns.c_str());
  client.println("</div>");
  client.println("<div class=sec><h3>mDNS</h3>");
  client.printf("<label>Hostname<input type=text name=mdns_hostname value='%s' maxlength=32 placeholder='uecs-ccm-01'></label>\n", mdnsHostname.c_str());
  client.printf("<p class=note>%s</p>\n", L("Access via <b>&lt;hostname&gt;.local</b>", "<b>&lt;hostname&gt;.local</b> でアクセス可能"));
  client.printf("<label><input type=checkbox name=mdns_enabled value=1%s> %s</label>\n",
                mdns_enabled ? " checked" : "", L("Enable mDNS", "mDNS有効"));
  client.println("</div>");
  client.printf("<input type=submit value='%s'>\n", L("Save &amp; Reboot", "保存して再起動"));
  client.println("</form>");
  client.println("<script>");
  client.print(FORM_JS);
  client.println("</script>");
  client.println("</body></html>");
}
// ============================================================
// POST /api/config — save network config and reboot
// ============================================================
void handleConfigPost(WiFiClient& client, const String& body) {
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

  String newNodeId       = getField("node_id");
  String newNodeName     = getField("node_name");
  String newMdnsHostname = getField("mdns_hostname");
  String newIp           = getField("ip");
  String newSubnet       = getField("subnet");
  String newGateway      = getField("gateway");
  String newDns          = getField("dns");
  String newMdnsStr      = getField("mdns_enabled");

  if (newNodeId.length() == 0)       newNodeId       = nodeId;
  if (newNodeName.length() == 0)     newNodeName     = nodeName;
  if (newMdnsHostname.length() == 0) newMdnsHostname = mdnsHostname;
  if (newSubnet.length() == 0)       newSubnet       = DEFAULT_SUBNET;
  if (newGateway.length() == 0)      newGateway      = DEFAULT_GATEWAY;
  if (newDns.length() == 0)          newDns          = DEFAULT_DNS;
  bool newMdns = (newMdnsStr == "1");

  JsonDocument doc;
  doc["node_id"]        = newNodeId;
  doc["node_name"]      = newNodeName;
  doc["mdns_hostname"]  = newMdnsHostname;
  doc["mdns_enabled"]   = newMdns;
  if (newIp.length() > 0) {
    doc["ip"]      = newIp;
    doc["subnet"]  = newSubnet;
    doc["gateway"] = newGateway;
    doc["dns"]     = newDns;
  }

  // Preserve rs485_baud
  if (LittleFS.exists("/config.json")) {
    File rf = LittleFS.open("/config.json", "r");
    if (rf) {
      JsonDocument old;
      if (!deserializeJson(old, rf) && !old["rs485_baud"].isNull()) {
        doc["rs485_baud"] = old["rs485_baud"];
      }
      rf.close();
    }
  }

  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"ok\":false,\"error\":\"Save failed\"}");
    return;
  }
  serializeJson(doc, f);
  f.close();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"ok\":true,\"reboot\":true}");
  client.flush();
  delay(500);
  rebootWithReason("config_saved");
}
