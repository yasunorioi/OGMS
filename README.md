# agri-relay — スタンドアロン温室リレーコントローラ (RP2350B)

Arduino firmware for **Waveshare RP2350-ETH-8DI-8RO / RP2350-POE-ETH-8DI-8RO**.

8chリレー + 側窓開度制御 + 日射比例灌水 + 排水検知停止 + 結露防止(側窓連動) + 急昇温ガード + CO2ガード + リレー調停レイヤー。ArSprout (UECS-CCM) 連携対応、**スタンドアロンでも動作**。EN/JP 言語切替対応。

### ボード購入先

| ボード | 購入先 |
|--------|--------|
| RP2350-ETH-8DI-8RO | [Waveshare公式](https://www.waveshare.com/rp2350-eth-8di-8ro.htm) / [スイッチサイエンス (Waveshare製品)](https://www.switch-science.com/) / [Amazon](https://www.amazon.com/dp/B0FVF19VRR) |
| RP2350-POE-ETH-8DI-8RO (PoE版) | [Waveshare公式](https://www.waveshare.com/rp2350-poe-eth-8di-8ro.htm) / [Amazon](https://www.amazon.com/dp/B0FVF19VRR) |
| M5Stack ADC Unit V1.1 (日射センサー用) | [M5Stack公式](https://shop.m5stack.com/products/adc-i2c-unit-v1-1-ads1100) / [スイッチサイエンス](https://www.switch-science.com/) |

## 機能一覧

| 機能 | 実装 |
|------|------|
| 8ch リレー制御 | GPIO17-24 直接制御 |
| 8ch デジタル入力 | GPIO9-16, フォトカプラ絶縁, アクティブLOW, 割り込み検知 |
| DI→リレー連動 | ch毎にDI紐付け、反転モード対応（フロートスイッチ→灌水停止等） |
| **リレー調停レイヤー** | 8制御者のclaim/release → OR合成(換気系) / AND合成(暖房系) |
| 無通信ウォッチドッグ | CCM受信途絶でリレー強制OFF (ch別、デフォルト60秒) |
| 通信 | **UECS-CCM** (UDP multicast 224.0.0.1:16520) |
| CCMマッピング | WebUI `/ccm` で ch⇔CCMタイプ/Room/Region/Order を設定 + Bulk Set |
| CCM送信 | リレー状態 + センサー値をCCM XMLでbroadcast (10秒間隔) |
| CCM受信 | type+room+region+orderマッチでリレー制御 (priority考慮) |
| I2Cセンサー | SHT40 自動検出 → InAirTemp/InAirHumid としてCCM送信 |
| **CO2センサー** | SCD41 (I2C 0x62) → InAirCO2 としてCCM送信 |
| 日射センサー | PVSS-03 + M5Stack ADC Unit (ADS1110 I2C) → InRadiation としてCCM送信 |
| 日射比例灌水 | 積算日射量(MJ/m²)閾値で灌水リレー自動ON/OFF (2ルール対応) |
| **排水検知灌水停止** | SEN0575で連続排水N秒検知 → 灌水強制OFF |
| 1-Wire温度 | DS18B20 (GPIO3) 自動検出 → InAirTemp region=12 としてCCM送信 |
| **雨量/排水センサー** | DFRobot SEN0575 (TTL UART, GPIO44/45 SerialPIO) → WRainfallAmt としてCCM送信 |
| **温室温度制御** | 温度比例デューティ制御 (4ルール、SHT40/DS18B20選択、4カーブモード) |
| **側窓開度制御** | 開/閉秒数指定の時間比例制御 (4スロット、開閉別リレー) |
| **結露防止** | 日の出前後の時間帯にファン+暖房+側窓連動を自動ON (緯度経度から日の出計算、低温ガード付き) |
| **急昇温ガード** | 温度上昇率が閾値超過でファンON (保持時間付き) |
| **CO2ガード** | CO2低下時に複数リレーを個別時間で同時発動 (側窓+ファン+暖房等) |
| WebUI | ダッシュボード + CCM + Greenhouse + Irrigation + Protection + Config + OTA |
| **EN/JP言語切替** | WebUI全ページ対応。`/api/language?lang=jp` で切替、設定はLittleFSに永続化 |
| OTA更新 | WebUI `/ota` からブラウザ経由でFW書き込み（10台超の運用に対応） |
| RGB LED | WS2812 (GPIO2) 状態表示: 緑=正常 / 黄=リレー稼働 / 赤=Ethernet断 |
| USB-UARTデバッグ | `status` / `help` / `reboot` コマンド対応 |
| mDNS | `{hostname}.local` (デフォルト: `uecs-ccm-01.local`) |
| RTC | PCF85063 (I2C1) NTP同期バックアップ |
| Watchdog 3段 | HW WDT 8s / SW WDT / 定期リブート 10分 |
| 設定永続化 | LittleFS (8設定ファイル) |

---

## クイックスタート

1. **初回のみ** USB-CでPCに接続、BOOTSELモードでFW書き込み
2. LANケーブル接続 → DHCP で IP 取得
3. ブラウザで `http://uecs-ccm-01.local/` → ダッシュボード表示
4. `/ccm` ページで各chにCCMタイプ/Room/Region を設定
5. ArSproutのCCMネットワークに自動参加 — **ArSprout側の設定変更は不要**
6. 2回目以降のFW更新は `/ota` ページからブラウザでアップロード

> **操作マニュアル**: 各設定項目の詳細は [docs/operation-manual.pdf](docs/operation-manual.pdf) を参照してください。

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

# .binエクスポート (OTA用)
arduino-cli compile --output-dir /tmp/ccm_ota \
  --fqbn "rp2040:rp2040:generic_rp2350:variantchip=RP2530B,flash=16777216_2097152,psramcs=GPIOnone,psram=0mb,freq=150,arch=arm,opt=Small,os=none,profile=Disabled,rtti=Disabled,stackprotect=Disabled,exceptions=Disabled,dbgport=Serial,dbglvl=None,usbstack=picosdk,ipbtstack=ipv4only,uploadmethod=default" \
  .

# 初回USB書き込み
arduino-cli upload -p /dev/ttyACM0 \
  --fqbn "rp2040:rp2040:generic_rp2350:variantchip=RP2530B,flash=16777216_2097152,..." \
  .

# OTA更新 (2回目以降)
curl --data-binary @/tmp/ccm_ota/ccm_rp2350_relay.ino.bin \
  -H "Content-Type: application/octet-stream" \
  http://uecs-ccm-01.local/api/ota
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
| `/` | GET | ダッシュボード (リレー+DI+センサー+Greenhouse+Irrigation+CO2) |
| `/config` | GET | ネットワーク設定 (IP/mDNS) |
| `/ccm` | GET | CCMマッピング設定 (Bulk Set / DI連動 / WDTタイマー) |
| `/greenhouse` | GET | 温度比例制御設定 (4ルール) + 側窓開度制御 (4スロット) |
| `/irrigation` | GET | 日射比例灌水設定 (2ルール、排水停止秒数含む) |
| `/protection` | GET | 結露防止 + 急昇温ガード + CO2ガード設定 |
| `/ota` | GET | FW更新ページ (ブラウザからbin選択→アップロード→自動リブート) |
| `/api/state` | GET | 状態JSON (リレー/DI/センサー/CCM/claims/Greenhouse/Aperture/Irrigation/Protection/CO2) |
| `/api/config` | GET | ネットワーク設定JSON |
| `/api/language` | GET | 言語切替 (`?lang=en` or `?lang=jp`)。302リダイレクトで `/` に戻る |
| `/api/config` | POST | ネットワーク設定保存 → リブート |
| `/api/ccm` | POST | CCMマッピング保存 (リブート不要) |
| `/api/greenhouse` | POST | Greenhouse制御設定保存 |
| `/api/aperture` | POST | 側窓開度設定保存 |
| `/api/irrigation` | POST | Irrigation制御設定保存 |
| `/api/protection` | POST | 結露防止/急昇温ガード/CO2ガード設定保存 |
| `/api/relay/{ch}` | POST | リレー手動制御 |
| `/api/ota` | POST | FWバイナリ受信 → フラッシュ → リブート |

---

## センサー接続

| センサー | インターフェース | GPIO | CCM送信 | 備考 |
|---------|----------------|------|---------|------|
| SHT40 | I2C1 (Grove) | 6/7 | InAirTemp, InAirHumid | 自動検出 |
| SCD41 | I2C1 (Grove) | 6/7 | InAirCO2 | CO2/温度/湿度。自動検出。5秒間隔 |
| PVSS-03 + ADS1110 | I2C1 (Grove) | 6/7 | InRadiation | M5Stack ADC Unit V1.1経由。0-1V=0-1000W/m² |
| DS18B20 | 1-Wire | 3 | InAirTemp (region=12) | Grove基板から引出し |
| SEN0575 | TTL UART (SerialPIO) | 44/45 | WRainfallAmt | 排水/雨量センサー。Modbus RTU 9600bps |

センサーは全て **自動検出**。未接続でも正常動作する。

> **SEN0575接続注意**: RS485端子(GPIO4/5)ではなく、拡張ヘッダのGPIO44/45にTTL UART接続。ボード上のI2C/UARTスイッチをUART側にすること。

---

## 日射比例灌水

WebUI `/irrigation` ページで設定。M5Stack ADC Unit V1.1 (ADS1110) + PVSS-03日射センサーをGrove I2Cに接続。

**動作**: 日射量を積算 → 閾値(MJ/m²)到達 → 灌水リレーON → 設定秒数後OFF or 排水検知停止 → カウンタリセット → 再積算

| 設定項目 | デフォルト | 説明 |
|---------|-----------|------|
| Threshold | 0.5 MJ/m² | 灌水トリガーの積算日射量 (成長段階で調整) |
| Duration | 120秒 | 1回の灌水時間 |
| Min W/m² | 50 | この値未満は積算しない (夜間ノイズ除外) |
| Drain Stop | 0秒 (無効) | SEN0575で連続排水を検知した秒数で灌水強制OFF |

### Threshold (積算日射量) の目安

作物の成長段階で調整する。閾値が小さいほど灌水頻度が高い。

| 段階 | Threshold | 灌水頻度 (晴天時) |
|------|----------|-----------------|
| 定植直後 | 0.3 MJ/m² | 多い (少量頻回) |
| 生育中期 | 0.5 MJ/m² | 標準 |
| 収穫期 | 0.8-1.0 MJ/m² | 少ない |

### Min W/m² (ノイズフィルター)

PVSS-03は0W/m²付近でもADCノイズで数W/m²の値が出る。  
この値未満の日射読み取りは積算から除外し、夜間の誤灌水を防止する。

| 設定値 | 用途 |
|--------|------|
| 50 (デフォルト) | 通常運用。曇天でもある程度拾う |
| 20-30 | 曇天続きで灌水不足になる場合に下げる |
| 100 | 晴天のみ積算したい場合 |

### 運用ノート

- 2ルール対応 (e.g. 点滴灌水 + ミスト)
- CCM `InRadiation` としてArSproutにも日射値をbroadcast
- ADS1110未接続でも他機能は正常動作（灌水ルール有効でも安全）
- 灌水中は積算を停止する（灌水直後に再トリガーしない）
- WebUI `/irrigation` で積算量・灌水回数・残り秒数をリアルタイム監視可能

---

## リレー調停レイヤー

複数の制御者（温度制御・灌水・結露防止・急昇温ガード・CO2ガード・側窓制御・CCM受信・手動操作）が同じリレーchを操作しうるため、調停レイヤーで衝突を解決する。

各制御者は `claimRelay()` (ON要求) / `releaseRelay()` (取り下げ) でビットを立て、ループ末尾の `resolveAllRelays()` が物理出力を一括確定する。

| 合成ルール | 対象ch | 動作 | 安全側 |
|-----------|--------|------|--------|
| **OR合成** | 換気・側窓・ファン等 | 誰か1人でもclaim → ON | 換気不足を防ぐ |
| **AND合成** | 暖房 (AirHeatBurn / AirHeatHP) | 関与者全員がclaim → ON | 不要な加温を防ぐ |

**例: 20℃ / CO2≤200ppm の場合**

| リレー | 温度制御 | CO2ガード | 合成結果 |
|--------|---------|----------|---------|
| 側窓 (OR) | release (涼しい) | claim (CO2低い) | **ON** — CO2確保 |
| 暖房 (AND) | release (暖房不要) | claim (換気補償) | **OFF** — 暑いのに暖房は入らない |

### 制御者一覧

| ID | 制御者 | ビット |
|----|--------|-------|
| OWN_GH | 温室温度制御 | 0 |
| OWN_IRRI | 灌水 | 1 |
| OWN_DEW | 結露防止 | 2 |
| OWN_RATE | 急昇温ガード | 3 |
| OWN_CO2 | CO2ガード | 4 |
| OWN_CCM | CCM受信 | 5 |
| OWN_MANUAL | WebUI手動 / DI連動 | 6 |
| OWN_APT | 側窓開度制御 | 7 |

`/api/state` の `relay_claims[]` で各chのビットマスクを確認可能。

---

## 側窓開度制御

WebUI `/greenhouse` ページ下部「Side Window Aperture」で設定。4スロット対応。

開方向・閉方向それぞれの全動作時間（秒）を指定し、目標開度%に比例した時間だけリレーを動作させる。

| 設定項目 | 説明 |
|---------|------|
| Open CH | 開方向リレーch |
| Close CH | 閉方向リレーch (-1 = Open CHのOFF=閉) |
| Limit DI | リミットスイッチDIch (-1=無効) |
| Open(s) | 全閉→全開にかかる秒数 |
| Close(s) | 全開→全閉にかかる秒数 |

**動作**: 起動時に全閉初期化 → 温室制御や結露防止から目標開度%を受け取り → `(目標% - 現在%) / 100 × 全動作秒数` だけリレーを動作させる。

---

## 結露防止 (Dew Prevention)

日の出前後にファンと暖房を稼働させ、放射冷却による結露を防止する。側窓連動で換気効率を向上。

| 設定項目 | 説明 |
|---------|------|
| Fan Relay CH | ファンのリレーch |
| Heater Relay CH | 暖房のリレーch |
| Before Sunrise | 日の出の何分前に開始 |
| After Sunrise | 日の出の何分後に終了 |
| Latitude / Longitude | 日の出計算用の緯度経度 |
| Side Window Slot | 連動させる側窓スロット (-1=無効) |
| Apt Open % | 結露対策中の側窓開度 |
| Apt Close % | 結露対策終了時の側窓開度 |
| Apt Min Temp (℃) | 低温ガード: この温度以下では側窓を開けない |

日の出時刻は緯度経度から毎日自動計算。NTP時刻と組み合わせて判定。

**側窓連動**: 結露対策中にハウス内温度を継続監視し、`apt_min_temp` 以上なら側窓を指定開度まで開けて換気効率を上げる。温度が下がれば側窓を閉じる。

---

## 急昇温ガード (Temperature Rate Guard)

急激な温度上昇（℃/分）を検知し、換気扇を強制起動する。

| 設定項目 | デフォルト | 説明 |
|---------|-----------|------|
| Rate Threshold | 2.0 ℃/min | この変化率以上でファンON |
| Fan Relay CH | -1 | 換気扇のリレーch |
| Sensor Source | SHT40 | SHT40 or DS18B20 |
| Hold Time | 120秒 | 変化率が閾値未満に戻ってからOFFまでの保持時間 |

---

## CO2ガード

CO2濃度が閾値以下になると、複数のリレーを個別の稼働時間で同時発動する。

| 設定項目 | 説明 |
|---------|------|
| Threshold | CO2閾値 (ppm) — これ以下で発動 |
| Action 1-4 | 各アクションごとにリレーchと稼働秒数を設定 |

**使用例**: CO2≤200ppm → 側窓1(300秒) + 側窓2(300秒) + ファン(600秒) + 暖房(180秒)

各アクションは独立したタイマーで管理され、設定秒数経過後に個別にOFFになる。全アクション完了後に再トリガー可能。

---

## 安全機能

### DI→リレー連動

WebUI `/ccm` ページで各リレーchにDI入力を紐付け。ネットワーク断でもローカルで即座に動作。

| 設定 | 用途例 |
|------|--------|
| DI1 → CH1 (normal) | 手元スイッチで換気扇ON |
| DI2 → CH3 (invert) | フロートスイッチON → 灌水ポンプOFF（水位上限） |

### 無通信ウォッチドッグ

ch毎に設定可能（0=無効、デフォルト60秒）。  
CCM受信が途絶えるとリレーを **安全側（OFF）** に強制。  
ArSprout 10秒間隔なら6回連続不達で停止。

---

## CCMマッピング

### 利用可能なCCMタイプ

| CCMタイプ | 日本語 | 用途 |
|-----------|--------|------|
| Irri | 灌水 | スイッチON/OFF |
| VenFan | 換気扇 | スイッチON/OFF |
| CirHoriFan | 循環扇 | スイッチON/OFF |
| AirHeatBurn | 暖房(燃焼) | スイッチON/OFF |
| AirHeatHP | 暖房(HP) | スイッチON/OFF |
| CO2Burn | CO2施用 | スイッチON/OFF |
| VenRfWin | 天窓 | 位置制御 |
| VenSdWin | 側窓 | 位置制御 |
| ThCrtn | 保温カーテン | 位置制御 |
| LsCrtn | 遮光カーテン | 位置制御 |
| AirCoolHP | 冷房 | スイッチON/OFF |
| AirHumFog | 加湿 | スイッチON/OFF |
| Relay | 汎用リレー | スイッチON/OFF |

### デフォルト設定

| CH | CCMタイプ | Room | Region | Order |
|----|-----------|------|--------|-------|
| 1 | Relay | 2 | 61 | 1 |
| 2 | Relay | 2 | 61 | 2 |
| ... | Relay | 2 | 61 | ... |
| 8 | Relay | 2 | 61 | 8 |

Room=2（ArSprout=Room 1との共存）、Order=CH番号。

---

## 設定ファイル (LittleFS)

| ファイル | 内容 |
|---------|------|
| `/config.json` | ネットワーク設定 (IP/mDNS/言語) |
| `/ccm_map.json` | CCMマッピング (ch⇔タイプ/Room/Region/DI連動) |
| `/gh_ctrl.json` | 温室制御ルール (4ルール、カーブ設定含む) |
| `/aperture.json` | 側窓開度制御 (4スロット、開閉秒数) |
| `/irri_ctrl.json` | 灌水制御 (2ルール、タイマー/デューティ) |
| `/dew_ctrl.json` | 結露防止設定 (側窓連動含む) |
| `/rate_ctrl.json` | 急昇温ガード設定 |
| `/co2_guard.json` | CO2ガード設定 (4アクション) |

---

## ファイル構成

```
.
├── ccm_rp2350_relay.ino   # メインFW (構造体・制御ロジック・JSON永続化)
├── web_common.h            # WebUI共通 (CSS・ナビ・JS)
├── web_i18n.h              # EN/JP言語切替 (L()マクロ)
├── web_dashboard.h         # ダッシュボード (GET /)
├── web_config.h            # ネットワーク設定 (GET /config, POST /api/config)
├── web_ccm.h               # CCMマッピング (GET /ccm, POST /api/ccm)
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
- [x] mDNS `uecs-ccm-01.local` アクセス確認
- [x] CCM multicast join + XML送信確認
- [x] WebUI 全ページ動作確認
- [x] OTA更新 動作確認 (248KB / ~7秒)
- [x] USB-UART デバッグコマンド確認
- [x] SHT40 I2C検出 + CCM送信
- [x] SCD41 I2C検出 + CO2読取 + CCM送信
- [x] SEN0575 SerialPIO (GPIO44/45) 検出 + Modbus RTU通信
- [x] DS18B20 1-Wire検出 + CCM送信
- [x] リレー調停レイヤー (relay_claims API確認)
- [x] EN/JP言語切替 WebUI動作確認
- [ ] 全8chリレー ON/OFF
- [ ] DI 8ch 割り込み検知
- [ ] DI→リレー連動
- [ ] CCM XML受信 → リレー制御
- [ ] WS2812 RGB LED 点灯確認
- [ ] ArSprout側で受信確認
- [ ] CO2ガード発動テスト
- [ ] 結露防止 時間帯テスト
- [ ] 急昇温ガード発動テスト
- [ ] 側窓開度制御 動作テスト

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
