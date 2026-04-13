#pragma once

// ============================================================
// API: /api/state
// ============================================================
void sendAPIState(WiFiClient& client) {
  JsonDocument doc;
  doc["node_id"]    = nodeId;
  doc["node_name"]  = nodeName;
  doc["version"]    = FW_VERSION;
  doc["protocol"]   = "UECS-CCM";
  doc["uptime"]     = millis() / 1000;
  doc["ts"]         = getCurrentEpoch();
  doc["relay_state"] = relayState;

  // Relay claims (arbitration debug)
  JsonArray claimsArr = doc["relay_claims"].to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    claimsArr.add(relayClaims[i]);
  }

  // DI as bitmask
  uint8_t diBits = 0;
  for (int i = 0; i < 8; i++) {
    if (diState[i]) diBits |= (1 << i);
  }
  doc["di_state"]   = diBits;

  // CCM mapping
  JsonArray ccmArr = doc["ccm_map"].to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    JsonObject m = ccmArr.add<JsonObject>();
    m["type"]     = ccmMap[i].ccmType;
    m["room"]     = ccmMap[i].room;
    m["region"]   = ccmMap[i].region;
    m["order"]    = ccmMap[i].order;
    m["priority"]     = ccmMap[i].priority;
    m["watchdog_sec"] = ccmMap[i].watchdog_sec;
    m["last_rx_ago"]  = lastCcmRx[i] > 0 ? (int)((millis() - lastCcmRx[i]) / 1000) : -1;
  }

  // Sensor
  JsonObject sensor = doc["sensor"].to<JsonObject>();
  if (!isnan(g_sht40_temp)) sensor["temp"] = round(g_sht40_temp * 10) / 10.0;
  else                       sensor["temp"] = nullptr;
  if (!isnan(g_sht40_hum))  sensor["hum"]  = round(g_sht40_hum * 10) / 10.0;
  else                       sensor["hum"]  = nullptr;
  if (!isnan(g_ds18b20_temp)) sensor["ds18b20_temp"] = round(g_ds18b20_temp * 10) / 10.0;
  else                         sensor["ds18b20_temp"] = nullptr;
  if (!isnan(g_solar_wm2)) sensor["solar_wm2"] = round(g_solar_wm2 * 10) / 10.0;
  else                      sensor["solar_wm2"] = nullptr;
  if (scd41_detected && g_scd41_co2 > 0) {
    sensor["co2"]       = g_scd41_co2;
    sensor["scd41_temp"] = round(g_scd41_temp * 10) / 10.0;
    sensor["scd41_hum"]  = round(g_scd41_hum * 10) / 10.0;
  }

  // Network
  doc["ip"]      = eth.localIP().toString();
  doc["subnet"]  = eth.subnetMask().toString();
  doc["gateway"] = eth.gatewayIP().toString();
  doc["dns"]     = eth.dnsIP().toString();
  doc["sht40_ok"]    = sht40_detected;
  doc["scd41_ok"]    = scd41_detected;
  doc["ds18b20_ok"]  = ds18b20_detected;
  doc["sen0575_ok"]  = sen0575_detected;
  doc["ads1110_ok"]  = ads1110_detected;

  // MAC
  {
    uint8_t mac[6];
    eth.macAddress(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["mac"] = macStr;
  }

  if (mdns_enabled) doc["mdns_hostname"] = mdnsHostname + ".local";
  else              doc["mdns_hostname"] = nullptr;
  doc["language"] = g_language == 1 ? "jp" : "en";

  // Greenhouse control status
  JsonArray ghArr = doc["greenhouse"].to<JsonArray>();
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    JsonObject g = ghArr.add<JsonObject>();
    g["enabled"]  = ghCtrl[i].enabled;
    g["ch"]       = ghCtrl[i].ch;
    g["temp_open"] = ghCtrl[i].temp_open;
    g["temp_full"] = ghCtrl[i].temp_full;
    g["cycle_sec"] = ghCtrl[i].cycle_sec;
    g["sensor"]   = ghCtrl[i].sensor_src == 0 ? "SHT40" : "DS18B20";
    if (!isnan(ghRun[i].lastTemp)) g["temp"] = round(ghRun[i].lastTemp * 10) / 10.0;
    else g["temp"] = nullptr;
    g["duty"]       = round(ghRun[i].duty * 100);
    g["active"]     = ghRun[i].active;
    g["curve_mode"] = ghCtrl[i].curve_mode;
    g["curve_coeff"] = ghCtrl[i].curve_coeff;
  }

  // Aperture (side window) status
  JsonArray aptArr = doc["aperture"].to<JsonArray>();
  for (int i = 0; i < APT_SLOTS; i++) {
    JsonObject a = aptArr.add<JsonObject>();
    a["enabled"]     = aptCtrl[i].enabled;
    a["ch"]          = aptCtrl[i].ch;
    a["current_pct"] = round(aptRun[i].current_pct * 10) / 10.0;
    a["target_pct"]  = round(aptRun[i].target_pct * 10) / 10.0;
    a["moving"]      = aptRun[i].moving;
    a["initializing"] = aptRun[i].initializing;
    a["open_sec"]    = aptCtrl[i].open_seconds;
    a["close_sec"]   = aptCtrl[i].close_seconds;
  }

  // Irrigation control status
  JsonArray irriArr = doc["irrigation"].to<JsonArray>();
  for (int i = 0; i < IRRI_SLOTS; i++) {
    JsonObject ir = irriArr.add<JsonObject>();
    ir["enabled"]        = irriCtrl[i].enabled;
    ir["relay_ch"]       = irriCtrl[i].relay_ch;
    ir["threshold_mj"]   = irriCtrl[i].threshold_mj;
    ir["duration_sec"]   = irriCtrl[i].duration_sec;
    ir["min_wm2"]        = irriCtrl[i].min_wm2;
    ir["drain_stop_sec"] = irriCtrl[i].drain_stop_sec;
    ir["mode"]           = irriCtrl[i].mode;
    ir["accum_mj"]       = round(irriRun[i].accum_mj * 1000) / 1000.0;
    ir["irrigating"]     = irriRun[i].irrigating;
    ir["today_count"]    = irriRun[i].today_count;
    if (irriRun[i].irrigating && irriCtrl[i].mode == 0) {
      ir["remaining_sec"] = irriCtrl[i].duration_sec -
        (int)((millis() - irriRun[i].irri_start) / 1000);
    }
    if (irriCtrl[i].mode == 1) {
      ir["duty"]         = round(irriRun[i].duty * 100);
      ir["drain_rate"]   = round(irriRun[i].last_drain_rate * 1000) / 10.0;
      ir["cycle_flow"]   = irriRun[i].cycle_flow_pulses;
      ir["cycle_drain"]  = irriRun[i].cycle_drain_tips;
    }
  }

  // Protection status (Dew + Rate Guard)
  JsonObject prot = doc["protection"].to<JsonObject>();
  {
    JsonObject dew = prot["dew"].to<JsonObject>();
    dew["enabled"] = dewCtrl.enabled;
    dew["active"]  = dewRun.active;
    if (dewRun.sunrise_min >= 0) {
      char sr[6];
      snprintf(sr, sizeof(sr), "%d:%02d", dewRun.sunrise_min / 60, dewRun.sunrise_min % 60);
      dew["sunrise"] = sr;
    }
    dew["apt_slot"]    = dewCtrl.apt_slot;
    dew["apt_open"]    = dewCtrl.apt_open_pct;
    dew["apt_close"]   = dewCtrl.apt_close_pct;
    dew["apt_min_temp"] = dewCtrl.apt_min_temp;
    dew["apt_opened"]  = dewRun.apt_opened;
    JsonObject rate = prot["rate"].to<JsonObject>();
    rate["enabled"] = rateGuard.enabled;
    rate["active"]  = rateRun.active;
    if (!isnan(rateRun.prev_temp)) rate["current_rate"] = round(rateRun.current_rate * 100) / 100.0;
    else rate["current_rate"] = nullptr;
    JsonObject co2g = prot["co2"].to<JsonObject>();
    co2g["enabled"] = co2Guard.enabled;
    co2g["threshold_ppm"] = co2Guard.threshold_ppm;
    co2g["active"] = co2Run.active;
  }

  // CCM InRadiation cache
  {
    JsonObject cs = doc["ccm_solar"].to<JsonObject>();
    if (!isnan(ccmSolar.wm2)) {
      cs["wm2"]     = round(ccmSolar.wm2 * 10) / 10.0;
      cs["room"]    = ccmSolar.room;
      cs["region"]  = ccmSolar.region;
      cs["order"]   = ccmSolar.order;
      cs["age_sec"] = ccmSolar.last_rx > 0 ? (int)((millis() - ccmSolar.last_rx) / 1000) : -1;
    }
  }

  // Last updated time string
  {
    unsigned long ep = getCurrentEpoch();
    if (ep > 0) {
      unsigned long hh = (ep % 86400) / 3600;
      unsigned long mm = (ep % 3600) / 60;
      unsigned long ss = ep % 60;
      char tstr[10];
      snprintf(tstr, sizeof(tstr), "%02lu:%02lu:%02lu", hh, mm, ss);
      doc["last_updated"] = tstr;
    } else {
      doc["last_updated"] = String(millis() / 1000) + "s";
    }
  }

  char buffer[4096];
  serializeJson(doc, buffer);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.print(buffer);
}

