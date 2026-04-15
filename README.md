# OGMS — Open Greenhouse Management System (RP2350B)

Arduino firmware for **Waveshare RP2350-ETH-8DI-8RO / RP2350-POE-ETH-8DI-8RO**.

8chリレー + I2Cセンサー → **MQTT** でセンサー値を送信し、MQTT指令でリレーを制御。  
温室制御・灌水・保護機能を搭載しスタンドアロンでも動作。EN/JP 言語切替対応。

> UECS-CCM（ArSprout直接連携）が必要な場合は [ccm_rp2350_relay](https://github.com/yasunorioi/ccm_rp2350_relay) を使用してください。

### ボード購入先

| ボード | 購入先 |
|--------|--------|
| RP2350-ETH-8DI-8RO | [Waveshare公式](https://www.waveshare.com/rp2350-eth-8di-8ro.htm) / [スイッチサイエンス (Waveshare製品)](https://www.switch-science.com/) / [Amazon](https://www.amazon.com/dp/B0FVF19VRR) |
| RP2350-POE-ETH-8DI-8RO (PoE版) | [Waveshare公式](https://www.waveshare.com/rp2350-poe-eth-8di-8ro.htm) |
| M5Stack ADC Unit V1.1 (日射センサー用) | [M5Stack公式](https://shop.m5stack.com/products/adc-i2c-unit-v1-1-ads1100) / [スイッチサイエンス](https://www.switch-science.com/) |

## 機能一覧

| 機能 | 実装 |
|------|------|
| 8ch リレー制御 | GPIO17-24 直接制御 |
| 8ch デジタル入力 | GPIO9-16, フォトカプラ絶縁, アクティブLOW, 割り込み検知 |
| DI→リレー連動 | ch毎にDI紐付け、反転モード対応（フロートスイッチ→灌水停止等） |
| **リレー調停レイヤー** | 8制御者のclaim/release → OR合成(換気系) / AND合成(暖房系) |
| 通信 | **MQTT over TCP** (PubSubClient 2.8) |
| **センサー値Publish** | MQTTで各センサートピックに送信（publish_interval秒間隔、QoS1, Retained） |
| **リレー制御受信** | `agriha/{house_id}/relay/{ch}/set` でリレーON/OFF（duration_sec対応） |
| **外気データ受信** | `agriha/farm/weather/#` をsubscribe → 日射フォールバック等 |
| **Last Will** | ブローカー切断時 `agriha/{house_id}/status` に `{"online":false}` 配信 |
| 無通信duration | MQTT受信後N秒でリレー自動OFF (ch別設定、WebUI /mqtt) |
| I2Cセンサー | SHT40 自動検出 → sensor/SHT40 としてMQTT publish |
| **CO2センサー** | SCD41 (I2C 0x62) → sensor/SCD41 としてMQTT publish |
| 日射センサー | PVSS-03 + M5Stack ADC Unit (ADS1110 I2C) → sensor/ADS1110 としてMQTT publish |
| 日射比例灌水 | 積算日射量(MJ/m²)閾値で灌水リレー自動ON/OFF (2ルール対応) |
| **排水検知灌水停止** | SEN0575で連続排水N秒検知 → 灌水強制OFF |
| 1-Wire温度 | DS18B20 (GPIO3) 自動検出 → sensor/DS18B20 としてMQTT publish |
| **雨量/排水センサー** | DFRobot SEN0575 (TTL UART, GPIO44/45 SerialPIO) → sensor/SEN0575 としてMQTT publish |
| **温室温度制御** | 温度比例デューティ制御 (4ルール、SHT40/DS18B20選択、4カーブモード) |
| **側窓開度制御** | 開/閉秒数指定の時間比例制御 (4スロット、開閉別リレー) |
| **結露防止** | 日の出前後の時間帯にファン+暖房+側窓連動を自動ON (緯度経度から日の出計算、低温ガード付き) |
| **急昇温ガード** | 温度上昇率が閾値超過でファンON (保持時間付き) |
| **CO2ガード** | CO2低下時に複数リレーを個別時間で同時発動 (側窓+ファン+暖房等) |
| WebUI | ダッシュボード + MQTT + Greenhouse + Irrigation + Protection + Config + OTA |
| **EN/JP言語切替** | WebUI全ページ対応。`/api/language?lang=jp` で切替、設定はLittleFSに永続化 |
| OTA更新 | WebUI `/ota` からブラウザ経由でFW書き込み（10台超の運用に対応） |
| RGB LED | WS2812 (GPIO2) 状態表示: 緑=正常 / 黄=リレー稼働 / 赤=Ethernet断 |
| USB-UARTデバッグ | `status` / `help` / `reboot` コマンド対応 |
| mDNS | `{hostname}.local` (デフォルト: `ogms-01.local`) |
| RTC | PCF85063 (I2C1) NTP同期バックアップ |
| Watchdog 3段 | HW WDT 8s / SW WDT / 定期リブート 10分 |
| 設定永続化 | LittleFS (mqtt_config.json / relay_ch.json / greenhouse.json 等) |

---

## クイックスタート

1. **初回のみ** USB-CでPCに接続、BOOTSELモードでFW書き込み
2. LANケーブル接続 → DHCP で IP 取得
3. ブラウザで `http://ogms-01.local/` → ダッシュボード表示
4. `/mqtt` ページでブローカーIPとハウスIDを設定
5. MQTTブローカーに自動接続 → センサー値のpublish開始
6. 2回目以降のFW更新は `/ota` ページからブラウザでアップロード

> **操作マニュアル**: 各設定項目の詳細は [docs/operation-manual.pdf](docs/operation-manual.pdf) を参照してください。

---

## MQTT接続

### ブローカー設定

WebUI `/mqtt` で設定する。

| 設定項目 | デフォルト | 説明 |
|---------|-----------|------|
| Broker IP | `192.168.100.1` | MQTTブローカーのIPアドレス |
| Port | `1883` | MQTTポート |
| House ID | `h01` | トピックプレフィックス (`agriha/{house_id}/...`) に使用 |
| Client ID | (自動生成) | 空なら `ogms-{MAC末尾4桁}` で自動生成 |
| Publish Interval | `10` 秒 | センサー値送信間隔 |

### MQTTトピック

#### Publish（QoS1, Retained）

| トピック | ペイロード例 | 備考 |
|---------|------------|------|
| `agriha/{house_id}/sensor/SHT40` | `{"temperature_c":23.5,"humidity_pct":65.2,"timestamp":1740000000}` | 室内温湿度 |
| `agriha/{house_id}/sensor/SCD41` | `{"temperature_c":23.1,"humidity_pct":64.8,"co2_ppm":450,"timestamp":...}` | CO2/温湿度 |
| `agriha/{house_id}/sensor/DS18B20` | `{"temperature_c":15.3,"timestamp":...}` | 1-Wire温度 |
| `agriha/{house_id}/sensor/ADS1110` | `{"solar_wm2":523.0,"timestamp":...}` | 日射量 (PVSS-03) |
| `agriha/{house_id}/sensor/SEN0575` | `{"drain_mm":12.4,"timestamp":...}` | 雨量/排水量 |
| `agriha/{house_id}/relay/state` | `{"ch1":0,"ch2":1,...,"ch8":0,"ts":...}` | リレー状態 |
| `agriha/{house_id}/status` | `{"online":true}` | 接続状態 (retained) / Last Will: `{"online":false}` |

NTP未同期時は `"timestamp":0` となる。

#### Subscribe

| トピック | ペイロード例 | 動作 |
|---------|------------|------|
| `agriha/{house_id}/relay/{ch}/set` | `{"value":1,"duration_sec":180,"reason":"LLM"}` | リレーON/OFF。`duration_sec` > 0でN秒後自動OFF |
| `agriha/farm/weather/#` | `{"solar_wm2":600.0}` 等 | 圃場共有気象データ受信 |

### コンシューマ方針（外気データ）

OGMSは外気データの **コンシューマ**。`agriha/farm/weather/#` をsubscribeして取得する。

| サブトピック | データ | OGMS側の用途 |
|------------|--------|------------------|
| `agriha/farm/weather/solar` | 日射量 (W/m²) | 日射比例灌水のフォールバック |
| `agriha/farm/weather/temperature` | 外気温 | 結露防止・急昇温ガード |
| `agriha/farm/weather/wind_speed` | 風速 | 側窓制御判断 |

- ローカルADS1110がある場合はローカル値優先、ない場合はMQTT受信値をフォールバックとして使用
- publisherはRPi側気象サービスでも別OGMSノードでも可。OGMSは関知しない

---

## ボード設定

| 項目 | 値 |
|------|-----|
| Board | **Generic RP2350** |
| Variant Chip | **RP2530B** (RP2350B — GPIO33-36使用のため必須) |
| Flash Size | **16MB (Sketch: 14MB, FS: 2MB)** |
| Upload Method | USB (UF2) 初回 / OTA (以降) |

## 必要ライブラリ

| ライブラリ | バージョン | 用途 |
|-----------|-----------|------|
| arduino-pico | 4.5.2+ | Earle Philhower版コア |
| ArduinoJson | v7.x | JSON設定・API |
| NTPClient | 3.2.1 | 時刻同期 |
| **PubSubClient** | **2.8+** | **MQTT クライアント** |
| SensirionI2cSht4x | 1.1+ | SHT40温湿度 (optional) |
| SensirionI2cScd4x | 1.1+ | SCD41 CO2 (optional) |
| Adafruit NeoPixel | 1.15+ | WS2812 RGB LED |
| OneWire | 2.3.8 | DS18B20 (optional) |
| DallasTemperature | 4.0+ | DS18B20 (optional) |
| W5500lwIP, LEAmDNS, Wire, LittleFS, Updater | arduino-pico内蔵 | |

---

## ビルド

### Arduino CLI

```bash
# ビルド
arduino-cli compile \
  --fqbn "rp2040:rp2040:generic_rp2350:variantchip=RP2530B,flash=16777216_2097152,psramcs=GPIOnone,psram=0mb,freq=150,arch=arm,opt=Small,os=none,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Serial,dbglvl=None,usbstack=picosdk,ipbtstack=ipv4only,uploadmethod=default" \
  .

# OTA更新 (2回目以降)
curl --data-binary @/tmp/ccm_ota/ccm_rp2350_relay.ino.bin \
  -H "Content-Type: application/octet-stream" \
  http://ogms-01.local/api/ota
```

### PlatformIO (VSCode)

`platformio.ini` 同梱。VSCodeで開いて `pio run` でビルド可能。

```ini
[env:rp2350b]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = generic_rp2350
framework = arduino
board_build.core = earlephilhower
board_build.chip = rp2350b
board_build.filesystem_size = 2m
```

---

## WebUI

| パス | メソッド | 説明 |
|------|---------|------|
| `/` | GET | ダッシュボード (リレー+DI+センサー+MQTT接続状態) |
| `/config` | GET | ネットワーク設定 (IP/mDNS) |
| `/mqtt` | GET | MQTT設定 (ブローカー/ハウスID/DI連動/WDTタイマー) |
| `/greenhouse` | GET | 温度比例制御設定 (4ルール) + 側窓開度制御 (4スロット) |
| `/irrigation` | GET | 日射比例灌水設定 (2ルール、排水停止秒数含む) |
| `/protection` | GET | 結露防止 + 急昇温ガード + CO2ガード設定 |
| `/ota` | GET | FW更新ページ (ブラウザからbin選択→アップロード→自動リブート) |
| `/api/state` | GET | 状態JSON (リレー/DI/センサー/MQTT/claims/Greenhouse/Aperture/Irrigation/Protection/CO2) |
| `/api/config` | GET | ネットワーク設定JSON |
| `/api/language` | GET | 言語切替 (`?lang=en` or `?lang=jp`)。302リダイレクトで `/` に戻る |
| `/api/config` | POST | ネットワーク設定保存 → リブート |
| `/api/mqtt` | POST | MQTT設定+DI連動/WDT設定保存 (リブート不要) |
| `/api/greenhouse` | POST | Greenhouse制御設定保存 |
| `/api/aperture` | POST | 側窓開度設定保存 |
| `/api/irrigation` | POST | Irrigation制御設定保存 |
| `/api/protection` | POST | 結露防止/急昇温ガード/CO2ガード設定保存 |
| `/api/relay/{ch}` | POST | リレー手動制御 |
| `/api/ota` | POST | FWバイナリ受信 → フラッシュ → リブート |

---

## センサー接続

| センサー | インターフェース | GPIO | MQTTトピック | 備考 |
|---------|----------------|------|------------|------|
| SHT40 | I2C1 (Grove) | 6/7 | sensor/SHT40 | 自動検出 |
| SCD41 | I2C1 (Grove) | 6/7 | sensor/SCD41 | CO2/温度/湿度。自動検出。5秒間隔 |
| PVSS-03 + ADS1110 | I2C1 (Grove) | 6/7 | sensor/ADS1110 | M5Stack ADC Unit V1.1経由。0-1V=0-1000W/m² |
| DS18B20 | 1-Wire | 3 | sensor/DS18B20 | Grove基板から引出し |
| SEN0575 | TTL UART (SerialPIO) | 44/45 | sensor/SEN0575 | 排水/雨量センサー。Modbus RTU 9600bps |

センサーは全て **自動検出**。未接続でも正常動作する。

> **SEN0575接続注意**: RS485端子(GPIO4/5)ではなく、拡張ヘッダのGPIO44/45にTTL UART接続。ボード上のI2C/UARTスイッチをUART側にすること。

---

## リレー調停レイヤー

複数の制御者が同じリレーchを操作する場合、調停レイヤーが衝突を解決する。

| 制御者 | 内容 |
|--------|------|
| OWN_GH (0) | 温室温度制御 |
| OWN_IRRI (1) | 灌水 |
| OWN_DEW (2) | 結露防止 |
| OWN_RATE (3) | 急昇温ガード |
| OWN_CO2 (4) | CO2ガード |
| OWN_MQTT (5) | MQTT受信 |
| OWN_MANUAL (6) | WebUI手動 / DI連動 |
| OWN_APT (7) | 側窓開度制御 |

| 合成ルール | 動作 | 安全側 |
|-----------|------|--------|
| **OR合成** | 誰か1人でもclaim → ON | 換気不足を防ぐ |
| **AND合成** | 関与者全員がclaim → ON | 不要な加温を防ぐ (暖房系) |

`/api/state` の `relay_claims[]` で各chのビットマスクを確認可能。

---

## 安全機能

### DI→リレー連動

WebUI `/mqtt` ページの「DI Link」テーブルで各リレーchにDI入力を紐付け。ネットワーク断でもローカルで即座に動作。

| 設定 | 用途例 |
|------|--------|
| DI1 → CH1 (normal) | 手元スイッチで換気扇ON |
| DI2 → CH3 (invert) | フロートスイッチON → 灌水ポンプOFF（水位上限） |

### MQTT duration / WDT

- **duration_sec**: `relay/{ch}/set` ペイロードに `duration_sec` を含めるとN秒後に自動OFF
- **WDT (watchdog_sec)**: WebUI `/mqtt` で設定。最終MQTT受信後N秒でリレーを自動OFF (0=無効)
- **接続断**: ブローカー切断を検知すると OWN_MQTT の全claimを即座に解放

---

## ファイル構成

```
.
├── ogms.ino                # メインFW (制御ロジック・MQTT・JSON永続化)
├── web_common.h            # WebUI共通 (CSS・ナビ・JS)
├── web_i18n.h              # EN/JP言語切替 (L()マクロ)
├── web_dashboard.h         # ダッシュボード (GET /)
├── web_config.h            # ネットワーク設定 (GET /config, POST /api/config)
├── web_mqtt.h              # MQTT設定+DI連動+WDT (GET /mqtt, POST /api/mqtt)
├── web_greenhouse.h        # 温室制御+側窓開度 (GET /greenhouse, POST /api/greenhouse, /api/aperture)
├── web_irrigation.h        # 灌水制御 (GET /irrigation, POST /api/irrigation)
├── web_protection.h        # 保護制御 (GET /protection, POST /api/protection)
├── web_ota.h               # OTA更新 (GET /ota, POST /api/ota)
├── web_api.h               # REST API (GET /api/state, /api/config, /api/language)
├── sw_watchdog.h            # SW WDT
├── sensor_registry.h       # I2Cセンサー定義 (SCD41/SHT40/BMP280/BH1750/ADS1110)
├── platformio.ini           # PlatformIO設定
├── docs/
│   ├── operation-manual.md   # 操作マニュアル (Markdown原本)
│   ├── operation-manual.pdf  # 操作マニュアル (PDF)
│   └── pdf-header.tex        # PDF生成用LaTeXヘッダー
└── README.md
```

---

## 実機検証

- [x] W5500 SPI0 (GPIO33-36) 動作確認
- [x] Ethernet DHCP取得 + 通信確認
- [x] mDNS `ogms-01.local` アクセス確認
- [x] MQTT接続 + sensor/SHT40 publish確認
- [x] WebUI 全ページ動作確認
- [x] OTA更新 動作確認 (313KB / ~7秒)
- [x] USB-UART デバッグコマンド確認
- [x] SHT40 I2C検出 + MQTT publish
- [x] SCD41 I2C検出 + CO2読取 + MQTT publish
- [x] SEN0575 SerialPIO (GPIO44/45) 検出 + Modbus RTU通信
- [x] DS18B20 1-Wire検出 + MQTT publish
- [x] リレー調停レイヤー (relay_claims API確認)
- [x] EN/JP言語切替 WebUI動作確認
- [ ] relay/{ch}/set MQTT受信 → リレー制御
- [ ] duration_sec auto-OFF動作確認
- [ ] farm/weather/solar subscribeフォールバック動作
- [ ] Last Will配信確認
- [ ] 全8chリレー ON/OFF
- [ ] DI 8ch 割り込み検知
- [ ] DI→リレー連動
- [ ] ArSprout(uecs-llm)側で送受信確認

### RP2350Bピンマップ注意事項

| 機能 | GPIO | バス | 注意 |
|------|------|------|------|
| I2C SDA/SCL | 6/7 | **I2C1** (I2C0ではない) | `Wire1` を使用 |
| W5500 SPI | 33-36 | **SPI0** (SPI1ではない) | `SPI` を使用 |
| RS485 TX/RX | 4/5 | UART1 | 差動トランシーバ経由。RS485機器専用 |
| SEN0575 TX/RX | 44/45 | **SerialPIO** | 拡張ヘッダ。TTL UART直結 |
| DS18B20 | 3 | 1-Wire | Grove基板から引出し |
| WS2812 | 2 | NeoPixel | ボード上実装済み |

`variantchip=RP2530B` (RP2350B用) でないとGPIO33-36が使えず、ビルドは通るが実行時PANICする。
