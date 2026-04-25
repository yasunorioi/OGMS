# OGMS — Open Greenhouse Management System (RP2350B)

Arduino firmware for **Waveshare RP2350-ETH-8DI-8RO / RP2350-POE-ETH-8DI-8RO**.

8-channel relay + I2C sensors → publishes sensor values via **MQTT**, controls relays via MQTT commands.  
Standalone greenhouse control, irrigation, and protection features built-in. EN/JP WebUI language toggle supported.

> For UECS-CCM (direct ArSprout integration), use [ccm_rp2350_relay](https://github.com/yasunorioi/ccm_rp2350_relay) instead.

<details>
<summary>🇯🇵 日本語</summary>

8chリレー + I2Cセンサー → **MQTT** でセンサー値を送信し、MQTT指令でリレーを制御。  
温室制御・灌水・保護機能を搭載しスタンドアロンでも動作。EN/JP 言語切替対応。

> UECS-CCM（ArSprout直接連携）が必要な場合は [ccm_rp2350_relay](https://github.com/yasunorioi/ccm_rp2350_relay) を使用してください。

</details>

### Board Purchase

| Board | Where to Buy |
|-------|-------------|
| RP2350-ETH-8DI-8RO | [Waveshare Official](https://www.waveshare.com/rp2350-eth-8di-8ro.htm) / [Amazon](https://www.amazon.com/dp/B0FVF19VRR) / [Switch Science](https://www.switch-science.com/) |
| RP2350-POE-ETH-8DI-8RO (PoE) | [Waveshare Official](https://www.waveshare.com/rp2350-poe-eth-8di-8ro.htm) |
| M5Stack ADC Unit V1.1 (for solar sensor) | [M5Stack Official](https://shop.m5stack.com/products/adc-i2c-unit-v1-1-ads1100) / [Switch Science](https://www.switch-science.com/) |

<details>
<summary>🇯🇵 日本語</summary>

| ボード / ケーブル | 購入先 |
|--------|--------|
| RP2350-ETH-8DI-8RO | [Waveshare公式](https://www.waveshare.com/rp2350-eth-8di-8ro.htm) / [Amazon](https://www.amazon.com/dp/B0FVF19VRR) / [スイッチサイエンス](https://www.switch-science.com/) |
| RP2350-POE-ETH-8DI-8RO (PoE版) | [Waveshare公式](https://www.waveshare.com/rp2350-poe-eth-8di-8ro.htm) |
| M5Stack ADC Unit V1.1 (日射センサー用) | [M5Stack公式](https://shop.m5stack.com/products/adc-i2c-unit-v1-1-ads1100) / [スイッチサイエンス](https://www.switch-science.com/) |
| Grove - 4ピンケーブル (I2Cセンサー接続用) | [スイッチサイエンス](https://www.switch-science.com/products/1048) |

</details>

## Architecture

```mermaid
flowchart TD

subgraph group_firmware_core["Firmware core"]
  node_ogms_main(("OGMS core<br/>firmware runtime<br/>[ogms.ino]"))
  node_relay_arbitration["Relay arbiter<br/>control mixer<br/>[ogms.ino]"]
  node_state_persist[("State store<br/>json persistence<br/>[ogms.ino]")]
  node_watchdog["Watchdog<br/>safety timer<br/>[sw_watchdog.h]"]
end

subgraph group_hardware_io["Hardware I/O"]
  node_sensor_registry["Sensor registry<br/>i2c discovery<br/>[sensor_registry.h]"]
  node_i2c_sensors["I2C sensors<br/>sensor bus"]
  node_ds18b20["DS18B20<br/>1-wire temp"]
  node_serial_sen0575["SEN0575<br/>uart sensor"]
  node_gpio_relays["On-board relays<br/>gpio outputs"]
  node_digital_inputs["Digital inputs<br/>gpio inputs"]
  node_status_led["Status LED<br/>ws2812 indicator"]
  node_modbus_master["Modbus master<br/>rs485 actuator bus<br/>[modbus_master.h]"]
end

subgraph group_network_ui["Network and UI"]
  node_mqtt_client["MQTT client<br/>pubsub client<br/>[ogms.ino]"]
  node_web_api["Web API<br/>http/json api<br/>[web_api.h]"]
  node_web_ui["Web UI<br/>hosted pages<br/>[web_dashboard.h]"]
  node_web_common["UI common<br/>shared ui<br/>[web_common.h]"]
  node_maintenance["Maintenance links<br/>transport access<br/>[usb_ncm.h]"]
  node_ota_update["OTA update<br/>firmware update<br/>[web_ota.h]"]
  node_mqtt_config["MQTT config<br/>settings module<br/>[web_mqtt.h]"]
  node_modbus_ui["Modbus UI<br/>settings module<br/>[web_modbus.h]"]
  node_config_ui["Config UI<br/>settings module<br/>[web_config.h]"]
end

subgraph group_control_policies["Control policies"]
  node_greenhouse_control["Greenhouse control<br/>policy module<br/>[web_greenhouse.h]"]
  node_irrigation_control["Irrigation<br/>policy module<br/>[web_irrigation.h]"]
  node_protection_control["Protection<br/>policy module<br/>[web_protection.h]"]
end

node_ogms_main -->|"controls"| node_relay_arbitration
node_ogms_main -->|"loads/saves"| node_state_persist
node_ogms_main -->|"feeds"| node_watchdog
node_ogms_main -->|"discovers"| node_sensor_registry
node_sensor_registry -->|"defines"| node_i2c_sensors
node_ogms_main -->|"polls"| node_ds18b20
node_ogms_main -->|"reads"| node_serial_sen0575
node_ogms_main -->|"drives"| node_gpio_relays
node_ogms_main -->|"samples"| node_digital_inputs
node_ogms_main -->|"indicates"| node_status_led
node_ogms_main -->|"expands to"| node_modbus_master
node_ogms_main -->|"publishes/subscribes"| node_mqtt_client
node_ogms_main -->|"serves"| node_web_api
node_web_api -->|"backs"| node_web_ui
node_web_ui -->|"shares"| node_web_common
node_web_ui -->|"includes"| node_config_ui
node_web_ui -->|"includes"| node_mqtt_config
node_web_ui -->|"includes"| node_greenhouse_control
node_web_ui -->|"includes"| node_irrigation_control
node_web_ui -->|"includes"| node_protection_control
node_web_ui -->|"includes"| node_modbus_ui
node_web_ui -->|"includes"| node_ota_update
node_maintenance -.->|"accesses"| node_ogms_main
node_greenhouse_control -->|"claims relays"| node_relay_arbitration
node_irrigation_control -->|"claims relays"| node_relay_arbitration
node_protection_control -->|"claims relays"| node_relay_arbitration
node_mqtt_client -->|"feeds weather"| node_greenhouse_control
node_mqtt_client -->|"feeds weather"| node_irrigation_control
node_mqtt_client -->|"feeds telemetry"| node_protection_control
node_mqtt_client -->|"commands"| node_ogms_main
node_web_api -->|"updates"| node_state_persist
node_web_api -->|"manages"| node_relay_arbitration

click node_ogms_main "https://github.com/yasunorioi/OGMS/blob/main/ogms.ino"
click node_relay_arbitration "https://github.com/yasunorioi/OGMS/blob/main/ogms.ino"
click node_state_persist "https://github.com/yasunorioi/OGMS/blob/main/ogms.ino"
click node_watchdog "https://github.com/yasunorioi/OGMS/blob/main/sw_watchdog.h"
click node_sensor_registry "https://github.com/yasunorioi/OGMS/blob/main/sensor_registry.h"
click node_modbus_master "https://github.com/yasunorioi/OGMS/blob/main/modbus_master.h"
click node_mqtt_client "https://github.com/yasunorioi/OGMS/blob/main/ogms.ino"
click node_web_api "https://github.com/yasunorioi/OGMS/blob/main/web_api.h"
click node_web_ui "https://github.com/yasunorioi/OGMS/blob/main/web_dashboard.h"
click node_web_common "https://github.com/yasunorioi/OGMS/blob/main/web_common.h"
click node_maintenance "https://github.com/yasunorioi/OGMS/blob/main/usb_ncm.h"
click node_ota_update "https://github.com/yasunorioi/OGMS/blob/main/web_ota.h"
click node_greenhouse_control "https://github.com/yasunorioi/OGMS/blob/main/web_greenhouse.h"
click node_irrigation_control "https://github.com/yasunorioi/OGMS/blob/main/web_irrigation.h"
click node_protection_control "https://github.com/yasunorioi/OGMS/blob/main/web_protection.h"
click node_mqtt_config "https://github.com/yasunorioi/OGMS/blob/main/web_mqtt.h"
click node_modbus_ui "https://github.com/yasunorioi/OGMS/blob/main/web_modbus.h"
click node_config_ui "https://github.com/yasunorioi/OGMS/blob/main/web_config.h"

classDef toneNeutral fill:#f8fafc,stroke:#334155,stroke-width:1.5px,color:#0f172a
classDef toneBlue fill:#dbeafe,stroke:#2563eb,stroke-width:1.5px,color:#172554
classDef toneAmber fill:#fef3c7,stroke:#d97706,stroke-width:1.5px,color:#78350f
classDef toneMint fill:#dcfce7,stroke:#16a34a,stroke-width:1.5px,color:#14532d
classDef toneRose fill:#ffe4e6,stroke:#e11d48,stroke-width:1.5px,color:#881337
classDef toneIndigo fill:#e0e7ff,stroke:#4f46e5,stroke-width:1.5px,color:#312e81
classDef toneTeal fill:#ccfbf1,stroke:#0f766e,stroke-width:1.5px,color:#134e4a
class node_ogms_main,node_relay_arbitration,node_state_persist,node_watchdog toneBlue
class node_sensor_registry,node_i2c_sensors,node_ds18b20,node_serial_sen0575,node_gpio_relays,node_digital_inputs,node_status_led,node_modbus_master toneAmber
class node_mqtt_client,node_web_api,node_web_ui,node_web_common,node_maintenance,node_ota_update,node_mqtt_config,node_modbus_ui,node_config_ui toneMint
class node_greenhouse_control,node_irrigation_control,node_protection_control toneRose
```

## Features

| Feature | Implementation |
|---------|---------------|
| 8ch Relay Control | GPIO17-24 direct control |
| 8ch Digital Input | GPIO9-16, photocoupler-isolated, active-LOW, interrupt detection |
| DI→Relay Link | Per-channel DI binding, invert mode (e.g. float switch → stop irrigation) |
| **Relay Arbitration** | 8 owners claim/release → OR composition (ventilation) / AND composition (heating) |
| Communication | **MQTT over TCP** (PubSubClient 2.8) |
| **Sensor Publish** | Publishes each sensor topic via MQTT (publish_interval seconds, QoS1, Retained) |
| **Relay Control** | `agriha/{house_id}/relay/{ch}/set` for relay ON/OFF (duration_sec supported) |
| **Weather Data** | Subscribes `agriha/farm/weather/#` → solar radiation fallback etc. |
| **Last Will** | On broker disconnect: `agriha/{house_id}/status` → `{"online":false}` |
| Duration Timer | Auto-OFF N seconds after MQTT command (per-channel, configure at /mqtt) |
| I2C Sensor | SHT40 auto-detect → MQTT publish as sensor/SHT40 |
| **CO2 Sensor** | SCD41 (I2C 0x62) → MQTT publish as sensor/SCD41 |
| Solar Sensor | PVSS-03 + M5Stack ADC Unit (ADS1110 I2C) → MQTT publish as sensor/ADS1110 |
| Solar Irrigation | Cumulative solar (MJ/m²) threshold → relay auto ON/OFF (2 rules) |
| **Drain Detection** | SEN0575 continuous drain N seconds → force irrigation OFF |
| 1-Wire Temp | DS18B20 (GPIO3) auto-detect → MQTT publish as sensor/DS18B20 |
| **Rain/Drain Sensor** | DFRobot SEN0575 (TTL UART, GPIO44/45 SerialPIO) → MQTT publish as sensor/SEN0575 |
| **Greenhouse Temp Control** | Temperature-proportional duty control (4 rules, SHT40/DS18B20 selectable, 4 curve modes) |
| **Side Window Control** | Time-proportional control with open/close seconds (4 slots, separate relays) |
| **Dew Prevention** | Fan + heater + side window auto-ON around sunrise/sunset (lat/lon sunrise calc, low-temp guard) |
| **Rapid Temp Guard** | Fan ON when temperature rise rate exceeds threshold (with hold time) |
| **CO2 Guard** | Multiple relays triggered simultaneously on CO2 drop (individual duration per relay) |
| **USB-NCM** | USB-C gives PC direct network access (192.168.7.1, built-in DHCP). No LAN cable needed |
| **Modbus RTU Master** | RS485 (GPIO4/5) controls external Waveshare Modbus RTU Relay boards (up to 4×8ch) |
| WebUI | Dashboard + MQTT + Greenhouse + Irrigation + Protection + Modbus + Config + OTA |
| **EN/JP Toggle** | All WebUI pages bilingual. Switch via `/api/language?lang=jp`, persisted in LittleFS |
| OTA Update | FW upload from browser at `/ota` (supports 10+ units). Works via USB-NCM too |
| RGB LED | WS2812 (GPIO2): green=normal / yellow=relay active / red=Ethernet down |
| USB-UART Debug | `status` / `help` / `reboot` commands |
| mDNS | `{hostname}.local` (default: `ogms-01.local`) |
| RTC | PCF85063 (I2C1) NTP sync backup |
| 3-Stage Watchdog | HW WDT 8s / SW WDT / periodic reboot 10min |
| Persistence | LittleFS (mqtt_config.json / relay_ch.json / greenhouse.json etc.) |

<details>
<summary>🇯🇵 日本語</summary>

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
| **USB-NCM** | USB-C接続でPCからWebUI/OTAアクセス (192.168.7.1、DHCPサーバー内蔵)。LANケーブル不要 |
| **Modbus RTUマスター** | RS485 (GPIO4/5) で外部Waveshare Modbus RTUリレーボードを制御 (最大4台×8ch) |
| WebUI | ダッシュボード + MQTT + Greenhouse + Irrigation + Protection + Modbus + Config + OTA |
| **EN/JP言語切替** | WebUI全ページ対応。`/api/language?lang=jp` で切替、設定はLittleFSに永続化 |
| OTA更新 | WebUI `/ota` からブラウザ経由でFW書き込み（10台超の運用に対応）。USB-NCM経由も可 |
| RGB LED | WS2812 (GPIO2) 状態表示: 緑=正常 / 黄=リレー稼働 / 赤=Ethernet断 |
| USB-UARTデバッグ | `status` / `help` / `reboot` コマンド対応 |
| mDNS | `{hostname}.local` (デフォルト: `ogms-01.local`) |
| RTC | PCF85063 (I2C1) NTP同期バックアップ |
| Watchdog 3段 | HW WDT 8s / SW WDT / 定期リブート 10分 |
| 設定永続化 | LittleFS (mqtt_config.json / relay_ch.json / greenhouse.json 等) |

</details>

---

## Quick Start

1. **First time only**: connect via USB-C, flash firmware in BOOTSEL mode
2. Connect LAN cable → obtain IP via DHCP
3. Open browser at `http://ogms-01.local/` → Dashboard
4. Go to `/mqtt` page, set broker IP and house ID
5. Auto-connects to MQTT broker → starts publishing sensor values
6. Subsequent FW updates: upload `.bin` from `/ota` page

> **Operation Manual**: See [docs/operation-manual.pdf](docs/operation-manual.pdf) for detailed configuration.

<details>
<summary>🇯🇵 日本語</summary>

1. **初回のみ** USB-CでPCに接続、BOOTSELモードでFW書き込み
2. LANケーブル接続 → DHCP で IP 取得
3. ブラウザで `http://ogms-01.local/` → ダッシュボード表示
4. `/mqtt` ページでブローカーIPとハウスIDを設定
5. MQTTブローカーに自動接続 → センサー値のpublish開始
6. 2回目以降のFW更新は `/ota` ページからブラウザでアップロード

> **操作マニュアル**: 各設定項目の詳細は [docs/operation-manual.pdf](docs/operation-manual.pdf) を参照してください。

</details>

---

## MQTT Connection

### Broker Configuration

Configure at WebUI `/mqtt`.

| Parameter | Default | Description |
|-----------|---------|-------------|
| Broker IP | `192.168.100.1` | MQTT broker IP address |
| Port | `1883` | MQTT port |
| House ID | `h01` | Topic prefix (`agriha/{house_id}/...`) |
| Client ID | (auto-generated) | If blank: `ogms-{last 4 MAC digits}` |
| Publish Interval | `10` sec | Sensor publish interval |

<details>
<summary>🇯🇵 日本語</summary>

WebUI `/mqtt` で設定する。

| 設定項目 | デフォルト | 説明 |
|---------|-----------|------|
| Broker IP | `192.168.100.1` | MQTTブローカーのIPアドレス |
| Port | `1883` | MQTTポート |
| House ID | `h01` | トピックプレフィックス (`agriha/{house_id}/...`) に使用 |
| Client ID | (自動生成) | 空なら `ogms-{MAC末尾4桁}` で自動生成 |
| Publish Interval | `10` 秒 | センサー値送信間隔 |

</details>

### MQTT Topics

#### Publish (QoS1, Retained)

| Topic | Payload Example | Notes |
|-------|----------------|-------|
| `agriha/{house_id}/sensor/SHT40` | `{"temperature_c":23.5,"humidity_pct":65.2,"timestamp":1740000000}` | Indoor temp/humidity |
| `agriha/{house_id}/sensor/SCD41` | `{"temperature_c":23.1,"humidity_pct":64.8,"co2_ppm":450,"timestamp":...}` | CO2/temp/humidity |
| `agriha/{house_id}/sensor/DS18B20` | `{"temperature_c":15.3,"timestamp":...}` | 1-Wire temperature |
| `agriha/{house_id}/sensor/ADS1110` | `{"solar_wm2":523.0,"timestamp":...}` | Solar irradiance (PVSS-03) |
| `agriha/{house_id}/sensor/SEN0575` | `{"drain_mm":12.4,"timestamp":...}` | Rain/drain volume |
| `agriha/{house_id}/relay/state` | `{"ch1":0,"ch2":1,...,"ch8":0,"ts":...}` | Relay state |
| `agriha/{house_id}/status` | `{"online":true}` | Connection state (retained) / Last Will: `{"online":false}` |

`"timestamp":0` when NTP not yet synced.

<details>
<summary>🇯🇵 日本語</summary>

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

</details>

#### Subscribe

| Topic | Payload Example | Action |
|-------|----------------|--------|
| `agriha/{house_id}/relay/{ch}/set` | `{"value":1,"duration_sec":180,"reason":"LLM"}` | Relay ON/OFF. `duration_sec` > 0 → auto-OFF after N seconds |
| `agriha/farm/weather/#` | `{"solar_wm2":600.0}` etc. | Receive farm weather data |

<details>
<summary>🇯🇵 日本語</summary>

| トピック | ペイロード例 | 動作 |
|---------|------------|------|
| `agriha/{house_id}/relay/{ch}/set` | `{"value":1,"duration_sec":180,"reason":"LLM"}` | リレーON/OFF。`duration_sec` > 0でN秒後自動OFF |
| `agriha/farm/weather/#` | `{"solar_wm2":600.0}` 等 | 圃場共有気象データ受信 |

</details>

### Weather Data Consumer Policy

OGMS is a **consumer** of weather data. It subscribes to `agriha/farm/weather/#` for external data.

| Sub-topic | Data | OGMS Usage |
|-----------|------|-----------|
| `agriha/farm/weather/solar` | Solar irradiance (W/m²) | Fallback for solar-proportional irrigation |
| `agriha/farm/weather/temperature` | Outdoor temperature | Dew prevention, rapid temp guard |
| `agriha/farm/weather/wind_speed` | Wind speed | Side window control |

- Local ADS1110 takes priority when available; MQTT value used as fallback when not present
- Publisher can be RPi weather service or another OGMS node — OGMS does not care

<details>
<summary>🇯🇵 日本語</summary>

OGMSは外気データの **コンシューマ**。`agriha/farm/weather/#` をsubscribeして取得する。

| サブトピック | データ | OGMS側の用途 |
|------------|--------|------------------|
| `agriha/farm/weather/solar` | 日射量 (W/m²) | 日射比例灌水のフォールバック |
| `agriha/farm/weather/temperature` | 外気温 | 結露防止・急昇温ガード |
| `agriha/farm/weather/wind_speed` | 風速 | 側窓制御判断 |

- ローカルADS1110がある場合はローカル値優先、ない場合はMQTT受信値をフォールバックとして使用
- publisherはRPi側気象サービスでも別OGMSノードでも可。OGMSは関知しない

</details>

---

## Board Configuration

| Item | Value |
|------|-------|
| Board | **Generic RP2350** |
| Variant Chip | **RP2530B** (RP2350B — required for GPIO33-36) |
| Flash Size | **16MB (Sketch: 14MB, FS: 2MB)** |
| Upload Method | USB (UF2) first time / OTA (subsequent) |

<details>
<summary>🇯🇵 日本語</summary>

| 項目 | 値 |
|------|-----|
| Board | **Generic RP2350** |
| Variant Chip | **RP2530B** (RP2350B — GPIO33-36使用のため必須) |
| Flash Size | **16MB (Sketch: 14MB, FS: 2MB)** |
| Upload Method | USB (UF2) 初回 / OTA (以降) |

</details>

## Required Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| arduino-pico | 4.5.2+ | Earle Philhower core |
| ArduinoJson | v7.x | JSON config & API |
| NTPClient | 3.2.1 | Time sync |
| **PubSubClient** | **2.8+** | **MQTT client** |
| SensirionI2cSht4x | 1.1+ | SHT40 temp/humidity (optional) |
| SensirionI2cScd4x | 1.1+ | SCD41 CO2 (optional) |
| Adafruit NeoPixel | 1.15+ | WS2812 RGB LED |
| OneWire | 2.3.8 | DS18B20 (optional) |
| DallasTemperature | 4.0+ | DS18B20 (optional) |
| **ModbusMaster (4-20mA)** | **2.0+** | **Modbus RTU Master** |
| Adafruit TinyUSB | 3.7+ | USB-NCM + CDC-ACM composite |
| W5500lwIP, LEAmDNS, Wire, LittleFS, Updater | arduino-pico built-in | |

<details>
<summary>🇯🇵 日本語</summary>

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
| **ModbusMaster (4-20mA)** | **2.0+** | **Modbus RTU マスター** |
| Adafruit TinyUSB | 3.7+ | USB-NCM + CDC-ACM composite |
| W5500lwIP, LEAmDNS, Wire, LittleFS, Updater | arduino-pico内蔵 | |

</details>

---

## Build

### Arduino CLI

```bash
# Build
arduino-cli compile \
  --fqbn "rp2040:rp2040:generic_rp2350:variantchip=RP2530B,flash=16777216_2097152,psramcs=GPIOnone,psram=0mb,freq=150,arch=arm,opt=Small,os=none,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Serial,dbglvl=None,usbstack=picosdk,ipbtstack=ipv4only,uploadmethod=default" \
  .

# OTA update (subsequent)
curl --data-binary @/tmp/ccm_ota/ogms.ino.bin \
  -H "Content-Type: application/octet-stream" \
  http://ogms-01.local/api/ota
```

### PlatformIO (VSCode)

`platformio.ini` included. Open in VSCode and run `pio run`.

```ini
[env:rp2350b]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = generic_rp2350
framework = arduino
board_build.core = earlephilhower
board_build.chip = rp2350b
board_build.filesystem_size = 2m
build_flags =
  -DUSE_TINYUSB=1
  -DCFG_TUD_NCM=1
  -Wl,--wrap=TinyUSB_Device_Init
```

<details>
<summary>🇯🇵 日本語</summary>

### Arduino CLI

```bash
# ビルド
arduino-cli compile \
  --fqbn "rp2040:rp2040:generic_rp2350:variantchip=RP2530B,flash=16777216_2097152,psramcs=GPIOnone,psram=0mb,freq=150,arch=arm,opt=Small,os=none,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Serial,dbglvl=None,usbstack=picosdk,ipbtstack=ipv4only,uploadmethod=default" \
  .

# OTA更新 (2回目以降)
curl --data-binary @/tmp/ccm_ota/ogms.ino.bin \
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
build_flags =
  -DUSE_TINYUSB=1
  -DCFG_TUD_NCM=1
  -Wl,--wrap=TinyUSB_Device_Init
```

</details>

---

## WebUI

| Path | Method | Description |
|------|--------|-------------|
| `/` | GET | Dashboard (relay + DI + sensors + MQTT status) |
| `/config` | GET | Network configuration (IP/mDNS) |
| `/mqtt` | GET | MQTT settings (broker/house ID/DI link/WDT timer) |
| `/greenhouse` | GET | Temperature control settings (4 rules) + side window control (4 slots) |
| `/irrigation` | GET | Solar-proportional irrigation settings (2 rules, drain stop sec) |
| `/protection` | GET | Dew prevention + rapid temp guard + CO2 guard settings |
| `/modbus` | GET | Modbus RTU Master settings (slaves, baud, poll) + external relay control |
| `/ota` | GET | FW update page (select .bin in browser → upload → auto-reboot) |
| `/api/state` | GET | State JSON (relay/DI/sensors/MQTT/claims/Greenhouse/Aperture/Irrigation/Protection/CO2) |
| `/api/config` | GET | Network config JSON |
| `/api/language` | GET | Language toggle (`?lang=en` or `?lang=jp`). 302 redirect to `/` |
| `/api/config` | POST | Save network config → reboot |
| `/api/mqtt` | POST | Save MQTT + DI link/WDT settings (no reboot) |
| `/api/greenhouse` | POST | Save greenhouse control settings |
| `/api/aperture` | POST | Save side window settings |
| `/api/irrigation` | POST | Save irrigation control settings |
| `/api/protection` | POST | Save dew/rapid-temp/CO2 guard settings |
| `/api/modbus` | POST | Save Modbus master config |
| `/api/modbus/relay` | POST | Toggle external Modbus relay (slave/ch/on) |
| `/api/relay/{ch}` | POST | Manual relay control |
| `/api/ota` | POST | Receive FW binary → flash → reboot |

<details>
<summary>🇯🇵 日本語</summary>

| パス | メソッド | 説明 |
|------|---------|------|
| `/` | GET | ダッシュボード (リレー+DI+センサー+MQTT接続状態) |
| `/config` | GET | ネットワーク設定 (IP/mDNS) |
| `/mqtt` | GET | MQTT設定 (ブローカー/ハウスID/DI連動/WDTタイマー) |
| `/greenhouse` | GET | 温度比例制御設定 (4ルール) + 側窓開度制御 (4スロット) |
| `/irrigation` | GET | 日射比例灌水設定 (2ルール、排水停止秒数含む) |
| `/protection` | GET | 結露防止 + 急昇温ガード + CO2ガード設定 |
| `/modbus` | GET | Modbus RTUマスター設定 (スレーブ/ボーレート/ポーリング) + 外部リレー制御 |
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
| `/api/modbus` | POST | Modbusマスター設定保存 |
| `/api/modbus/relay` | POST | 外部Modbusリレー操作 (slave/ch/on) |
| `/api/relay/{ch}` | POST | リレー手動制御 |
| `/api/ota` | POST | FWバイナリ受信 → フラッシュ → リブート |

</details>

---

## Sensor Connections

| Sensor | Interface | GPIO | MQTT Topic | Notes |
|--------|-----------|------|-----------|-------|
| SHT40 | I2C1 (Grove) | 6/7 | sensor/SHT40 | Auto-detected |
| SCD41 | I2C1 (Grove) | 6/7 | sensor/SCD41 | CO2/temp/humidity. Auto-detected. 5s interval |
| PVSS-03 + ADS1110 | I2C1 (Grove) | 6/7 | sensor/ADS1110 | Via M5Stack ADC Unit V1.1. 0–1V = 0–1000 W/m² |
| DS18B20 | 1-Wire | 3 | sensor/DS18B20 | From Grove board |
| SEN0575 | TTL UART (SerialPIO) | 44/45 | sensor/SEN0575 | Rain/drain sensor. Modbus RTU 9600bps |

All sensors are **auto-detected**. Normal operation continues if any sensor is absent.

> **SEN0575 note**: Connect to GPIO44/45 (expansion header), **not** RS485 terminals (GPIO4/5). Set the board's I2C/UART switch to UART side.

<details>
<summary>🇯🇵 日本語</summary>

| センサー | インターフェース | GPIO | MQTTトピック | 備考 |
|---------|----------------|------|------------|------|
| SHT40 | I2C1 (Grove) | 6/7 | sensor/SHT40 | 自動検出 |
| SCD41 | I2C1 (Grove) | 6/7 | sensor/SCD41 | CO2/温度/湿度。自動検出。5秒間隔 |
| PVSS-03 + ADS1110 | I2C1 (Grove) | 6/7 | sensor/ADS1110 | M5Stack ADC Unit V1.1経由。0-1V=0-1000W/m² |
| DS18B20 | 1-Wire | 3 | sensor/DS18B20 | Grove基板から引出し |
| SEN0575 | TTL UART (SerialPIO) | 44/45 | sensor/SEN0575 | 排水/雨量センサー。Modbus RTU 9600bps |

センサーは全て **自動検出**。未接続でも正常動作する。

> **SEN0575接続注意**: RS485端子(GPIO4/5)ではなく、拡張ヘッダのGPIO44/45にTTL UART接続。ボード上のI2C/UARTスイッチをUART側にすること。

</details>

---

## USB-NCM (USB Network)

Direct WebUI/OTA/API access from PC via a single USB-C cable. No LAN cable needed for maintenance.

| Item | Value |
|------|-------|
| IP Address | `192.168.7.1` (fixed) |
| PC-side IP | DHCP auto-assigns `192.168.7.2` |
| Protocol | CDC-NCM (USB composite: NCM + CDC-ACM) |
| Windows | No driver needed (Windows 10 1703+) |
| Linux/macOS | Natively supported |
| Serial Port | CDC-ACM available simultaneously (COM/ttyACM) |

> **OTA via USB-NCM**: `curl --data-binary @firmware.bin http://192.168.7.1/api/ota` — update firmware without LAN.

<details>
<summary>🇯🇵 日本語</summary>

USB-Cケーブル1本でPCからWebUI・OTA・APIにアクセス可能。LANケーブル不要のメンテナンス経路。

| 項目 | 値 |
|------|-----|
| IPアドレス | `192.168.7.1` (固定) |
| PC側IP | DHCPで `192.168.7.2` 自動取得 |
| プロトコル | CDC-NCM (USB composite: NCM + CDC-ACM) |
| Windows | Windows 10 1703以降ドライバ不要 |
| Linux/macOS | 標準対応 |
| シリアルポート | CDC-ACM同時利用可 (COM/ttyACM) |

</details>

---

## Modbus RTU Master

Controls external Waveshare Modbus RTU Relay boards via RS485 (Serial2, GPIO4/5). Up to 4 slave devices × 8 channels = 32 external relays.

Configure at WebUI `/modbus`. Manual relay toggle buttons provided.

| Feature | Detail |
|---------|--------|
| FC05 | Write Single Coil — relay ON/OFF |
| FC01 | Read Coils — relay state readback |
| Polling | Configurable interval (default 10s) |
| Slaves | Up to 4, each with address/label/channel count |
| Persistence | `/modbus_master.json` |

> **SEN0575 coexistence**: SEN0575 uses SerialPIO (GPIO44/45), completely separate from RS485 (GPIO4/5).

<details>
<summary>🇯🇵 日本語</summary>

RS485ポート (Serial2, GPIO4/5) で外部Waveshare Modbus RTUリレーボードを制御。最大4台×8ch = 32外部リレー。

WebUI `/modbus` で設定。手動リレートグルボタンあり。

| 機能 | 詳細 |
|------|------|
| FC05 | Write Single Coil — リレーON/OFF |
| FC01 | Read Coils — リレー状態読み取り |
| ポーリング | 設定間隔 (デフォルト10秒) |
| スレーブ | 最大4台。アドレス/ラベル/CH数を個別設定 |
| 永続化 | `/modbus_master.json` |

> **SEN0575との共存**: SEN0575はSerialPIO (GPIO44/45) 使用。RS485 (GPIO4/5) と完全に独立。

</details>

---

## Relay Arbitration Layer

When multiple owners control the same relay channel, the arbitration layer resolves conflicts.

| Owner | Description |
|-------|-------------|
| OWN_GH (0) | Greenhouse temperature control |
| OWN_IRRI (1) | Irrigation |
| OWN_DEW (2) | Dew prevention |
| OWN_RATE (3) | Rapid temperature guard |
| OWN_CO2 (4) | CO2 guard |
| OWN_MQTT (5) | MQTT command |
| OWN_MANUAL (6) | WebUI manual / DI link |
| OWN_APT (7) | Side window aperture control |

| Composition Rule | Behavior | Safety Rationale |
|-----------------|----------|-----------------|
| **OR composition** | Any single owner claims → ON | Prevents ventilation shortage |
| **AND composition** | All involved owners claim → ON | Prevents unnecessary heating |

Check per-channel bitmask via `/api/state` `relay_claims[]`.

<details>
<summary>🇯🇵 日本語</summary>

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

</details>

---

## Safety Features

### DI→Relay Link

Bind a DI input to each relay channel in the "DI Link" table at WebUI `/mqtt`. Works locally even when network is disconnected.

| Setting | Example Use |
|---------|------------|
| DI1 → CH1 (normal) | Manual switch → ventilation fan ON |
| DI2 → CH3 (invert) | Float switch ON → irrigation pump OFF (water level limit) |

<details>
<summary>🇯🇵 日本語</summary>

WebUI `/mqtt` ページの「DI Link」テーブルで各リレーchにDI入力を紐付け。ネットワーク断でもローカルで即座に動作。

| 設定 | 用途例 |
|------|--------|
| DI1 → CH1 (normal) | 手元スイッチで換気扇ON |
| DI2 → CH3 (invert) | フロートスイッチON → 灌水ポンプOFF（水位上限） |

</details>

### MQTT duration / WDT

- **duration_sec**: Include `duration_sec` in `relay/{ch}/set` payload → auto-OFF after N seconds
- **WDT (watchdog_sec)**: Configure at WebUI `/mqtt`. Relay auto-OFF if no MQTT received for N seconds (0 = disabled)
- **Disconnection**: On broker disconnect, all OWN_MQTT claims are immediately released

<details>
<summary>🇯🇵 日本語</summary>

- **duration_sec**: `relay/{ch}/set` ペイロードに `duration_sec` を含めるとN秒後に自動OFF
- **WDT (watchdog_sec)**: WebUI `/mqtt` で設定。最終MQTT受信後N秒でリレーを自動OFF (0=無効)
- **接続断**: ブローカー切断を検知すると OWN_MQTT の全claimを即座に解放

</details>

---

## File Structure

```
.
├── ogms.ino                # Main FW (control logic, MQTT, JSON persistence)
├── usb_ncm.h               # USB-NCM netif + DHCP server
├── usb_ncm_init.cpp         # TinyUSB IAD init override
├── modbus_master.h          # Modbus RTU Master (Serial2 GPIO4/5)
├── web_common.h            # WebUI common (CSS, nav, JS)
├── web_i18n.h              # EN/JP language toggle (L() macro)
├── web_dashboard.h         # Dashboard (GET /)
├── web_config.h            # Network config (GET /config, POST /api/config)
├── web_mqtt.h              # MQTT config + DI link + WDT (GET /mqtt, POST /api/mqtt)
├── web_greenhouse.h        # Greenhouse control + side window (GET /greenhouse, POST /api/greenhouse, /api/aperture)
├── web_irrigation.h        # Irrigation control (GET /irrigation, POST /api/irrigation)
├── web_protection.h        # Protection control (GET /protection, POST /api/protection)
├── web_modbus.h             # Modbus Master config + relay control (GET /modbus, POST /api/modbus)
├── web_ota.h               # OTA update (GET /ota, POST /api/ota)
├── web_api.h               # REST API (GET /api/state, /api/config, /api/language)
├── sw_watchdog.h            # SW WDT
├── sensor_registry.h       # I2C sensor definitions (SCD41/SHT40/BMP280/BH1750/ADS1110)
├── platformio.ini           # PlatformIO config
├── docs/
│   ├── operation-manual.md   # Operation manual (Markdown source)
│   ├── operation-manual.pdf  # Operation manual (PDF)
│   └── pdf-header.tex        # LaTeX header for PDF generation
└── README.md
```

<details>
<summary>🇯🇵 日本語</summary>

```
.
├── ogms.ino                # メインFW (制御ロジック・MQTT・JSON永続化)
├── usb_ncm.h               # USB-NCM netif + DHCPサーバー
├── usb_ncm_init.cpp         # TinyUSB IAD init override
├── modbus_master.h          # Modbus RTU マスター (Serial2 GPIO4/5)
├── web_common.h            # WebUI共通 (CSS・ナビ・JS)
├── web_i18n.h              # EN/JP言語切替 (L()マクロ)
├── web_dashboard.h         # ダッシュボード (GET /)
├── web_config.h            # ネットワーク設定 (GET /config, POST /api/config)
├── web_mqtt.h              # MQTT設定+DI連動+WDT (GET /mqtt, POST /api/mqtt)
├── web_greenhouse.h        # 温室制御+側窓開度 (GET /greenhouse, POST /api/greenhouse, /api/aperture)
├── web_irrigation.h        # 灌水制御 (GET /irrigation, POST /api/irrigation)
├── web_protection.h        # 保護制御 (GET /protection, POST /api/protection)
├── web_modbus.h             # Modbusマスター設定+リレー制御 (GET /modbus, POST /api/modbus)
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

</details>

---

## Hardware Verification

- [x] W5500 SPI0 (GPIO33-36) operation confirmed
- [x] Ethernet DHCP acquisition + communication confirmed
- [x] mDNS `ogms-01.local` access confirmed
- [x] MQTT connection + sensor/SHT40 publish confirmed
- [x] WebUI all pages operation confirmed
- [x] OTA update confirmed (313KB / ~7 seconds)
- [x] USB-UART debug commands confirmed
- [x] SHT40 I2C detection + MQTT publish
- [x] SCD41 I2C detection + CO2 reading + MQTT publish
- [x] SEN0575 SerialPIO (GPIO44/45) detection + Modbus RTU communication
- [x] DS18B20 1-Wire detection + MQTT publish
- [x] Relay arbitration layer (relay_claims API confirmed)
- [x] EN/JP language toggle WebUI confirmed
- [x] USB-NCM ping + WebUI + OTA confirmed (192.168.7.1)
- [x] USB-NCM DHCP auto-assign (PC gets 192.168.7.2)
- [x] Modbus Master WebUI page confirmed
- [ ] Modbus RTU Master external relay control (pending hardware)
- [ ] relay/{ch}/set MQTT receive → relay control
- [ ] duration_sec auto-OFF behavior
- [ ] farm/weather/solar subscribe fallback
- [ ] Last Will delivery confirmed
- [ ] All 8ch relay ON/OFF
- [ ] DI 8ch interrupt detection
- [ ] DI→relay link
- [ ] ArSprout (uecs-llm) send/receive confirmed

<details>
<summary>🇯🇵 日本語</summary>

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
- [x] USB-NCM ping + WebUI + OTA動作確認 (192.168.7.1)
- [x] USB-NCM DHCP割当 (PC側 192.168.7.2 自動取得)
- [x] Modbusマスター WebUIページ動作確認
- [ ] Modbus RTUマスター 外部リレー制御 (ハードウェア待ち)
- [ ] relay/{ch}/set MQTT受信 → リレー制御
- [ ] duration_sec auto-OFF動作確認
- [ ] farm/weather/solar subscribeフォールバック動作
- [ ] Last Will配信確認
- [ ] 全8chリレー ON/OFF
- [ ] DI 8ch 割り込み検知
- [ ] DI→リレー連動
- [ ] ArSprout(uecs-llm)側で送受信確認

</details>

### RP2350B Pin Map Notes

| Function | GPIO | Bus | Notes |
|----------|------|-----|-------|
| I2C SDA/SCL | 6/7 | **I2C1** (not I2C0) | Use `Wire1` |
| W5500 SPI | 33-36 | **SPI0** (not SPI1) | Use `SPI` |
| RS485 TX/RX | 4/5 | UART1 | Differential transceiver. RS485 devices only |
| SEN0575 TX/RX | 44/45 | **SerialPIO** | Expansion header. TTL UART direct |
| DS18B20 | 3 | 1-Wire | From Grove board |
| WS2812 | 2 | NeoPixel | On-board |

`variantchip=RP2530B` (for RP2350B) is required — GPIO33-36 are unusable without it; build succeeds but causes PANIC at runtime.

<details>
<summary>🇯🇵 日本語</summary>

| 機能 | GPIO | バス | 注意 |
|------|------|------|------|
| I2C SDA/SCL | 6/7 | **I2C1** (I2C0ではない) | `Wire1` を使用 |
| W5500 SPI | 33-36 | **SPI0** (SPI1ではない) | `SPI` を使用 |
| RS485 TX/RX | 4/5 | UART1 | 差動トランシーバ経由。RS485機器専用 |
| SEN0575 TX/RX | 44/45 | **SerialPIO** | 拡張ヘッダ。TTL UART直結 |
| DS18B20 | 3 | 1-Wire | Grove基板から引出し |
| WS2812 | 2 | NeoPixel | ボード上実装済み |

`variantchip=RP2530B` (RP2350B用) でないとGPIO33-36が使えず、ビルドは通るが実行時PANICする。

</details>
