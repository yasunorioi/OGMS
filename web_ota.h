#pragma once

// ============================================================
// OTA Firmware Update Page (GET /ota)
// ============================================================
void sendOTAPage(WiFiClient& client) {
  sendCommonHead(client, L("Firmware Update", "ファームウェア更新"));
  client.println("<style>.sec{padding:12px}");
  client.println("#prog{width:100%;height:24px;background:#2e2e2e;border-radius:4px;margin:10px 0;display:none}");
  client.println("#progBar{height:100%;background:#1976d2;border-radius:4px;width:0%;transition:width 0.3s}");
  client.println("#msg{margin:10px 0;font-weight:bold}");
  client.println("input[type=file]{margin:8px 0}");
  client.println("button{background:#1976d2;color:#fff;border:none;padding:8px 20px;border-radius:4px;cursor:pointer}"); // button (not input[type=submit]) intentional: XHR upload requires JS onclick, not form submit
  client.println("button:disabled{background:#555}</style></head><body>");
  client.printf("<h2>%s</h2>\n", L("Firmware Update", "ファームウェア更新"));
  printNavLinks(client);
  client.printf("<div class=sec><p>%s <b>%s</b> v%s</p>\n", L("Current:", "現在:"), FW_NAME, FW_VERSION);
  client.printf("<p class=note>%s</p>\n", L("Select a .bin firmware file compiled with arduino-cli.",
    "arduino-cliでコンパイルした.binファームウェアファイルを選択してください。"));
  client.println("<input type=file id=fw accept='.bin'><br>");
  client.printf("<button id=btn onclick=doOTA()>%s</button>\n", L("Upload &amp; Flash", "アップロード＆書込み"));
  client.println("<div id=prog><div id=progBar></div></div>");
  client.println("<div id=msg></div></div>");
  client.println("<script>");
  client.println("function doOTA(){");
  client.println("var f=document.getElementById('fw').files[0];");
  client.println("if(!f){alert('Select a file');return;}");
  client.println("var btn=document.getElementById('btn');btn.disabled=true;");
  client.println("var msg=document.getElementById('msg');");
  client.println("var prog=document.getElementById('prog');prog.style.display='block';");
  client.println("var bar=document.getElementById('progBar');");
  client.println("msg.textContent='Uploading '+f.name+' ('+f.size+' bytes)...';");
  client.println("var xhr=new XMLHttpRequest();");
  client.println("xhr.open('POST','/api/ota',true);");
  client.println("xhr.setRequestHeader('Content-Type','application/octet-stream');");
  client.println("xhr.upload.onprogress=function(e){if(e.lengthComputable)bar.style.width=Math.round(e.loaded/e.total*100)+'%';};");
  client.println("xhr.onload=function(){");
  client.println("if(xhr.status==200){msg.textContent=LANG==='jp'?'成功！再起動中...':'Success! Rebooting...';bar.style.width='100%';bar.style.background='#66bb6a';");
  client.println("setTimeout(function(){window.location='/';},8000);}");
  client.println("else{msg.textContent=(LANG==='jp'?'エラー: ':'Error: ')+xhr.responseText;btn.disabled=false;bar.style.background='#ef5350';}};");
  client.println("xhr.onerror=function(){msg.textContent=LANG==='jp'?'アップロード失敗（接続断）。デバイスが再起動中の可能性があります...':'Upload failed (connection lost). Device may be rebooting...';");
  client.println("setTimeout(function(){window.location='/';},8000);};");
  client.println("xhr.send(f);}");
  client.println("</script></body></html>");
}

// ============================================================
// OTA Upload Handler (POST /api/ota)
// Receives raw binary firmware via Content-Type: application/octet-stream
// ============================================================
void handleOTAUpload(WiFiClient& client, int contentLength) {
  if (contentLength <= 0 || contentLength > 8 * 1024 * 1024) {
    client.println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid size");
    return;
  }

  Serial.printf("[OTA] Starting update, size=%d bytes\n", contentLength);

  if (!Update.begin(contentLength, U_FLASH)) {
    Serial.printf("[OTA] Update.begin failed: %d\n", Update.getError());
    client.println("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\nUpdate.begin failed");
    return;
  }

  uint8_t buf[4096];
  size_t written = 0;
  unsigned long lastActivity = millis();

  while (written < (size_t)contentLength) {
    watchdog_update();

    int avail = client.available();
    if (avail > 0) {
      int toRead = min(avail, (int)sizeof(buf));
      int rd = client.readBytes(buf, toRead);
      if (rd > 0) {
        size_t wr = Update.write(buf, rd);
        if (wr != (size_t)rd) {
          Serial.printf("[OTA] Write mismatch: rd=%d wr=%zu\n", rd, wr);
          client.println("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\nWrite failed");
          Update.end();
          return;
        }
        written += rd;
        lastActivity = millis();
        if ((written % 65536) < (size_t)rd) {
          Serial.printf("[OTA] %zu / %d bytes\n", written, contentLength);
        }
      }
    } else if (millis() - lastActivity > 30000) {
      Serial.println("[OTA] Timeout waiting for data");
      client.println("HTTP/1.1 408 Request Timeout\r\nConnection: close\r\n\r\nTimeout");
      Update.end();
      return;
    } else {
      delay(1);
    }
  }

  if (Update.end(true)) {
    Serial.printf("[OTA] Success! MD5: %s\n", Update.md5String().c_str());
    client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK");
    client.flush();
    delay(500);
    client.stop();
    delay(1000);
    rebootWithReason("ota_update");
  } else {
    Serial.printf("[OTA] end() failed: %d\n", Update.getError());
    client.print("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\nUpdate failed: err=");
    client.println(Update.getError());
  }
}