// ============================================================
// API: /api/config
// ============================================================
void sendAPIConfig(WiFiClient& client) {
  JsonDocument doc;
  doc["node_id"]        = nodeId;
  doc["node_name"]      = nodeName;
  doc["mdns_hostname"]  = mdnsHostname;
  doc["mdns_enabled"]   = mdns_enabled;

  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    if (f) {
      JsonDocument cfgDoc;
      if (!deserializeJson(cfgDoc, f)) {
        doc["ip"]      = (const char*)(cfgDoc["ip"]      | "");
        doc["subnet"]  = (const char*)(cfgDoc["subnet"]  | DEFAULT_SUBNET);
        doc["gateway"] = (const char*)(cfgDoc["gateway"] | DEFAULT_GATEWAY);
        doc["dns"]     = (const char*)(cfgDoc["dns"]     | DEFAULT_DNS);
      }
      f.close();
    }
  } else {
    doc["ip"]      = "";
    doc["subnet"]  = DEFAULT_SUBNET;
    doc["gateway"] = DEFAULT_GATEWAY;
    doc["dns"]     = DEFAULT_DNS;
  }

  char buffer[512];
  serializeJson(doc, buffer);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.print(buffer);
}

// ============================================================
// Relay POST (WebUI)
// ============================================================
void handleRelayPost(WiFiClient& client, int ch, const String& body) {
  if (ch < 1 || ch > 8) {
    client.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    client.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n");
    return;
  }

  int value = doc["value"] | -1;
  if (value == 1) claimRelay(ch, OWN_MANUAL);
  else if (value == 0) releaseRelay(ch, OWN_MANUAL);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.printf("{\"ok\":true,\"ch\":%d}\n", ch);
}
