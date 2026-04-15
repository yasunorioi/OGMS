// ogms.ino - OGMS (Open Greenhouse Management System) — Waveshare 8ch Relay + DI Node (RP2350B)
// Board: RP2350-ETH-8DI-8RO / RP2350-POE-ETH-8DI-8RO
// Protocol: MQTT over TCP (PubSubClient)
// Relay: GPIO17-24 直接制御
// DI:    GPIO9-16, フォトカプラ絶縁, アクティブLOW, 割り込み検知
// RTC:   PCF85063 (I2C1: SDA=GPIO6, SCL=GPIO7) — GPIO6/7 is I2C1 on RP2350B pinmux
// Comm:  W5500 SPI0 (CS=GPIO33, RST=GPIO25, SCK=GPIO34, MOSI=GPIO35, MISO=GPIO36)
// Framework: arduino-pico (Earle Philhower)
//
// Libraries: arduino-pico 4.5.2+, ArduinoJson 7.x, NTPClient 3.2.1,
//            W5500lwIP (arduino-pico内蔵), SensirionI2cSht4x (optional),
//            LEAmDNS (arduino-pico内蔵), PubSubClient 2.8

#include <SPI.h>
#include <W5500lwIP.h>
#include <ArduinoJson.h>        // v7.x
#include <LittleFS.h>
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <SensirionI2cSht4x.h>
#include <SensirionI2cScd4x.h>
#include <LEAmDNS.h>
#include <Updater.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "sw_watchdog.h"
#include "sensor_registry.h"

// ========== Firmware Version ==========
const char* FW_VERSION = "2.0.0";
const char* FW_NAME    = "ogms";

// ========== Default Configuration ==========
const char* DEFAULT_NODE_NAME     = "OGMS";
const char* DEFAULT_IP            = "";          // 空=DHCP
const char* DEFAULT_SUBNET        = "255.255.255.0";
const char* DEFAULT_GATEWAY       = "192.168.1.1";
const char* DEFAULT_DNS           = "192.168.1.1";
const bool  DEFAULT_MDNS_ENABLED   = true;
const char* DEFAULT_MDNS_HOSTNAME  = "ogms-01";
const char* DEFAULT_NODE_ID        = "ogms-01";

// ========== W5500 SPI1 Pins ==========
const int W5500_CS   = 33;
const int W5500_RST  = 25;
const int W5500_SCK  = 34;
const int W5500_MOSI = 35;
const int W5500_MISO = 36;
const int W5500_INT  = -1;

// ========== RS485 UART1 Pins ==========
const int RS485_TX = 4;
const int RS485_RX = 5;
const int RS485_DEFAULT_BAUD = 9600;

// ========== SEN0575 TTL UART (GPIO44/45 expansion header) ==========
const int SEN0575_TX_PIN = 44;  // RP2350 TX → SEN0575 C/R(RX)
const int SEN0575_RX_PIN = 45;  // RP2350 RX ← SEN0575 D/T(TX)
SerialPIO sen0575Serial(SEN0575_TX_PIN, SEN0575_RX_PIN, 64);

// ========== I2C0 Pins (RTC PCF85063) ==========
const int I2C_SDA = 6;  // I2C1 SDA (not I2C0 — GPIO6 is I2C1 per RP2350 pinmux)
const int I2C_SCL = 7;  // I2C1 SCL

// ========== WS2812 RGB LED ==========
const int WS2812_PIN = 2;   // GPIO2 (onboard)
const int WS2812_NUM = 1;   // 1 LED

// ========== DS18B20 OneWire ==========
const int ONEWIRE_PIN = 3;  // GPIO3 (Grove基板から引出し)

// ========== PCF85063 RTC ==========
const uint8_t PCF85063_ADDR = 0x51;

// ========== Relay GPIO Pins (GPIO17-24 直接制御) ==========
const int RELAY_PINS[8] = {17, 18, 19, 20, 21, 22, 23, 24};

// ========== DI GPIO Pins (GPIO9-16, アクティブLOW) ==========
const int DI_PINS[8] = {9, 10, 11, 12, 13, 14, 15, 16};

// ========== Relay Channel Config (per-ch DI link + MQTT duration) ==========
struct RelayChCfg {
  int  watchdog_sec;  // MQTT受信後N秒でauto-OFF (0=無効)
  int  di_link;       // DI連動 (-1=なし, 0-7=DI番号)
  bool di_invert;     // DI反転 (true: DI ON→リレーOFF)
};
RelayChCfg relayCh[8];

// ========== MQTT Config ==========
struct MqttConfig {
  bool enabled;
  char broker[64];
  int  port;
  char house_id[16];
  char client_id[32];
  int  publish_interval;
};
MqttConfig mqttCfg;

// ========== Greenhouse Local Control ==========
// 温度ベース比例制御。CCM受信優先 → 途絶時ローカルフォールバック

// 温度→開度カーブポイント（テーブル方式）
struct TempCurvePoint {
  float temp;   // 温度 (℃)
  float pct;    // 開度% (0-100)
};
const int MAX_CURVE_POINTS = 5;

struct GreenhouseCtrl {
  bool   enabled;        // ローカル制御有効
  int    ch;             // 制御対象リレーch (0-7, -1=none)
  float  temp_open;      // 開始温度 (この温度でデューティ>0%)
  float  temp_full;      // 全開温度 (この温度でデューティ100%)
  int    cycle_sec;      // 制御周期 秒 (e.g. 60 = 30sON+30sOFF at 50%)
  int    sensor_src;     // 0=SHT40, 1=DS18B20
  // Feature 2: 温度カーブ選択
  int    curve_mode;          // 0=linear, 1=table, 2=sigmoid, 3=exponential
  TempCurvePoint curve_points[MAX_CURVE_POINTS];
  int    curve_point_count;   // テーブルポイント数 (3-5)
  float  curve_coeff;         // sigmoid/exponential係数 (default 1.0)
};

const int GH_CTRL_SLOTS = 4;  // 最大4ルール
GreenhouseCtrl ghCtrl[GH_CTRL_SLOTS];

// ランタイム状態
struct GhRuntime {
  unsigned long cycleStart;  // 現在サイクルの開始時刻
  float         lastTemp;    // 最後に使った温度
  float         duty;        // 現在のデューティ比 (0.0-1.0)
  bool          active;      // 現在リレーON中か
};
GhRuntime ghRun[GH_CTRL_SLOTS];

// ========== Solar Irrigation Control ==========
// 積算日射量ベース灌水。PVSS-03 (0-1V=0-1000W/m²) → ADS1110 I2C ADC
// mode 0: タイマー灌水（従来）、mode 1: 排水率デューティ制御
struct IrrigationCtrl {
  bool   enabled;
  int    relay_ch;        // 灌水リレーch (0-7, -1=none)
  float  threshold_mj;    // 積算日射量閾値 (MJ/m²) — 到達で灌水開始
  int    duration_sec;    // 灌水時間 (秒) — mode 0で使用
  float  min_wm2;         // この日射量未満は積算しない (夜間ノイズ除外)
  int    drain_stop_sec;  // 排水検知で灌水停止する秒数 (0=無効, mode 0)
  // --- デューティモード (mode 1) ---
  int    mode;            // 0=タイマー, 1=排水率デューティ
  int    duty_cycle_sec;  // デューティサイクル秒 (e.g. 60)
  float  duty_init;       // 初期デューティ比 (0.0-1.0, e.g. 0.8)
  float  duty_min;        // 最小デューティ (e.g. 0.1)
  float  duty_max;        // 最大デューティ (e.g. 1.0)
  float  duty_step;       // 1サイクルごとの調整幅 (e.g. 0.05)
  float  drain_target_lo; // 排水率下限 (e.g. 0.15 = 15%)
  float  drain_target_hi; // 排水率上限 (e.g. 0.30 = 30%)
  int    flow_di_ch;      // 灌水流量パルスのDIch (0-1, diPulseCount index)
  float  flow_ml_per_pulse; // 1パルスあたりmL (e.g. 0.45 for DIGITEN G1)
  float  drain_ml_per_tip;  // SEN0575 1tipあたりmL (0.2mm×集水面積, e.g. 3.6)
};

const int IRRI_SLOTS = 2;  // 最大2ルール (e.g. 点滴+ミスト)
IrrigationCtrl irriCtrl[IRRI_SLOTS];

// ランタイム状態
struct IrriRuntime {
  float         accum_mj;       // 現在の積算日射量 (MJ/m²)
  bool          irrigating;     // 灌水中フラグ
  unsigned long irri_start;     // 灌水開始時刻 (millis)
  unsigned long last_sample;    // 最終サンプル時刻 (millis)
  int           today_count;    // 本日の灌水回数
  uint32_t      drain_prev_tips;    // 前回チェック時のtipカウント
  unsigned long drain_active_since; // 排水tip増加が始まった時刻 (0=非検知)
  // --- デューティモード ランタイム ---
  float         duty;               // 現在のデューティ比
  unsigned long duty_cycle_start;   // 現サイクル開始時刻
  uint32_t      cycle_flow_pulses;  // このサイクルの灌水パルス数
  uint32_t      cycle_drain_tips;   // このサイクルの排水tip数
  uint32_t      snap_flow_pulses;   // サイクル開始時のdiPulseCount snapshot
  uint32_t      snap_drain_tips;    // サイクル開始時のrawTips snapshot
  float         last_drain_rate;    // 直近の排水率
};
IrriRuntime irriRun[IRRI_SLOTS];

// ========== Dew Prevention (結露対策) ==========
// 日の出前に循環扇/暖房を起動して結露を防止
struct DewPreventionCtrl {
  bool   enabled;
  float  latitude;         // 緯度 (度、北半球+)
  float  longitude;        // 経度 (度、東経+)
  int    timezone_h;       // UTC offset (JST=9)
  int    before_sunrise_min; // 日の出の何分前に開始 (default 30)
  int    after_sunrise_min;  // 日の出の何分後に停止 (default 60)
  int    fan_relay_ch;     // 循環扇リレーch (0-7, -1=無効)
  int    heater_relay_ch;  // 暖房リレーch (0-7, -1=無効)
  int    apt_slot;         // 側窓スロット (0-3, -1=無効)
  float  apt_open_pct;     // 結露対策時の側窓開度 (%)
  float  apt_close_pct;    // 結露対策終了時の側窓開度 (%)
  float  apt_min_temp;     // 低温ガード: この温度以下では側窓を開けない (℃)
};

DewPreventionCtrl dewCtrl;

// ランタイム
struct DewRuntime {
  int   sunrise_min;       // 今日の日の出時刻 (ローカル時、0時からの分)
  int   last_calc_day;     // 最後に計算した日 (day_of_year)
  bool  active;            // 結露対策稼働中
  bool  apt_opened;        // 側窓が結露対策で開いているか
};
DewRuntime dewRun = {0, -1, false, false};

// ========== Temp Rate Guard (温度急変対策) ==========
// 温度変化率が閾値を超えたらリレー制御
struct TempRateGuard {
  bool   enabled;
  float  rate_threshold;   // ℃/分 の閾値 (e.g. 2.0 = 2℃/分以上で発動)
  int    fan_relay_ch;     // 換気扇リレーch (0-7, -1=無効)
  int    sensor_src;       // 0=SHT40, 1=DS18B20
  int    hold_sec;         // 発動後の最小保持時間 (秒)
};

TempRateGuard rateGuard;

// ランタイム
struct RateRuntime {
  float  prev_temp;        // 前回温度
  unsigned long prev_time; // 前回時刻 (millis)
  float  current_rate;     // 現在の変化率 (℃/分)
  bool   active;           // ガード発動中
  unsigned long active_since; // 発動開始時刻
};
RateRuntime rateRun = {NAN, 0, 0.0, false, 0};

// ========== CO2 Guard ==========
const int CO2_GUARD_ACTIONS = 4;
struct CO2GuardAction {
  int relay_ch;    // リレーch (0-7, -1=none)
  int duration_sec; // ON時間 (秒)
};
struct CO2GuardCtrl {
  bool enabled;
  uint16_t threshold_ppm; // この値以下で発動
  CO2GuardAction actions[CO2_GUARD_ACTIONS];
};
CO2GuardCtrl co2Guard;
struct CO2GuardRuntime {
  bool active;                          // 発動中
  unsigned long action_start[CO2_GUARD_ACTIONS]; // 各アクションのON開始時刻 (0=未発動)
  bool action_on[CO2_GUARD_ACTIONS];             // 各リレーがON中か
};
CO2GuardRuntime co2Run = {};

// ========== Aperture (Side Window) Control ==========
// 開/閉それぞれの動作秒数で側窓開度を制御
struct ApertureConfig {
  bool  enabled;           // 開度管理有効
  int   ch;                // 開方向リレーch (0-7)
  int   close_ch;          // 閉方向リレーch (-1=開chのOFF=閉)
  int   limit_di;          // リミットSW DIch (-1=無効)
  int   open_seconds;      // 全閉→全開の所要秒数
  int   close_seconds;     // 全開→全閉の所要秒数
};
const int APT_SLOTS = 4;

struct ApertureRuntime {
  float current_pct;         // 現在開度% (0-100)
  float target_pct;          // 目標開度% (0-100)
  bool  moving;              // 動作中
  bool  opening;             // true=開, false=閉
  unsigned long move_start;  // 動作開始時刻 (millis)
  int   move_duration_ms;    // 計算済み動作時間 (ms)
  bool  initializing;        // 起動初期化中（全閉動作中）
};

ApertureConfig  aptCtrl[APT_SLOTS];
ApertureRuntime aptRun[APT_SLOTS];

int g_language = 0;  // 0=EN, 1=JP

// ========== Timing ==========
const int           SENSOR_INTERVAL      = 10;
const int           ETH_CONNECT_TIMEOUT  = 15;
const unsigned long REBOOT_INTERVAL      = 600000UL;  // 10分
const unsigned long NTP_SYNC_INTERVAL    = 3600000UL;

// ========== HW WDT ==========
const int HW_WDT_TIMEOUT_MS = 8000;

// ========== Relay Arbitration Layer ==========
// 各制御者が claim/release でビットを立て、resolveAllRelays() で物理出力を確定
// OR合成（誰かがON→ON）、暖房chのみAND合成（全員ON→ON）
enum RelayOwner : uint8_t {
  OWN_GH       = 0,  // 温室温度制御
  OWN_IRRI     = 1,  // 灌水
  OWN_DEW      = 2,  // 結露防止
  OWN_RATE     = 3,  // 急昇温ガード
  OWN_CO2      = 4,  // CO2ガード
  OWN_MQTT     = 5,  // MQTT受信
  OWN_MANUAL   = 6,  // WebUI手動 / DI連動
  OWN_APT      = 7,  // 開度（側窓）制御
  OWN_COUNT    = 8
};
uint8_t relayClaims[8]     = {0};  // ビットマスク: bit N = OWN_xxx が ON 要求中
uint8_t relayInterested[8] = {0};  // ビットマスク: bit N = OWN_xxx がこのchに関与したことがある

// ========== Global State ==========
uint8_t      relayState          = 0x00;
unsigned long relayDurationEnd[8] = {0};

bool diState[8]     = {false};
bool diPrevState[8] = {false};
volatile bool     diInterruptFlag  = false;
volatile uint32_t diPulseCount[2]  = {0, 0};
unsigned long diLastDebounce  = 0;
const unsigned long DI_DEBOUNCE_MS = 50;

bool  sht40_detected    = false;
int   sht40_error_count = 0;
float g_sht40_temp      = NAN;
float g_sht40_hum       = NAN;

bool     scd41_detected    = false;
int      scd41_error_count = 0;
uint16_t g_scd41_co2       = 0;
float    g_scd41_temp      = NAN;
float    g_scd41_hum       = NAN;

bool  ds18b20_detected  = false;
float g_ds18b20_temp    = NAN;

// ========== ADS1110 ADC (M5Stack ADC Unit V1.1 / PVSS Solar Sensor) ==========
const uint8_t ADS1110_ADDR     = 0x48;  // default (ADDR=GND)
const uint8_t ADS1110_CFG_CONT_16BIT = 0x0C;  // continuous, 8SPS, gain=1, 16bit
bool  ads1110_detected  = false;
float g_solar_wm2       = NAN;  // instantaneous solar radiation (W/m²)

bool mdns_enabled = DEFAULT_MDNS_ENABLED;

unsigned long ntpEpoch  = 0;
unsigned long ntpMillis = 0;

int loopCount = 0;
unsigned long last_status = 0;  // [STATUS] 30秒タイマー

// ========== Objects ==========
// Note: eth must be constructed after SPI pin setup in initEthernet()
// GPIO33-36 are SPI0 pins on RP2350B (not SPI1)
Wiznet5500lwIP* ethPtr = nullptr;
#define eth (*ethPtr)
WiFiUDP        ntpUDP;
WiFiClient     ethClient;
PubSubClient   mqttClient(ethClient);
unsigned long  lastMqttPublish     = 0;
unsigned long  lastMqttReconnect   = 0;
unsigned long  mqttDurationEnd[8]  = {0};  // MQTT duration_secタイマー
bool           mqttOnline          = false;
float          g_mqtt_solar_wm2    = NAN;  // MQTT受信の日射フォールバック
NTPClient      timeClient(ntpUDP, "pool.ntp.org", 0);
SensirionI2cSht4x sht4x;
SensirionI2cScd4x scd4x;
Adafruit_NeoPixel rgbLED(WS2812_NUM, WS2812_PIN, NEO_GRB + NEO_KHZ800);
OneWire           oneWire(ONEWIRE_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiServer        webServer(80);

String nodeId;
String nodeName;
String mdnsHostname;
int    rs485Baud = RS485_DEFAULT_BAUD;

// ========== RS485 / SEN0575 ==========
// Modbus RTU register map (Input Registers, FC=0x04)
const uint8_t  SEN0575_ADDR              = 0xC0;
const uint16_t SEN0575_REG_PID           = 0x0000;  // PID (1 register)
const uint16_t SEN0575_REG_VID           = 0x0001;  // VID (1 register)
const uint16_t SEN0575_REG_CUMRAIN_L     = 0x0008;  // Cumulative rainfall Low
const uint16_t SEN0575_REG_CUMRAIN_H     = 0x0009;  // Cumulative rainfall High
const uint16_t SEN0575_REG_RAWDATA_L     = 0x000A;  // Raw tipping count Low
const uint16_t SEN0575_REG_RAWDATA_H     = 0x000B;  // Raw tipping count High
const uint16_t SEN0575_REG_SYSTIME       = 0x000C;  // System working time (min)

bool     sen0575_detected    = false;
uint32_t sen0575_cumRainRaw  = 0;
uint32_t sen0575_rawTips     = 0;
uint16_t sen0575_workTimeMins = 0;

// ========== Function Declarations ==========
void loadConfig();
void saveLangToConfig();
void loadRelayChConfig();
void saveRelayChConfig();
void loadMqttConfig();
void saveMqttConfig();
void initMqtt();
void mqttReconnect();
void mqttPublishAll();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void sendMqttConfigPage(WiFiClient& client);
void handleMqttConfigPost(WiFiClient& client, const String& body);
void initEthernet();
void setRelay(uint8_t ch, bool on);
void claimRelay(uint8_t ch, RelayOwner owner);
void releaseRelay(uint8_t ch, RelayOwner owner);
void resolveAllRelays();
void initRelaysOff();
bool readDI();
void scanI2CSensors();
void readSensors();
void syncNTP();
bool rtcGetTime(struct tm* t);
bool rtcSetTime(struct tm* t);
unsigned long getCurrentEpoch();
void rebootWithReason(const char* reason);
void handleWebClient();
void sendDashboard(WiFiClient& client);
void sendAPIState(WiFiClient& client);
void sendAPIConfig(WiFiClient& client);
void sendConfigPage(WiFiClient& client);
void handleRelayPost(WiFiClient& client, int ch, const String& body);
void handleConfigPost(WiFiClient& client, const String& body);
void sendOTAPage(WiFiClient& client);
void handleOTAUpload(WiFiClient& client, int contentLength);
void initRS485();
void pollDrainSensor();
uint16_t modbusCalcCRC(const uint8_t* data, size_t len);
bool modbusReadInput(uint8_t addr, uint16_t reg, uint16_t count,
                     uint16_t* out1, uint16_t* out2);
void loadGreenhouseConfig();
void saveGreenhouseConfig();
void greenhouseControl(unsigned long now);
void sendGreenhousePage(WiFiClient& client);
void handleGreenhousePost(WiFiClient& client, const String& body);
void handleAperturePost(WiFiClient& client, const String& body);
void loadIrrigationConfig();
void saveIrrigationConfig();
void irrigationControl(unsigned long now);
void sendIrrigationPage(WiFiClient& client);
void handleIrrigationPost(WiFiClient& client, const String& body);
void loadCO2GuardConfig();
void saveCO2GuardConfig();
void co2GuardControl(unsigned long now);
float readADS1110();
int calcSunriseMinLocal(int dayOfYear, float lat, float lon, int tz_h);
void loadDewConfig();
void saveDewConfig();
void dewPreventionControl(unsigned long now);
void loadRateGuardConfig();
void saveRateGuardConfig();
void tempRateGuardControl(unsigned long now);
void sendProtectionPage(WiFiClient& client);
void handleProtectionPost(WiFiClient& client, const String& body);
void loadApertureConfig();
void saveApertureConfig();
float calcMoveDuration(int slot, float from_pct, float to_pct);
void setTargetAperture(int slot, float target_pct);
void apertureControl(unsigned long now);

// ============================================================
// DI Interrupt
// ============================================================
void diPulseISR1() { diPulseCount[0]++; }
void diPulseISR2() { diPulseCount[1]++; }
void diISR() { diInterruptFlag = true; }

// ============================================================
// PCF85063 RTC helpers
// ============================================================
static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

bool rtcGetTime(struct tm* t) {
  Wire1.beginTransmission(PCF85063_ADDR);
  Wire1.write(0x04);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)PCF85063_ADDR, (uint8_t)7) != 7) return false;
  t->tm_sec  = bcd2dec(Wire1.read() & 0x7F);
  t->tm_min  = bcd2dec(Wire1.read() & 0x7F);
  t->tm_hour = bcd2dec(Wire1.read() & 0x3F);
  t->tm_mday = bcd2dec(Wire1.read() & 0x3F);
  Wire1.read();
  t->tm_mon  = bcd2dec(Wire1.read() & 0x1F) - 1;
  t->tm_year = bcd2dec(Wire1.read()) + 100;
  t->tm_isdst = 0;
  return true;
}

bool rtcSetTime(struct tm* t) {
  Wire1.beginTransmission(PCF85063_ADDR);
  Wire1.write(0x04);
  Wire1.write(dec2bcd(t->tm_sec));
  Wire1.write(dec2bcd(t->tm_min));
  Wire1.write(dec2bcd(t->tm_hour));
  Wire1.write(dec2bcd(t->tm_mday));
  Wire1.write(0);
  Wire1.write(dec2bcd(t->tm_mon + 1));
  Wire1.write(dec2bcd(t->tm_year - 100));
  return Wire1.endTransmission() == 0;
}

unsigned long getCurrentEpoch() {
  if (ntpEpoch == 0) return 0;
  return ntpEpoch + (millis() - ntpMillis) / 1000;
}

// ============================================================
// NTP Sync
// ============================================================
void syncNTP() {
  timeClient.begin();
  int retries = 5;
  while (!timeClient.update() && --retries > 0) {
    delay(1000);
    timeClient.forceUpdate();
  }
  if (retries == 0) {
    Serial.println("NTP: sync failed");
    return;
  }
  ntpEpoch  = timeClient.getEpochTime();
  ntpMillis = millis();
  Serial.printf("NTP: epoch=%lu (%s)\n", ntpEpoch, timeClient.getFormattedTime().c_str());

  unsigned long e = ntpEpoch;
  struct tm t;
  t.tm_sec  = e % 60; e /= 60;
  t.tm_min  = e % 60; e /= 60;
  t.tm_hour = e % 24; e /= 24;
  if (rtcSetTime(&t)) {
    Serial.println("RTC: time written (hms only)");
  }
}

// ============================================================
// Relay Control
// ============================================================
void initRelaysOff() {
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }
  relayState = 0x00;
  Serial.println("All relays OFF (safe startup state)");
}

void setRelay(uint8_t ch, bool on) {
  if (ch < 1 || ch > 8) return;
  uint8_t idx = ch - 1;
  digitalWrite(RELAY_PINS[idx], on ? HIGH : LOW);
  if (on) relayState |=  (1 << idx);
  else    relayState &= ~(1 << idx);
  Serial.printf("Relay CH%d %s  (state=0x%02X)\n", ch, on ? "ON" : "OFF", relayState);
}

// 暖房chか判定 (MQTT版: AND合成廃止、全チャンネルOR合成)
bool isHeaterCh(uint8_t idx) {
  (void)idx;
  return false;
}

// 制御者がリレーをON要求
void claimRelay(uint8_t ch, RelayOwner owner) {
  if (ch < 1 || ch > 8) return;
  uint8_t idx = ch - 1;
  relayClaims[idx]     |= (1 << owner);
  relayInterested[idx] |= (1 << owner);
}

// 制御者がリレーON要求を取り下げ
void releaseRelay(uint8_t ch, RelayOwner owner) {
  if (ch < 1 || ch > 8) return;
  uint8_t idx = ch - 1;
  relayClaims[idx]     &= ~(1 << owner);
  relayInterested[idx] |= (1 << owner);
}

// 全chのclaims→物理出力を確定（ループ末尾で1回呼ぶ）
void resolveAllRelays() {
  for (int i = 0; i < 8; i++) {
    bool shouldOn;
    if (isHeaterCh(i)) {
      // AND合成: 関与者全員がclaim中の時だけON
      // 「暑いのに暖房が入る」を防ぐ — 1人でもreleaseしたらOFF
      uint8_t interested = relayInterested[i];
      shouldOn = (interested != 0) && ((relayClaims[i] & interested) == interested);
    } else {
      // OR合成: 誰か1人でもON要求 → ON
      shouldOn = (relayClaims[i] != 0);
    }
    bool currentOn = (relayState >> i) & 1;
    if (shouldOn != currentOn) {
      setRelay(i + 1, shouldOn);
    }
  }
}

// ============================================================
// Digital Input
// ============================================================
bool readDI() {
  bool changed = false;
  for (int i = 0; i < 8; i++) {
    diState[i] = !digitalRead(DI_PINS[i]);
    if (diState[i] != diPrevState[i]) {
      changed = true;
      Serial.printf("DI%d: %s\n", i + 1, diState[i] ? "ON" : "OFF");
    }
  }
  memcpy(diPrevState, diState, sizeof(diState));
  return changed;
}

// ============================================================
// I2C Sensor
// ============================================================
void scanI2CSensors() {
  Wire1.setTimeout(10);
  Serial.println("I2C scan:");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (addr == PCF85063_ADDR) continue;
    Wire1.beginTransmission(addr);
    uint8_t rc = Wire1.endTransmission();
    if (rc == 0) {
      found++;
      Serial.printf("  0x%02X -> ", addr);
      bool matched = false;
      for (int i = 0; i < SENSOR_REGISTRY_SIZE; i++) {
        if (SENSOR_REGISTRY[i].addr == addr) {
          Serial.printf("%s\n", SENSOR_REGISTRY[i].name);
          if (SENSOR_REGISTRY[i].type == SENSOR_SHT40) sht40_detected = true;
          if (SENSOR_REGISTRY[i].type == SENSOR_SCD41) scd41_detected = true;
          if (SENSOR_REGISTRY[i].type == SENSOR_ADS1110) ads1110_detected = true;
          matched = true;
          break;
        }
      }
      if (!matched) Serial.println("unknown");
    }
  }
  if (found == 0) Serial.println("  no devices (continuing)");
  if (sht40_detected) {
    sht4x.begin(Wire1, 0x44);
    Serial.println("SHT40 initialized");
  }
  if (scd41_detected) {
    scd4x.begin(Wire1, 0x62);
    scd4x.stopPeriodicMeasurement();
    delay(500);
    scd4x.startPeriodicMeasurement();
    Serial.println("SCD41 initialized (CO2/Temp/Hum)");
  }
  if (ads1110_detected) {
    // Configure: continuous conversion, 8 SPS, PGA gain=1
    Wire1.beginTransmission(ADS1110_ADDR);
    Wire1.write(ADS1110_CFG_CONT_16BIT);
    Wire1.endTransmission();
    Serial.println("ADS1110 initialized (solar ADC)");
  }
}

void readSensors() {
  // SHT40 (I2C)
  if (sht40_detected) {
    float temp, hum;
    uint16_t err;
    char msg[64];
    err = sht4x.measureHighPrecision(temp, hum);
    if (err) {
      errorToString(err, msg, sizeof(msg));
      sht40_error_count++;
      Serial.printf("SHT40 error (%d/3): %s\n", sht40_error_count, msg);
      if (sht40_error_count >= 3) {
        sht40_detected = false;
        Serial.println("SHT40: disabled after 3 errors");
      }
    } else {
      sht40_error_count = 0;
      g_sht40_temp = temp;
      g_sht40_hum  = hum;
    }
  }

  // SCD41 (I2C)
  if (scd41_detected) {
    uint16_t co2; float temp, hum;
    bool dataReady = false;
    scd4x.getDataReadyStatus(dataReady);
    if (dataReady) {
      uint16_t err = scd4x.readMeasurement(co2, temp, hum);
      if (err) {
        scd41_error_count++;
        if (scd41_error_count >= 5) {
          scd41_detected = false;
          Serial.println("SCD41: disabled after 5 errors");
        }
      } else {
        scd41_error_count = 0;
        g_scd41_co2  = co2;
        g_scd41_temp = temp;
        g_scd41_hum  = hum;
      }
    }
  }

  // DS18B20 (OneWire)
  if (ds18b20_detected) {
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55.0 && t < 125.0) {
      g_ds18b20_temp = t;
    } else {
      g_ds18b20_temp = NAN;
    }
  }

  // ADS1110 → Solar radiation (PVSS-03: 0-1V = 0-1000 W/m²)
  if (ads1110_detected) {
    float v = readADS1110();
    if (!isnan(v)) {
      // PVSS-03: 0-1V linear → 0-1000 W/m²
      float wm2 = v * 1000.0;
      if (wm2 < 0.0) wm2 = 0.0;
      if (wm2 > 2000.0) wm2 = NAN;  // sanity check
      g_solar_wm2 = wm2;
    }
  }
}

// ============================================================
// ADS1110 (M5Stack ADC Unit V1.1) — 16bit signed, Vref=2.048V
// ============================================================
float readADS1110() {
  if (Wire1.requestFrom(ADS1110_ADDR, (uint8_t)3) != 3) return NAN;
  uint8_t hi  = Wire1.read();
  uint8_t lo  = Wire1.read();
  uint8_t cfg = Wire1.read();
  (void)cfg;
  int16_t raw = ((int16_t)hi << 8) | lo;
  // Vref=2.048V, gain=1, 16bit signed (max=+32767)
  float voltage = (float)raw / 32767.0 * 2.048;
  if (voltage < -0.01) return NAN;
  return voltage;
}

// ============================================================
// RS485 / Modbus RTU — DFRobot SEN0575
// ============================================================
uint16_t modbusCalcCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else               crc >>= 1;
    }
  }
  return crc;
}

void initRS485() {
  // RS485 transceiver on UART1 (GPIO4/5) — kept for future RS485 devices
  Serial2.setTX(RS485_TX);
  Serial2.setRX(RS485_RX);
  Serial2.begin(rs485Baud);
  Serial.printf("RS485: UART1 TX=GPIO%d RX=GPIO%d baud=%d\n", RS485_TX, RS485_RX, rs485Baud);

  // SEN0575: TTL UART via SerialPIO on GPIO44/45 (expansion header)
  sen0575Serial.begin(9600);
  Serial.printf("SEN0575: SerialPIO TX=GPIO%d RX=GPIO%d baud=9600\n",
                SEN0575_TX_PIN, SEN0575_RX_PIN);

  delay(100);
  // Read PID and VID as separate registers (DFRobot library convention)
  uint16_t pidReg = 0, vidReg = 0;
  bool pidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_PID, 1, &pidReg, nullptr);
  delay(50);
  bool vidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_VID, 1, &vidReg, nullptr);
  if (pidOk && vidOk) {
    uint32_t pid = ((uint32_t)(vidReg & 0xC000) << 2) | pidReg;
    sen0575_detected = (pid == 0x000100C0);
    Serial.printf("SEN0575: PID=0x%05lX VID=0x%04X %s\n", pid, vidReg,
                  sen0575_detected ? "DETECTED" : "PID mismatch");
  } else {
    Serial.println("SEN0575: not found");
  }
}

bool modbusReadInput(uint8_t addr, uint16_t reg, uint16_t count,
                     uint16_t* out1, uint16_t* out2) {
  uint8_t req[6];
  req[0] = addr;
  req[1] = 0x04;
  req[2] = (reg >> 8) & 0xFF;
  req[3] = reg & 0xFF;
  req[4] = (count >> 8) & 0xFF;
  req[5] = count & 0xFF;
  uint16_t crc = modbusCalcCRC(req, 6);
  uint8_t frame[8];
  memcpy(frame, req, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  while (sen0575Serial.available()) sen0575Serial.read();
  sen0575Serial.write(frame, 8);
  sen0575Serial.flush();

  const int respLen = 3 + count * 2 + 2;
  uint8_t resp[11];
  if (respLen > (int)sizeof(resp)) return false;

  int received = 0;
  unsigned long deadline = millis() + 1000UL;
  while (received < respLen && millis() < deadline) {
    if (sen0575Serial.available()) resp[received++] = (uint8_t)sen0575Serial.read();
  }
  if (received < respLen) return false;

  uint16_t rxCRC = (uint16_t)resp[respLen - 2] | ((uint16_t)resp[respLen - 1] << 8);
  if (modbusCalcCRC(resp, respLen - 2) != rxCRC) return false;
  if (resp[0] != addr || resp[1] != 0x04 || resp[2] != count * 2) return false;

  *out1 = ((uint16_t)resp[3] << 8) | resp[4];
  if (count >= 2 && out2) *out2 = ((uint16_t)resp[5] << 8) | resp[6];
  return true;
}

void pollDrainSensor() {
  if (!sen0575_detected) return;

  uint16_t cumL = 0, cumH = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_CUMRAIN_L, 2, &cumL, &cumH)) {
    sen0575_cumRainRaw = ((uint32_t)cumH << 16) | cumL;
  }
  delay(50);

  uint16_t rawL = 0, rawH = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_RAWDATA_L, 2, &rawL, &rawH)) {
    sen0575_rawTips = ((uint32_t)rawH << 16) | rawL;
  }
  delay(50);

  uint16_t wt = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_SYSTIME, 1, &wt, nullptr)) {
    sen0575_workTimeMins = wt;
  }

  float rainfall_mm = sen0575_cumRainRaw / 10000.0;
  float workHours   = sen0575_workTimeMins / 60.0;
  Serial.printf("SEN0575: rain=%.2fmm tips=%lu work=%.1fh\n",
                rainfall_mm, (unsigned long)sen0575_rawTips, workHours);
}

// ============================================================
// Relay Channel Config (DI link + MQTT watchdog)
// ============================================================
void loadRelayChConfig() {
  for (int i = 0; i < 8; i++) {
    relayCh[i].watchdog_sec = 0;
    relayCh[i].di_link      = -1;
    relayCh[i].di_invert    = false;
  }
  if (!LittleFS.exists("/relay_ch.json")) return;
  File f = LittleFS.open("/relay_ch.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  JsonArray arr = doc["channels"].as<JsonArray>();
  if (!arr) return;
  int idx = 0;
  for (JsonObject ch : arr) {
    if (idx >= 8) break;
    relayCh[idx].watchdog_sec = ch["watchdog_sec"] | 0;
    relayCh[idx].di_link      = ch["di_link"]      | -1;
    relayCh[idx].di_invert    = ch["di_invert"]    | false;
    idx++;
  }
}

void saveRelayChConfig() {
  JsonDocument doc;
  JsonArray arr = doc["channels"].to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    JsonObject ch = arr.add<JsonObject>();
    ch["watchdog_sec"] = relayCh[i].watchdog_sec;
    ch["di_link"]      = relayCh[i].di_link;
    ch["di_invert"]    = relayCh[i].di_invert;
  }
  File f = LittleFS.open("/relay_ch.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ============================================================
// MQTT Config (LittleFS /mqtt_config.json)
// ============================================================
void loadMqttConfig() {
  mqttCfg.enabled          = true;
  strncpy(mqttCfg.broker,       "192.168.100.1", sizeof(mqttCfg.broker));
  mqttCfg.port             = 1883;
  strncpy(mqttCfg.house_id,     "h01", sizeof(mqttCfg.house_id));
  mqttCfg.client_id[0]     = '\0';
  mqttCfg.publish_interval = 10;

  if (!LittleFS.exists("/mqtt_config.json")) return;
  File f = LittleFS.open("/mqtt_config.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();
  mqttCfg.enabled          = doc["enabled"]          | true;
  const char* br = doc["broker"] | "192.168.100.1";
  strncpy(mqttCfg.broker, br, sizeof(mqttCfg.broker) - 1);
  mqttCfg.port             = doc["port"]             | 1883;
  const char* hi = doc["house_id"] | "h01";
  strncpy(mqttCfg.house_id, hi, sizeof(mqttCfg.house_id) - 1);
  const char* ci = doc["client_id"] | "";
  strncpy(mqttCfg.client_id, ci, sizeof(mqttCfg.client_id) - 1);
  mqttCfg.publish_interval = doc["publish_interval"] | 10;
}

void saveMqttConfig() {
  JsonDocument doc;
  doc["enabled"]          = mqttCfg.enabled;
  doc["broker"]           = mqttCfg.broker;
  doc["port"]             = mqttCfg.port;
  doc["house_id"]         = mqttCfg.house_id;
  doc["client_id"]        = mqttCfg.client_id;
  doc["publish_interval"] = mqttCfg.publish_interval;
  File f = LittleFS.open("/mqtt_config.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ============================================================
// MQTT callback — relay/{ch}/set, farm/weather/#
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char buf[256];
  if (length >= sizeof(buf)) length = sizeof(buf) - 1;
  memcpy(buf, payload, length);
  buf[length] = '\0';

  String topicStr(topic);
  String base = String("agriha/") + mqttCfg.house_id + "/relay/";

  // relay/{ch}/set
  for (int ch = 1; ch <= 8; ch++) {
    String expected = base + String(ch) + "/set";
    if (topicStr == expected) {
      JsonDocument doc;
      if (deserializeJson(doc, buf) != DeserializationError::Ok) return;
      int value = doc["value"] | -1;
      int dur   = doc["duration_sec"] | 0;
      if (value < 0) return;
      if (value > 0) {
        claimRelay(ch, OWN_MQTT);
        if (dur > 0) {
          mqttDurationEnd[ch - 1] = millis() + (unsigned long)dur * 1000UL;
        } else {
          mqttDurationEnd[ch - 1] = 0;
        }
      } else {
        releaseRelay(ch, OWN_MQTT);
        mqttDurationEnd[ch - 1] = 0;
      }
      Serial.printf("MQTT RX relay/%d/set value=%d dur=%d\n", ch, value, dur);
      return;
    }
  }

  // agriha/farm/weather/solar → g_mqtt_solar_wm2
  String weatherBase = "agriha/farm/weather/";
  if (topicStr.startsWith(weatherBase)) {
    String sub = topicStr.substring(weatherBase.length());
    if (sub == "solar") {
      JsonDocument doc;
      if (deserializeJson(doc, buf) == DeserializationError::Ok) {
        float wm2 = doc["solar_wm2"] | doc["value"] | (float)NAN;
        if (!isnan(wm2) && wm2 >= 0.0 && wm2 <= 2000.0) {
          g_mqtt_solar_wm2 = wm2;
          Serial.printf("MQTT RX farm/weather/solar=%.1f W/m2\n", wm2);
        }
      }
    }
  }
}

// ============================================================
// MQTT init & reconnect
// ============================================================
void initMqtt() {
  if (!mqttCfg.enabled) return;

  // Auto-generate client_id from MAC if not set
  if (mqttCfg.client_id[0] == '\0') {
    uint8_t mac[6];
    eth.macAddress(mac);
    snprintf(mqttCfg.client_id, sizeof(mqttCfg.client_id),
             "ogms-%02X%02X", mac[4], mac[5]);
  }

  mqttClient.setServer(mqttCfg.broker, mqttCfg.port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
}

void mqttReconnect() {
  if (!mqttCfg.enabled) return;
  if (mqttClient.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttReconnect < 5000UL) return;
  lastMqttReconnect = now;

  char willTopic[64];
  snprintf(willTopic, sizeof(willTopic), "agriha/%s/status", mqttCfg.house_id);

  char willPayload[] = "{\"online\":false}";

  Serial.printf("MQTT: connecting to %s:%d as %s ...\n",
                mqttCfg.broker, mqttCfg.port, mqttCfg.client_id);

  bool ok = mqttClient.connect(mqttCfg.client_id,
                               nullptr, nullptr,
                               willTopic, 1, true, willPayload);
  if (!ok) {
    Serial.printf("MQTT: connect failed, rc=%d\n", mqttClient.state());
    return;
  }

  Serial.println("MQTT: connected");
  mqttOnline = true;

  // Publish online status
  char onlinePayload[64];
  unsigned long ep = getCurrentEpoch();
  if (ep > 0) {
    snprintf(onlinePayload, sizeof(onlinePayload), "{\"online\":true,\"ts\":%lu}", ep);
  } else {
    strncpy(onlinePayload, "{\"online\":true}", sizeof(onlinePayload));
  }
  mqttClient.publish(willTopic, onlinePayload, true);

  // Subscribe: relay/{ch}/set
  char subTopic[64];
  snprintf(subTopic, sizeof(subTopic), "agriha/%s/relay/+/set", mqttCfg.house_id);
  mqttClient.subscribe(subTopic, 1);
  Serial.printf("MQTT: subscribed %s\n", subTopic);

  // Subscribe: farm/weather/#
  mqttClient.subscribe("agriha/farm/weather/#", 1);
  Serial.println("MQTT: subscribed agriha/farm/weather/#");
}

// ============================================================
// MQTT publish all sensors + relay state
// ============================================================
void mqttPublishAll() {
  if (!mqttClient.connected()) return;

  unsigned long ep = getCurrentEpoch();
  char topic[80];
  char payload[200];

  // SHT40
  if (sht40_detected && !isnan(g_sht40_temp)) {
    snprintf(topic, sizeof(topic), "agriha/%s/sensor/SHT40", mqttCfg.house_id);
    if (ep > 0)
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"humidity_pct\":%.1f,\"timestamp\":%lu}",
               g_sht40_temp, g_sht40_hum, ep);
    else
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"humidity_pct\":%.1f,\"timestamp\":0}",
               g_sht40_temp, g_sht40_hum);
    mqttClient.publish(topic, payload, true);
  }

  // SCD41
  if (scd41_detected && g_scd41_co2 > 0) {
    snprintf(topic, sizeof(topic), "agriha/%s/sensor/SCD41", mqttCfg.house_id);
    if (ep > 0)
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"humidity_pct\":%.1f,\"co2_ppm\":%u,\"timestamp\":%lu}",
               g_scd41_temp, g_scd41_hum, g_scd41_co2, ep);
    else
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"humidity_pct\":%.1f,\"co2_ppm\":%u,\"timestamp\":0}",
               g_scd41_temp, g_scd41_hum, g_scd41_co2);
    mqttClient.publish(topic, payload, true);
  }

  // DS18B20
  if (ds18b20_detected && !isnan(g_ds18b20_temp)) {
    snprintf(topic, sizeof(topic), "agriha/%s/sensor/DS18B20", mqttCfg.house_id);
    if (ep > 0)
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"timestamp\":%lu}", g_ds18b20_temp, ep);
    else
      snprintf(payload, sizeof(payload),
               "{\"temperature_c\":%.2f,\"timestamp\":0}", g_ds18b20_temp);
    mqttClient.publish(topic, payload, true);
  }

  // ADS1110 (solar)
  if (ads1110_detected && !isnan(g_solar_wm2)) {
    snprintf(topic, sizeof(topic), "agriha/%s/sensor/ADS1110", mqttCfg.house_id);
    if (ep > 0)
      snprintf(payload, sizeof(payload),
               "{\"solar_wm2\":%.1f,\"timestamp\":%lu}", g_solar_wm2, ep);
    else
      snprintf(payload, sizeof(payload),
               "{\"solar_wm2\":%.1f,\"timestamp\":0}", g_solar_wm2);
    mqttClient.publish(topic, payload, true);
  }

  // SEN0575 (rain/drain)
  if (sen0575_detected) {
    snprintf(topic, sizeof(topic), "agriha/%s/sensor/SEN0575", mqttCfg.house_id);
    float drain_mm = (float)sen0575_cumRainRaw * 0.2f;
    if (ep > 0)
      snprintf(payload, sizeof(payload),
               "{\"drain_mm\":%.1f,\"timestamp\":%lu}", drain_mm, ep);
    else
      snprintf(payload, sizeof(payload),
               "{\"drain_mm\":%.1f,\"timestamp\":0}", drain_mm);
    mqttClient.publish(topic, payload, true);
  }

  // relay/state
  {
    snprintf(topic, sizeof(topic), "agriha/%s/relay/state", mqttCfg.house_id);
    String s = "{";
    for (int i = 0; i < 8; i++) {
      if (i > 0) s += ",";
      s += "\"ch" + String(i + 1) + "\":" + (((relayState >> i) & 1) ? "1" : "0");
    }
    if (ep > 0) s += ",\"ts\":" + String(ep);
    else        s += ",\"ts\":0";
    s += "}";
    mqttClient.publish(topic, s.c_str(), true);
  }
}

// ============================================================
// Aperture (Side Window) Control — Config & Logic
// ============================================================
void loadApertureConfig() {
  for (int i = 0; i < APT_SLOTS; i++) {
    aptCtrl[i].enabled       = false;
    aptCtrl[i].ch            = -1;
    aptCtrl[i].close_ch      = -1;
    aptCtrl[i].limit_di      = -1;
    aptCtrl[i].open_seconds  = 60;
    aptCtrl[i].close_seconds = 60;
    aptRun[i] = {0.0, 0.0, false, false, 0, 0, false};
  }
  if (!LittleFS.exists("/aperture.json")) return;
  File f = LittleFS.open("/aperture.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  JsonArray arr = doc["slots"].as<JsonArray>();
  int idx = 0;
  for (JsonObject s : arr) {
    if (idx >= APT_SLOTS) break;
    aptCtrl[idx].enabled       = s["enabled"] | false;
    aptCtrl[idx].ch            = s["ch"]       | -1;
    aptCtrl[idx].close_ch      = s["close_ch"] | -1;
    aptCtrl[idx].limit_di      = s["limit_di"] | -1;
    aptCtrl[idx].open_seconds  = constrain((int)(s["open_sec"]  | 60), 1, 300);
    aptCtrl[idx].close_seconds = constrain((int)(s["close_sec"] | 60), 1, 300);
    idx++;
  }
}

void saveApertureConfig() {
  JsonDocument doc;
  JsonArray arr = doc["slots"].to<JsonArray>();
  for (int i = 0; i < APT_SLOTS; i++) {
    JsonObject s = arr.add<JsonObject>();
    s["enabled"]   = aptCtrl[i].enabled;
    s["ch"]        = aptCtrl[i].ch;
    s["close_ch"]  = aptCtrl[i].close_ch;
    s["limit_di"]  = aptCtrl[i].limit_di;
    s["open_sec"]  = aptCtrl[i].open_seconds;
    s["close_sec"] = aptCtrl[i].close_seconds;
  }
  File f = LittleFS.open("/aperture.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// 開度fromからtoへの所要時間(ms)を線形比例で計算
float calcMoveDuration(int slot, float from_pct, float to_pct) {
  if (slot < 0 || slot >= APT_SLOTS) return 0;
  float dist = fabsf(to_pct - from_pct);
  if (dist < 0.1) return 0;
  bool opening = (to_pct > from_pct);
  int total_sec = opening ? aptCtrl[slot].open_seconds : aptCtrl[slot].close_seconds;
  return (dist / 100.0) * total_sec * 1000.0;
}

void setTargetAperture(int slot, float target_pct) {
  if (slot < 0 || slot >= APT_SLOTS) return;
  if (!aptCtrl[slot].enabled || aptCtrl[slot].ch < 0) return;
  target_pct = constrain(target_pct, 0.0, 100.0);
  aptRun[slot].target_pct = target_pct;
  float from = aptRun[slot].current_pct;
  if (fabsf(target_pct - from) < 0.5) return;
  float dur_ms = calcMoveDuration(slot, from, target_pct);
  if (dur_ms <= 0) return;
  // 動作中なら一旦停止
  if (aptRun[slot].moving) {
    releaseRelay(aptCtrl[slot].ch + 1, OWN_APT);
    if (aptCtrl[slot].close_ch >= 0) releaseRelay(aptCtrl[slot].close_ch + 1, OWN_APT);
  }
  aptRun[slot].moving = true;
  aptRun[slot].opening = (target_pct > from);
  aptRun[slot].move_start = millis();
  aptRun[slot].move_duration_ms = (int)dur_ms;
  if (aptRun[slot].opening) {
    claimRelay(aptCtrl[slot].ch + 1, OWN_APT);
    if (aptCtrl[slot].close_ch >= 0) releaseRelay(aptCtrl[slot].close_ch + 1, OWN_APT);
  } else {
    if (aptCtrl[slot].close_ch >= 0) {
      claimRelay(aptCtrl[slot].close_ch + 1, OWN_APT);
      releaseRelay(aptCtrl[slot].ch + 1, OWN_APT);
    } else {
      releaseRelay(aptCtrl[slot].ch + 1, OWN_APT);
    }
  }
}

void apertureControl(unsigned long now) {
  bool diNow[8] = {false};
  for (int i = 0; i < 8; i++) diNow[i] = diState[i];

  for (int i = 0; i < APT_SLOTS; i++) {
    if (!aptCtrl[i].enabled || aptCtrl[i].ch < 0) continue;

    // リミットSW検知（閉じ動作中 or initializing中）
    if (aptCtrl[i].limit_di >= 0 && aptCtrl[i].limit_di < 8) {
      bool limitHit = diNow[aptCtrl[i].limit_di];
      if (limitHit && (!aptRun[i].opening || aptRun[i].initializing)) {
        // 全閉確定
        aptRun[i].current_pct = 0.0;
        aptRun[i].moving = false;
        aptRun[i].initializing = false;
        releaseRelay(aptCtrl[i].ch + 1, OWN_APT);
        if (aptCtrl[i].close_ch >= 0) releaseRelay(aptCtrl[i].close_ch + 1, OWN_APT);
        continue;
      }
    }

    if (aptRun[i].initializing) {
      // 初期化中: 全閉方向に動作
      if (!aptRun[i].moving) {
        float dur_ms = calcMoveDuration(i, aptRun[i].current_pct, 0.0);
        if (dur_ms < 100) dur_ms = aptCtrl[i].close_seconds * 1000.0;
        aptRun[i].moving = true;
        aptRun[i].opening = false;
        aptRun[i].move_start = now;
        aptRun[i].move_duration_ms = (int)dur_ms + 2000;  // +2s マージン
        if (aptCtrl[i].close_ch >= 0) {
          claimRelay(aptCtrl[i].close_ch + 1, OWN_APT);
          releaseRelay(aptCtrl[i].ch + 1, OWN_APT);
        } else {
          releaseRelay(aptCtrl[i].ch + 1, OWN_APT);
        }
      }
      // タイムアウトで強制全閉扱い
      if ((long)(now - aptRun[i].move_start) >= aptRun[i].move_duration_ms) {
        aptRun[i].current_pct = 0.0;
        aptRun[i].moving = false;
        aptRun[i].initializing = false;
        releaseRelay(aptCtrl[i].ch + 1, OWN_APT);
        if (aptCtrl[i].close_ch >= 0) releaseRelay(aptCtrl[i].close_ch + 1, OWN_APT);
      }
      continue;
    }

    // 通常動作: 動作完了チェック
    if (aptRun[i].moving) {
      if ((long)(now - aptRun[i].move_start) >= aptRun[i].move_duration_ms) {
        aptRun[i].current_pct = aptRun[i].target_pct;
        aptRun[i].moving = false;
        releaseRelay(aptCtrl[i].ch + 1, OWN_APT);
        if (aptCtrl[i].close_ch >= 0) releaseRelay(aptCtrl[i].close_ch + 1, OWN_APT);
        Serial.printf("[APT] slot%d done: %.0f%%\n", i + 1, aptRun[i].current_pct);
      }
    }
  }
}

// ============================================================
// Greenhouse Local Control — Config & Logic
// ============================================================
void loadGreenhouseConfig() {
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    ghCtrl[i].enabled   = false;
    ghCtrl[i].ch        = -1;
    ghCtrl[i].temp_open = 25.0;
    ghCtrl[i].temp_full = 30.0;
    ghCtrl[i].cycle_sec = 60;
    ghCtrl[i].sensor_src = 0;
    ghRun[i] = {0, NAN, 0.0, false};
  }
  if (!LittleFS.exists("/gh_ctrl.json")) return;
  File f = LittleFS.open("/gh_ctrl.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  JsonArray arr = doc["rules"].as<JsonArray>();
  int idx = 0;
  for (JsonObject r : arr) {
    if (idx >= GH_CTRL_SLOTS) break;
    ghCtrl[idx].enabled    = r["enabled"]    | false;
    ghCtrl[idx].ch         = r["ch"]         | -1;
    ghCtrl[idx].temp_open  = r["temp_open"]  | 25.0;
    ghCtrl[idx].temp_full  = r["temp_full"]  | 30.0;
    ghCtrl[idx].cycle_sec  = r["cycle_sec"]  | 60;
    ghCtrl[idx].sensor_src = r["sensor_src"] | 0;
    ghCtrl[idx].curve_mode        = r["curve_mode"]        | 0;
    ghCtrl[idx].curve_coeff       = r["curve_coeff"]       | 1.0;
    ghCtrl[idx].curve_point_count = constrain((int)(r["curve_point_count"] | 3), 2, MAX_CURVE_POINTS);
    JsonArray pts = r["curve_points"].as<JsonArray>();
    int pi = 0;
    for (JsonObject p : pts) {
      if (pi >= MAX_CURVE_POINTS) break;
      ghCtrl[idx].curve_points[pi].temp = p["temp"] | 25.0;
      ghCtrl[idx].curve_points[pi].pct  = p["pct"]  | 0.0;
      pi++;
    }
    idx++;
  }
  Serial.printf("Greenhouse: %d rules loaded\n", idx);
}

void saveGreenhouseConfig() {
  JsonDocument doc;
  JsonArray arr = doc["rules"].to<JsonArray>();
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    JsonObject r = arr.add<JsonObject>();
    r["enabled"]    = ghCtrl[i].enabled;
    r["ch"]         = ghCtrl[i].ch;
    r["temp_open"]  = ghCtrl[i].temp_open;
    r["temp_full"]  = ghCtrl[i].temp_full;
    r["cycle_sec"]  = ghCtrl[i].cycle_sec;
    r["sensor_src"] = ghCtrl[i].sensor_src;
    r["curve_mode"]        = ghCtrl[i].curve_mode;
    r["curve_coeff"]       = ghCtrl[i].curve_coeff;
    r["curve_point_count"] = ghCtrl[i].curve_point_count;
    JsonArray pts = r["curve_points"].to<JsonArray>();
    for (int j = 0; j < ghCtrl[i].curve_point_count; j++) {
      JsonObject p = pts.add<JsonObject>();
      p["temp"] = ghCtrl[i].curve_points[j].temp;
      p["pct"]  = ghCtrl[i].curve_points[j].pct;
    }
  }
  File f = LittleFS.open("/gh_ctrl.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
  Serial.println("Greenhouse config saved");
}

void greenhouseControl(unsigned long now) {
  for (int i = 0; i < GH_CTRL_SLOTS; i++) {
    if (!ghCtrl[i].enabled || ghCtrl[i].ch < 0 || ghCtrl[i].ch > 7) continue;
    int ch = ghCtrl[i].ch;

    // 温度取得
    float temp = NAN;
    if (ghCtrl[i].sensor_src == 0 && !isnan(g_sht40_temp)) {
      temp = g_sht40_temp;
    } else if (ghCtrl[i].sensor_src == 1 && !isnan(g_ds18b20_temp)) {
      temp = g_ds18b20_temp;
    }
    if (isnan(temp)) continue;  // センサー無し→制御しない

    ghRun[i].lastTemp = temp;

    // デューティ比計算 (カーブ選択式)
    float duty = 0.0;
    float t_lo = ghCtrl[i].temp_open;
    float t_hi = ghCtrl[i].temp_full;
    if (temp >= t_hi) {
      duty = 1.0;
    } else if (temp > t_lo) {
      float t_norm = (t_hi > t_lo) ? (temp - t_lo) / (t_hi - t_lo) : 0.0;
      switch (ghCtrl[i].curve_mode) {
        case 1: { // Table interpolation
          float d = 0.0;
          int cnt = ghCtrl[i].curve_point_count;
          if (cnt >= 2) {
            // curve_points は temp昇順を前提
            if (temp <= ghCtrl[i].curve_points[0].temp) {
              d = ghCtrl[i].curve_points[0].pct / 100.0;
            } else if (temp >= ghCtrl[i].curve_points[cnt-1].temp) {
              d = ghCtrl[i].curve_points[cnt-1].pct / 100.0;
            } else {
              for (int p = 0; p < cnt - 1; p++) {
                float p0t = ghCtrl[i].curve_points[p].temp;
                float p1t = ghCtrl[i].curve_points[p+1].temp;
                if (temp >= p0t && temp < p1t) {
                  float frac = (p1t > p0t) ? (temp - p0t) / (p1t - p0t) : 0.0;
                  d = (ghCtrl[i].curve_points[p].pct + frac * (ghCtrl[i].curve_points[p+1].pct - ghCtrl[i].curve_points[p].pct)) / 100.0;
                  break;
                }
              }
            }
          }
          duty = constrain(d, 0.0, 1.0);
          break;
        }
        case 2: { // Sigmoid
          float k = ghCtrl[i].curve_coeff;
          float midpoint = (t_lo + t_hi) / 2.0;
          duty = 1.0 / (1.0 + expf(-k * (temp - midpoint)));
          duty = constrain(duty, 0.0, 1.0);
          break;
        }
        case 3: { // Exponential
          float k = ghCtrl[i].curve_coeff;
          float denom = expf(k) - 1.0;
          duty = (denom > 0.001) ? (expf(k * t_norm) - 1.0) / denom : t_norm;
          duty = constrain(duty, 0.0, 1.0);
          break;
        }
        default: // Linear (0)
          duty = t_norm;
          break;
      }
    }
    ghRun[i].duty = duty;

    // サイクル制御 (デューティ比でON/OFF切替)
    unsigned long cycleDur = (unsigned long)ghCtrl[i].cycle_sec * 1000UL;
    if (cycleDur == 0) cycleDur = 60000;
    if (ghRun[i].cycleStart == 0) ghRun[i].cycleStart = now;
    unsigned long elapsed = now - ghRun[i].cycleStart;
    if (elapsed >= cycleDur) {
      ghRun[i].cycleStart = now;
      elapsed = 0;
    }

    unsigned long onDur = (unsigned long)(duty * cycleDur);
    bool shouldBeOn = (duty > 0.01) && (elapsed < onDur);

    if (shouldBeOn != ghRun[i].active) {
      if (shouldBeOn) claimRelay(ch + 1, OWN_GH);
      else            releaseRelay(ch + 1, OWN_GH);
      ghRun[i].active = shouldBeOn;
      Serial.printf("[GH] rule%d CH%d %s (%.1fC duty=%.0f%%)\n",
                    i + 1, ch + 1, shouldBeOn ? "ON" : "OFF", temp, duty * 100);
    }
  }
}

// ============================================================
// Solar Irrigation Control — Config & Logic
// ============================================================
void loadIrrigationConfig() {
  for (int i = 0; i < IRRI_SLOTS; i++) {
    irriCtrl[i].enabled        = false;
    irriCtrl[i].relay_ch       = -1;
    irriCtrl[i].threshold_mj   = 0.5;
    irriCtrl[i].duration_sec   = 120;
    irriCtrl[i].min_wm2        = 50.0;
    irriCtrl[i].drain_stop_sec = 0;
    irriCtrl[i].mode             = 0;
    irriCtrl[i].duty_cycle_sec   = 60;
    irriCtrl[i].duty_init        = 0.8;
    irriCtrl[i].duty_min         = 0.1;
    irriCtrl[i].duty_max         = 1.0;
    irriCtrl[i].duty_step        = 0.05;
    irriCtrl[i].drain_target_lo  = 0.15;
    irriCtrl[i].drain_target_hi  = 0.30;
    irriCtrl[i].flow_di_ch       = 0;
    irriCtrl[i].flow_ml_per_pulse = 0.45;
    irriCtrl[i].drain_ml_per_tip  = 3.6;
    memset(&irriRun[i], 0, sizeof(IrriRuntime));
  }
  if (!LittleFS.exists("/irri_ctrl.json")) return;
  File f = LittleFS.open("/irri_ctrl.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  JsonArray arr = doc["rules"].as<JsonArray>();
  int idx = 0;
  for (JsonObject r : arr) {
    if (idx >= IRRI_SLOTS) break;
    irriCtrl[idx].enabled        = r["enabled"]          | false;
    irriCtrl[idx].relay_ch       = r["relay_ch"]          | -1;
    irriCtrl[idx].threshold_mj   = r["threshold_mj"]      | 0.5;
    irriCtrl[idx].duration_sec   = r["duration_sec"]      | 120;
    irriCtrl[idx].min_wm2        = r["min_wm2"]           | 50.0;
    irriCtrl[idx].drain_stop_sec = r["drain_stop_sec"]    | 0;
    irriCtrl[idx].mode             = r["mode"]             | 0;
    irriCtrl[idx].duty_cycle_sec   = r["duty_cycle_sec"]   | 60;
    irriCtrl[idx].duty_init        = r["duty_init"]        | 0.8;
    irriCtrl[idx].duty_min         = r["duty_min"]         | 0.1;
    irriCtrl[idx].duty_max         = r["duty_max"]         | 1.0;
    irriCtrl[idx].duty_step        = r["duty_step"]        | 0.05;
    irriCtrl[idx].drain_target_lo  = r["drain_target_lo"]  | 0.15;
    irriCtrl[idx].drain_target_hi  = r["drain_target_hi"]  | 0.30;
    irriCtrl[idx].flow_di_ch       = r["flow_di_ch"]       | 0;
    irriCtrl[idx].flow_ml_per_pulse = r["flow_ml_pulse"]   | 0.45;
    irriCtrl[idx].drain_ml_per_tip  = r["drain_ml_tip"]    | 3.6;
    idx++;
  }
  Serial.printf("Irrigation: %d rules loaded\n", idx);
}

void saveIrrigationConfig() {
  JsonDocument doc;
  JsonArray arr = doc["rules"].to<JsonArray>();
  for (int i = 0; i < IRRI_SLOTS; i++) {
    JsonObject r = arr.add<JsonObject>();
    r["enabled"]        = irriCtrl[i].enabled;
    r["relay_ch"]       = irriCtrl[i].relay_ch;
    r["threshold_mj"]   = irriCtrl[i].threshold_mj;
    r["duration_sec"]   = irriCtrl[i].duration_sec;
    r["min_wm2"]        = irriCtrl[i].min_wm2;
    r["drain_stop_sec"] = irriCtrl[i].drain_stop_sec;
    r["mode"]             = irriCtrl[i].mode;
    r["duty_cycle_sec"]   = irriCtrl[i].duty_cycle_sec;
    r["duty_init"]        = irriCtrl[i].duty_init;
    r["duty_min"]         = irriCtrl[i].duty_min;
    r["duty_max"]         = irriCtrl[i].duty_max;
    r["duty_step"]        = irriCtrl[i].duty_step;
    r["drain_target_lo"]  = irriCtrl[i].drain_target_lo;
    r["drain_target_hi"]  = irriCtrl[i].drain_target_hi;
    r["flow_di_ch"]       = irriCtrl[i].flow_di_ch;
    r["flow_ml_pulse"]    = irriCtrl[i].flow_ml_per_pulse;
    r["drain_ml_tip"]     = irriCtrl[i].drain_ml_per_tip;
  }
  File f = LittleFS.open("/irri_ctrl.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
  Serial.println("Irrigation config saved");
}

// 日射積算 (mode 0/1 共通): 閾値到達で灌水セッション開始
void irrigationAccumulate(int i, int ch, unsigned long now) {
  if (irriRun[i].last_sample == 0) {
    irriRun[i].last_sample = now;
    return;
  }
  unsigned long dt_ms = now - irriRun[i].last_sample;
  if (dt_ms < 5000) return;
  irriRun[i].last_sample = now;

  if (g_solar_wm2 >= irriCtrl[i].min_wm2) {
    float dt_sec = dt_ms / 1000.0;
    irriRun[i].accum_mj += (g_solar_wm2 * dt_sec) / 1000000.0;
  }

  if (irriRun[i].accum_mj >= irriCtrl[i].threshold_mj) {
    claimRelay(ch + 1, OWN_IRRI);
    irriRun[i].irrigating = true;
    irriRun[i].irri_start = now;
    irriRun[i].today_count++;

    if (irriCtrl[i].mode == 1) {
      // デューティモード: サイクル初期化
      irriRun[i].duty = irriCtrl[i].duty_init;
      irriRun[i].duty_cycle_start = now;
      noInterrupts();
      irriRun[i].snap_flow_pulses = diPulseCount[irriCtrl[i].flow_di_ch & 1];
      interrupts();
      irriRun[i].snap_drain_tips = sen0575_rawTips;
      irriRun[i].cycle_flow_pulses = 0;
      irriRun[i].cycle_drain_tips  = 0;
      Serial.printf("[IRRI] rule%d CH%d DUTY START (accum=%.3f MJ, duty=%.0f%%, #%d)\n",
                    i + 1, ch + 1, irriRun[i].accum_mj,
                    irriRun[i].duty * 100, irriRun[i].today_count);
    } else {
      Serial.printf("[IRRI] rule%d CH%d ON (accum=%.3f MJ >= %.3f, #%d)\n",
                    i + 1, ch + 1, irriRun[i].accum_mj,
                    irriCtrl[i].threshold_mj, irriRun[i].today_count);
    }
  }
}

// Mode 0: タイマー灌水 (従来)
void irrigationMode0(int i, int ch, unsigned long now) {
  bool timeUp = (now - irriRun[i].irri_start) >= (unsigned long)irriCtrl[i].duration_sec * 1000UL;

  bool drainStop = false;
  if (sen0575_detected && irriCtrl[i].drain_stop_sec > 0) {
    if (sen0575_rawTips > irriRun[i].drain_prev_tips) {
      if (irriRun[i].drain_active_since == 0) {
        irriRun[i].drain_active_since = now;
      } else if ((now - irriRun[i].drain_active_since) >= (unsigned long)irriCtrl[i].drain_stop_sec * 1000UL) {
        drainStop = true;
      }
      irriRun[i].drain_prev_tips = sen0575_rawTips;
    } else {
      irriRun[i].drain_active_since = 0;
    }
  }

  if (timeUp || drainStop) {
    releaseRelay(ch + 1, OWN_IRRI);
    irriRun[i].irrigating = false;
    Serial.printf("[IRRI] rule%d CH%d OFF (%s, accum=%.3f MJ, count=%d)\n",
                  i + 1, ch + 1, drainStop ? "drain_stop" : "done",
                  irriRun[i].accum_mj, irriRun[i].today_count);
    irriRun[i].accum_mj = 0.0;
    irriRun[i].drain_active_since = 0;
  }
}

// Mode 1: 排水率デューティ制御
void irrigationMode1(int i, int ch, unsigned long now) {
  unsigned long cycleDur = (unsigned long)irriCtrl[i].duty_cycle_sec * 1000UL;
  if (cycleDur == 0) cycleDur = 60000;
  unsigned long elapsed = now - irriRun[i].duty_cycle_start;

  // サイクル内: duty比でON/OFF
  unsigned long onDur = (unsigned long)(irriRun[i].duty * cycleDur);
  bool shouldOn = (elapsed < onDur);

  if (shouldOn) claimRelay(ch + 1, OWN_IRRI);
  else          releaseRelay(ch + 1, OWN_IRRI);

  // サイクル末: 排水率計算 → duty調整 → 次サイクル
  if (elapsed >= cycleDur) {
    // パルスカウント取得
    noInterrupts();
    uint32_t curFlow = diPulseCount[irriCtrl[i].flow_di_ch & 1];
    interrupts();
    uint32_t curDrain = sen0575_rawTips;

    irriRun[i].cycle_flow_pulses = curFlow - irriRun[i].snap_flow_pulses;
    irriRun[i].cycle_drain_tips  = curDrain - irriRun[i].snap_drain_tips;

    // 排水率 (mL比)
    float flowMl  = irriRun[i].cycle_flow_pulses * irriCtrl[i].flow_ml_per_pulse;
    float drainMl = irriRun[i].cycle_drain_tips  * irriCtrl[i].drain_ml_per_tip;
    float drainRate = (flowMl > 0.1) ? (drainMl / flowMl) : 0.0;
    irriRun[i].last_drain_rate = drainRate;

    // duty調整
    float prevDuty = irriRun[i].duty;
    if (drainRate < irriCtrl[i].drain_target_lo) {
      irriRun[i].duty += irriCtrl[i].duty_step;  // 乾き気味 → duty上げ
    } else if (drainRate > irriCtrl[i].drain_target_hi) {
      irriRun[i].duty -= irriCtrl[i].duty_step;  // 過湿 → duty下げ
    }
    // clamp
    if (irriRun[i].duty < irriCtrl[i].duty_min) irriRun[i].duty = irriCtrl[i].duty_min;
    if (irriRun[i].duty > irriCtrl[i].duty_max) irriRun[i].duty = irriCtrl[i].duty_max;

    Serial.printf("[IRRI] rule%d cycle: flow=%upls(%.1fmL) drain=%utips(%.1fmL) rate=%.1f%% duty=%.0f%%->%.0f%%\n",
                  i + 1, irriRun[i].cycle_flow_pulses, flowMl,
                  irriRun[i].cycle_drain_tips, drainMl,
                  drainRate * 100, prevDuty * 100, irriRun[i].duty * 100);

    // 排水率が上限超え かつ duty最小 → セッション終了
    if (drainRate >= irriCtrl[i].drain_target_hi && irriRun[i].duty <= irriCtrl[i].duty_min) {
      releaseRelay(ch + 1, OWN_IRRI);
      irriRun[i].irrigating = false;
      irriRun[i].accum_mj = 0.0;
      Serial.printf("[IRRI] rule%d CH%d DUTY END (drain saturated, count=%d)\n",
                    i + 1, ch + 1, irriRun[i].today_count);
      return;
    }

    // 次サイクル開始
    irriRun[i].duty_cycle_start = now;
    irriRun[i].snap_flow_pulses = curFlow;
    irriRun[i].snap_drain_tips  = curDrain;
  }
}

void irrigationControl(unsigned long now) {
  if (!ads1110_detected && isnan(g_solar_wm2)) return;

  for (int i = 0; i < IRRI_SLOTS; i++) {
    if (!irriCtrl[i].enabled || irriCtrl[i].relay_ch < 0) continue;
    int ch = irriCtrl[i].relay_ch;

    if (irriRun[i].irrigating) {
      if (irriCtrl[i].mode == 1)
        irrigationMode1(i, ch, now);
      else
        irrigationMode0(i, ch, now);
      continue;
    }

    irrigationAccumulate(i, ch, now);
  }
}

// ============================================================
// CO2 Guard — Config & Logic
// ============================================================
void loadCO2GuardConfig() {
  co2Guard.enabled = false;
  co2Guard.threshold_ppm = 200;
  for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
    co2Guard.actions[i].relay_ch = -1;
    co2Guard.actions[i].duration_sec = 60;
  }
  if (!LittleFS.exists("/co2_guard.json")) return;
  File f = LittleFS.open("/co2_guard.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  co2Guard.enabled = doc["enabled"] | false;
  co2Guard.threshold_ppm = doc["threshold_ppm"] | 200;
  JsonArray arr = doc["actions"].as<JsonArray>();
  int idx = 0;
  for (JsonObject a : arr) {
    if (idx >= CO2_GUARD_ACTIONS) break;
    co2Guard.actions[idx].relay_ch = a["relay_ch"] | -1;
    co2Guard.actions[idx].duration_sec = a["duration_sec"] | 60;
    idx++;
  }
  Serial.printf("CO2Guard: threshold=%dppm, %d actions\n", co2Guard.threshold_ppm, idx);
}

void saveCO2GuardConfig() {
  JsonDocument doc;
  doc["enabled"] = co2Guard.enabled;
  doc["threshold_ppm"] = co2Guard.threshold_ppm;
  JsonArray arr = doc["actions"].to<JsonArray>();
  for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
    JsonObject a = arr.add<JsonObject>();
    a["relay_ch"] = co2Guard.actions[i].relay_ch;
    a["duration_sec"] = co2Guard.actions[i].duration_sec;
  }
  File f = LittleFS.open("/co2_guard.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void co2GuardControl(unsigned long now) {
  if (!co2Guard.enabled || !scd41_detected || g_scd41_co2 == 0) return;

  bool belowThreshold = (g_scd41_co2 <= co2Guard.threshold_ppm);

  if (belowThreshold && !co2Run.active) {
    // 発動: 全アクションのリレーをON
    co2Run.active = true;
    Serial.printf("[CO2] Guard triggered: %dppm <= %dppm\n",
                  g_scd41_co2, co2Guard.threshold_ppm);
    for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
      if (co2Guard.actions[i].relay_ch >= 0 && co2Guard.actions[i].duration_sec > 0) {
        claimRelay(co2Guard.actions[i].relay_ch + 1, OWN_CO2);
        co2Run.action_start[i] = now;
        co2Run.action_on[i] = true;
        Serial.printf("[CO2] CH%d ON for %ds\n",
                      co2Guard.actions[i].relay_ch + 1, co2Guard.actions[i].duration_sec);
      }
    }
  }

  if (co2Run.active) {
    // 各アクションを個別にタイマー管理
    bool anyOn = false;
    for (int i = 0; i < CO2_GUARD_ACTIONS; i++) {
      if (!co2Run.action_on[i]) continue;
      if ((now - co2Run.action_start[i]) >= (unsigned long)co2Guard.actions[i].duration_sec * 1000UL) {
        releaseRelay(co2Guard.actions[i].relay_ch + 1, OWN_CO2);
        co2Run.action_on[i] = false;
        Serial.printf("[CO2] CH%d OFF (timer done)\n", co2Guard.actions[i].relay_ch + 1);
      } else {
        anyOn = true;
      }
    }
    if (!anyOn) {
      co2Run.active = false;
      Serial.println("[CO2] Guard complete");
    }
  }
}

// ============================================================
// NOAA Sunrise Calculation (solareqns.PDF)
// ============================================================
int calcSunriseMinLocal(int dayOfYear, float lat, float lon, int tz_h) {
  // Fractional year (γ) in radians
  float gamma = 2.0 * PI / 365.0 * (dayOfYear - 1);

  // Equation of time (minutes)
  float eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma)
                 - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));

  // Solar declination (radians)
  float decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
               - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
               - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);

  // Hour angle for sunrise (degrees)
  float latRad = lat * PI / 180.0;
  float zenith = 90.833 * PI / 180.0;  // atmospheric refraction correction
  float cosHA = (cos(zenith) / (cos(latRad) * cos(decl))) - tan(latRad) * tan(decl);

  if (cosHA > 1.0) return -1;   // no sunrise (polar night)
  if (cosHA < -1.0) return -1;  // no sunset (midnight sun)

  float ha = acos(cosHA) * 180.0 / PI;  // degrees, positive = sunrise

  // Sunrise UTC in minutes
  float sunriseUTC = 720.0 - 4.0 * (lon + ha) - eqtime;

  // Convert to local time
  int sunriseLocal = (int)(sunriseUTC + tz_h * 60.0);
  if (sunriseLocal < 0) sunriseLocal += 1440;
  if (sunriseLocal >= 1440) sunriseLocal -= 1440;
  return sunriseLocal;
}

// ============================================================
// Dew Prevention — Config & Logic
// ============================================================
void loadDewConfig() {
  dewCtrl.enabled           = false;
  dewCtrl.latitude          = 43.0;   // 北海道デフォルト
  dewCtrl.longitude         = 141.3;
  dewCtrl.timezone_h        = 9;      // JST
  dewCtrl.before_sunrise_min = 30;
  dewCtrl.after_sunrise_min  = 60;
  dewCtrl.fan_relay_ch      = -1;
  dewCtrl.heater_relay_ch   = -1;
  dewCtrl.apt_slot          = -1;
  dewCtrl.apt_open_pct      = 10.0;
  dewCtrl.apt_close_pct     = 0.0;
  dewCtrl.apt_min_temp      = 5.0;

  if (!LittleFS.exists("/dew_ctrl.json")) return;
  File f = LittleFS.open("/dew_ctrl.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  dewCtrl.enabled           = doc["enabled"]    | false;
  dewCtrl.latitude          = doc["lat"]        | 43.0;
  dewCtrl.longitude         = doc["lon"]        | 141.3;
  dewCtrl.timezone_h        = doc["tz"]         | 9;
  dewCtrl.before_sunrise_min = doc["before_min"] | 30;
  dewCtrl.after_sunrise_min  = doc["after_min"]  | 60;
  dewCtrl.fan_relay_ch      = doc["fan_ch"]     | -1;
  dewCtrl.heater_relay_ch   = doc["heater_ch"]  | -1;
  dewCtrl.apt_slot          = doc["apt_slot"]   | -1;
  dewCtrl.apt_open_pct      = doc["apt_open"]   | 10.0;
  dewCtrl.apt_close_pct     = doc["apt_close"]  | 0.0;
  dewCtrl.apt_min_temp      = doc["apt_min_t"]  | 5.0;
  Serial.printf("Dew: lat=%.2f lon=%.2f tz=%d before=%d after=%d\n",
                dewCtrl.latitude, dewCtrl.longitude, dewCtrl.timezone_h,
                dewCtrl.before_sunrise_min, dewCtrl.after_sunrise_min);
}

void saveDewConfig() {
  JsonDocument doc;
  doc["enabled"]    = dewCtrl.enabled;
  doc["lat"]        = dewCtrl.latitude;
  doc["lon"]        = dewCtrl.longitude;
  doc["tz"]         = dewCtrl.timezone_h;
  doc["before_min"] = dewCtrl.before_sunrise_min;
  doc["after_min"]  = dewCtrl.after_sunrise_min;
  doc["fan_ch"]     = dewCtrl.fan_relay_ch;
  doc["heater_ch"]  = dewCtrl.heater_relay_ch;
  doc["apt_slot"]   = dewCtrl.apt_slot;
  doc["apt_open"]   = dewCtrl.apt_open_pct;
  doc["apt_close"]  = dewCtrl.apt_close_pct;
  doc["apt_min_t"]  = dewCtrl.apt_min_temp;
  File f = LittleFS.open("/dew_ctrl.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
  Serial.println("Dew config saved");
}

void dewPreventionControl(unsigned long now) {
  if (!dewCtrl.enabled) return;
  unsigned long epoch = getCurrentEpoch();
  if (epoch == 0) return;  // NTP未同期

  // ローカル時刻 (分)
  int localSec = (epoch + dewCtrl.timezone_h * 3600L) % 86400L;
  int localMin = localSec / 60;

  // 日の出時刻を1日1回再計算
  int dayOfYear = ((epoch + dewCtrl.timezone_h * 3600L) / 86400L) % 365 + 1;
  if (dayOfYear != dewRun.last_calc_day) {
    dewRun.sunrise_min = calcSunriseMinLocal(dayOfYear, dewCtrl.latitude,
                                              dewCtrl.longitude, dewCtrl.timezone_h);
    dewRun.last_calc_day = dayOfYear;
    Serial.printf("[DEW] day=%d sunrise=%d:%02d local\n",
                  dayOfYear, dewRun.sunrise_min / 60, dewRun.sunrise_min % 60);
  }

  if (dewRun.sunrise_min < 0) return;  // 極夜

  int startMin = dewRun.sunrise_min - dewCtrl.before_sunrise_min;
  int endMin   = dewRun.sunrise_min + dewCtrl.after_sunrise_min;
  if (startMin < 0) startMin += 1440;

  // 時間帯内か判定
  bool inWindow;
  if (startMin < endMin) {
    inWindow = (localMin >= startMin && localMin < endMin);
  } else {
    // 日をまたぐ場合 (e.g. 23:30-05:30)
    inWindow = (localMin >= startMin || localMin < endMin);
  }

  if (inWindow && !dewRun.active) {
    dewRun.active = true;
    if (dewCtrl.fan_relay_ch >= 0 && dewCtrl.fan_relay_ch <= 7)
      claimRelay(dewCtrl.fan_relay_ch + 1, OWN_DEW);
    if (dewCtrl.heater_relay_ch >= 0 && dewCtrl.heater_relay_ch <= 7)
      claimRelay(dewCtrl.heater_relay_ch + 1, OWN_DEW);
    // 側窓: 低温ガード確認後に開放
    if (dewCtrl.apt_slot >= 0 && dewCtrl.apt_slot < APT_SLOTS) {
      float temp = !isnan(g_sht40_temp) ? g_sht40_temp : g_ds18b20_temp;
      if (!isnan(temp) && temp >= dewCtrl.apt_min_temp) {
        setTargetAperture(dewCtrl.apt_slot, dewCtrl.apt_open_pct);
        dewRun.apt_opened = true;
      }
    }
    Serial.printf("[DEW] ON — sunrise=%d:%02d now=%d:%02d\n",
                  dewRun.sunrise_min / 60, dewRun.sunrise_min % 60,
                  localMin / 60, localMin % 60);
  }

  // 稼働中: 側窓の低温ガード（温度変化に追従）
  if (inWindow && dewRun.active && dewCtrl.apt_slot >= 0 && dewCtrl.apt_slot < APT_SLOTS) {
    float temp = !isnan(g_sht40_temp) ? g_sht40_temp : g_ds18b20_temp;
    if (!isnan(temp)) {
      if (temp < dewCtrl.apt_min_temp && dewRun.apt_opened) {
        setTargetAperture(dewCtrl.apt_slot, dewCtrl.apt_close_pct);
        dewRun.apt_opened = false;
        Serial.printf("[DEW] Apt close — temp %.1fC < min %.1fC\n", temp, dewCtrl.apt_min_temp);
      } else if (temp >= dewCtrl.apt_min_temp && !dewRun.apt_opened) {
        setTargetAperture(dewCtrl.apt_slot, dewCtrl.apt_open_pct);
        dewRun.apt_opened = true;
        Serial.printf("[DEW] Apt open — temp %.1fC >= min %.1fC\n", temp, dewCtrl.apt_min_temp);
      }
    }
  }

  if (!inWindow && dewRun.active) {
    dewRun.active = false;
    if (dewCtrl.fan_relay_ch >= 0 && dewCtrl.fan_relay_ch <= 7)
      releaseRelay(dewCtrl.fan_relay_ch + 1, OWN_DEW);
    if (dewCtrl.heater_relay_ch >= 0 && dewCtrl.heater_relay_ch <= 7)
      releaseRelay(dewCtrl.heater_relay_ch + 1, OWN_DEW);
    // 側窓: 終了時開度へ
    if (dewCtrl.apt_slot >= 0 && dewCtrl.apt_slot < APT_SLOTS && dewRun.apt_opened) {
      setTargetAperture(dewCtrl.apt_slot, dewCtrl.apt_close_pct);
      dewRun.apt_opened = false;
    }
    Serial.printf("[DEW] OFF — window ended\n");
  }
}

// ============================================================
// Temp Rate Guard — Config & Logic
// ============================================================
void loadRateGuardConfig() {
  rateGuard.enabled        = false;
  rateGuard.rate_threshold = 2.0;   // 2℃/分
  rateGuard.fan_relay_ch   = -1;
  rateGuard.sensor_src     = 0;     // SHT40
  rateGuard.hold_sec       = 120;   // 2分保持

  if (!LittleFS.exists("/rate_ctrl.json")) return;
  File f = LittleFS.open("/rate_ctrl.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  rateGuard.enabled        = doc["enabled"]   | false;
  rateGuard.rate_threshold = doc["threshold"]  | 2.0;
  rateGuard.fan_relay_ch   = doc["fan_ch"]     | -1;
  rateGuard.sensor_src     = doc["sensor_src"] | 0;
  rateGuard.hold_sec       = doc["hold_sec"]   | 120;
  Serial.printf("RateGuard: threshold=%.1f℃/min fan=CH%d hold=%ds\n",
                rateGuard.rate_threshold, rateGuard.fan_relay_ch + 1, rateGuard.hold_sec);
}

void saveRateGuardConfig() {
  JsonDocument doc;
  doc["enabled"]    = rateGuard.enabled;
  doc["threshold"]  = rateGuard.rate_threshold;
  doc["fan_ch"]     = rateGuard.fan_relay_ch;
  doc["sensor_src"] = rateGuard.sensor_src;
  doc["hold_sec"]   = rateGuard.hold_sec;
  File f = LittleFS.open("/rate_ctrl.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
  Serial.println("RateGuard config saved");
}

void tempRateGuardControl(unsigned long now) {
  if (!rateGuard.enabled || rateGuard.fan_relay_ch < 0) return;

  float temp = (rateGuard.sensor_src == 0) ? g_sht40_temp : g_ds18b20_temp;
  if (isnan(temp)) return;

  // 初回
  if (isnan(rateRun.prev_temp) || rateRun.prev_time == 0) {
    rateRun.prev_temp = temp;
    rateRun.prev_time = now;
    return;
  }

  // 10秒以上経過で変化率計算
  unsigned long dt_ms = now - rateRun.prev_time;
  if (dt_ms < 10000) return;

  float dt_min = dt_ms / 60000.0;
  rateRun.current_rate = (temp - rateRun.prev_temp) / dt_min;
  rateRun.prev_temp = temp;
  rateRun.prev_time = now;

  // 急上昇検知 → 換気扇ON
  if (!rateRun.active && rateRun.current_rate >= rateGuard.rate_threshold) {
    rateRun.active = true;
    rateRun.active_since = now;
    claimRelay(rateGuard.fan_relay_ch + 1, OWN_RATE);
    Serial.printf("[RATE] ON — %.2f℃/min >= %.1f threshold\n",
                  rateRun.current_rate, rateGuard.rate_threshold);
  }

  // 保持時間経過 かつ 変化率が閾値未満 → OFF
  if (rateRun.active) {
    bool holdDone = (now - rateRun.active_since) >= (unsigned long)rateGuard.hold_sec * 1000UL;
    bool rateLow  = rateRun.current_rate < rateGuard.rate_threshold;
    if (holdDone && rateLow) {
      rateRun.active = false;
      releaseRelay(rateGuard.fan_relay_ch + 1, OWN_RATE);
      Serial.printf("[RATE] OFF — rate=%.2f℃/min, hold done\n", rateRun.current_rate);
    }
  }
}

// ============================================================
// Configuration (LittleFS /config.json)
// ============================================================
void loadConfig() {
  String ipStr = DEFAULT_IP;

  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    if (file) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, file);
      file.close();

      if (!err) {
        Serial.println("Config loaded from /config.json");
        nodeId       = (const char*)(doc["node_id"]        | DEFAULT_NODE_ID);
        nodeName     = (const char*)(doc["node_name"]      | DEFAULT_NODE_NAME);
        mdnsHostname = (const char*)(doc["mdns_hostname"]  | DEFAULT_MDNS_HOSTNAME);
        ipStr        = (const char*)(doc["ip"]             | DEFAULT_IP);
        rs485Baud    = doc["rs485_baud"] | RS485_DEFAULT_BAUD;
        mdns_enabled = doc["mdns_enabled"] | DEFAULT_MDNS_ENABLED;
        g_language   = doc["language"] | 0;

        if (ipStr.length() > 0) {
          IPAddress ip, subnet, gw, dns;
          ip.fromString(ipStr);
          subnet.fromString((const char*)(doc["subnet"] | DEFAULT_SUBNET));
          gw.fromString((const char*)(doc["gateway"]    | DEFAULT_GATEWAY));
          dns.fromString((const char*)(doc["dns"]       | DEFAULT_DNS));
          eth.config(ip, gw, subnet, dns);
          Serial.printf("Static IP: %s\n", ipStr.c_str());
        }
        return;
      }
      Serial.printf("Config parse error: %s\n", err.c_str());
    }
  }

  Serial.println("Using default configuration (DHCP)");
  nodeId       = DEFAULT_NODE_ID;
  nodeName     = DEFAULT_NODE_NAME;
  mdnsHostname = DEFAULT_MDNS_HOSTNAME;
}

void saveLangToConfig() {
  JsonDocument doc;
  if (LittleFS.exists("/config.json")) {
    File rf = LittleFS.open("/config.json", "r");
    if (rf) { deserializeJson(doc, rf); rf.close(); }
  }
  doc["language"] = g_language;
  File f = LittleFS.open("/config.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

// ============================================================
// Ethernet
// ============================================================
void initEthernet() {
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(100);
  digitalWrite(W5500_RST, HIGH);
  delay(500);

  SPI.setSCK(W5500_SCK);
  SPI.setTX(W5500_MOSI);
  SPI.setRX(W5500_MISO);
  SPI.begin();

  // Construct eth object AFTER SPI pins are configured
  // GPIO33-36 = SPI0 on RP2350B pinmux
  ethPtr = new Wiznet5500lwIP(W5500_CS, SPI, W5500_INT);

  lwipPollingPeriod(5);
  eth.begin();

  Serial.println("ETH: waiting for link...");
  unsigned long start = millis();
  while (!eth.connected()) {
    if (millis() - start > (unsigned long)ETH_CONNECT_TIMEOUT * 1000UL) {
      Serial.println("[ERR] DHCP timeout, retrying...");
      rebootWithReason("eth_timeout");
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  {
    uint8_t mac[6];
    eth.macAddress(mac);
    char mac_str[20];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("[BOOT] mac: %s\n", mac_str);
  }
  Serial.printf("[NET] ip: %s gw: %s mask: %s\n",
                eth.localIP().toString().c_str(),
                eth.gatewayIP().toString().c_str(),
                eth.subnetMask().toString().c_str());
}

void rebootWithReason(const char* reason) {
  Serial.printf("Rebooting: %s\n", reason);
  File file = LittleFS.open("/reboot_reason.txt", "w");
  if (file) { file.print(reason); file.close(); }
  delay(500);
  watchdog_reboot(0, 0, 0);
  while (true) {}
}


// ============================================================
// WebUI (split into header files)
// ============================================================
#include "web_common.h"
#include "web_i18n.h"
#include "web_dashboard.h"
#include "web_api.h"
#include "web_config.h"
#include "web_mqtt.h"
#include "web_greenhouse.h"
#include "web_irrigation.h"
#include "web_protection.h"
#include "web_ota.h"
// ============================================================
// Web Router
// ============================================================
void handleWebClient() {
  WiFiClient client = webServer.accept();
  if (!client) return;

  client.setTimeout(500);
  unsigned long t = millis();

  while (!client.available() && (millis() - t) < 500) delay(1);
  if (!client.available()) { client.stop(); return; }

  String reqLine = client.readStringUntil('\n');
  reqLine.trim();

  int sp1 = reqLine.indexOf(' ');
  int sp2 = (sp1 >= 0) ? reqLine.indexOf(' ', sp1 + 1) : -1;
  if (sp1 < 0 || sp2 < 0) { client.stop(); return; }

  String method = reqLine.substring(0, sp1);
  String path   = reqLine.substring(sp1 + 1, sp2);

  int contentLength = 0;
  while ((millis() - t) < 2000) {
    String hdr = client.readStringUntil('\n');
    hdr.trim();
    if (hdr.length() == 0) break;
    if (hdr.startsWith("Content-Length:")) {
      contentLength = hdr.substring(15).toInt();
    }
  }

  // OTA: stream directly, do NOT buffer body into String
  if (method == "POST" && path == "/api/ota") {
    handleOTAUpload(client, contentLength);
    delay(1);
    client.stop();
    return;
  }

  String body;
  if (contentLength > 0) {
    unsigned long bt = millis();
    while ((int)body.length() < contentLength && (millis() - bt) < 1000) {
      if (client.available()) body += (char)client.read();
    }
  }

  if (method == "GET" && (path == "/" || path == "/index.html")) {
    sendDashboard(client);
  } else if (method == "GET" && path == "/api/state") {
    sendAPIState(client);
  } else if (method == "GET" && path.startsWith("/api/language")) {
    int qi = path.indexOf("lang=");
    if (qi >= 0) {
      String lang = path.substring(qi + 5);
      int amp = lang.indexOf('&'); if (amp >= 0) lang = lang.substring(0, amp);
      g_language = (lang == "jp") ? 1 : 0;
      saveLangToConfig();
    }
    client.println("HTTP/1.1 302 Found");
    client.println("Location: /");
    client.println("Connection: close");
    client.println();
  } else if (method == "GET" && path == "/api/config") {
    sendAPIConfig(client);
  } else if (method == "GET" && path == "/config") {
    sendConfigPage(client);
  } else if (method == "GET" && path == "/mqtt") {
    sendMqttConfigPage(client);
  } else if (method == "GET" && path == "/ota") {
    sendOTAPage(client);
  } else if (method == "GET" && path == "/greenhouse") {
    sendGreenhousePage(client);
  } else if (method == "POST" && path == "/api/greenhouse") {
    handleGreenhousePost(client, body);
  } else if (method == "POST" && path == "/api/aperture") {
    handleAperturePost(client, body);
  } else if (method == "GET" && path == "/irrigation") {
    sendIrrigationPage(client);
  } else if (method == "POST" && path == "/api/irrigation") {
    handleIrrigationPost(client, body);
  } else if (method == "GET" && path == "/protection") {
    sendProtectionPage(client);
  } else if (method == "POST" && path == "/api/protection") {
    handleProtectionPost(client, body);
  } else if (method == "POST" && path == "/api/config") {
    handleConfigPost(client, body);
  } else if (method == "POST" && path == "/api/mqtt") {
    handleMqttConfigPost(client, body);
  } else if (method == "POST" && path.startsWith("/api/relay/")) {
    int ch = path.substring(11).toInt();
    handleRelayPost(client, ch, body);
  } else {
    client.println("HTTP/1.1 404 Not Found\r\nConnection: close\r\n");
  }

  delay(1);
  client.stop();
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  { unsigned long t = millis(); while (!Serial && millis() - t < 3000) delay(10); }  // USB-CDC ready (3s timeout)
  Serial.printf("=== %s v%s ===\n", FW_NAME, FW_VERSION);
  Serial.println("Board: RP2350-POE-ETH-8DI-8RO");
  Serial.println("Protocol: MQTT over TCP (PubSubClient)");

  initRelaysOff();

  for (int i = 0; i < 8; i++) {
    pinMode(DI_PINS[i], INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(DI_PINS[0]), diPulseISR1, FALLING);
  attachInterrupt(digitalPinToInterrupt(DI_PINS[1]), diPulseISR2, FALLING);
  for (int i = 2; i < 8; i++) {
    attachInterrupt(digitalPinToInterrupt(DI_PINS[i]), diISR, CHANGE);
  }

  Wire1.setSDA(I2C_SDA);
  Wire1.setSCL(I2C_SCL);
  Wire1.begin();
  delay(200);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS: formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("WARNING: LittleFS unavailable");
    }
  }

  loadConfig();
  loadRelayChConfig();
  loadMqttConfig();
  loadGreenhouseConfig();
  loadApertureConfig();
  for (int i = 0; i < APT_SLOTS; i++) {
    if (aptCtrl[i].enabled) aptRun[i].initializing = true;
  }
  loadIrrigationConfig();
  loadCO2GuardConfig();
  loadDewConfig();
  loadRateGuardConfig();

  Serial.printf("Node=%s\n", nodeId.c_str());
  Serial.printf("[BOOT] hostname: %s.local\n", mdnsHostname.c_str());

  initEthernet();
  syncNTP();
  scanI2CSensors();

  // DS18B20 (OneWire on GPIO3)
  ds18b20.begin();
  if (ds18b20.getDeviceCount() > 0) {
    ds18b20_detected = true;
    ds18b20.setResolution(12);
    Serial.printf("DS18B20: %d device(s) on GPIO%d\n", ds18b20.getDeviceCount(), ONEWIRE_PIN);
  } else {
    Serial.println("DS18B20: not found (continuing)");
  }

  // RGB LED
  rgbLED.begin();
  rgbLED.setBrightness(30);  // 控えめ
  rgbLED.setPixelColor(0, rgbLED.Color(0, 0, 50));  // 起動中=青
  rgbLED.show();

  readSensors();
  initRS485();

  initMqtt();

  // mDNS
  if (mdns_enabled) {
    if (MDNS.begin(mdnsHostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS: %s.local\n", mdnsHostname.c_str());
    }
  }

  webServer.begin();
  Serial.printf("WebUI: http://%s\n", eth.localIP().toString().c_str());

  readDI();

  // Watchdog
  watchdog_enable(HW_WDT_TIMEOUT_MS, true);
  swWdtStart();

  Serial.println("=== Setup complete ===\n");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  loopCount++;

  watchdog_update();
  swWdtFeed();

  // [STATUS] 30秒毎デバッグ出力
  if (millis() - last_status >= 30000UL) {
    Serial.printf("[STATUS] ip:%s up:%lus\n",
                  eth.localIP().toString().c_str(),
                  millis() / 1000);
    last_status = millis();
  }

  if (millis() >= REBOOT_INTERVAL) {
    rebootWithReason("periodic_reboot");
  }

  if (!eth.connected()) {
    rebootWithReason("eth_disconnected");
  }

  if (mdns_enabled) MDNS.update();

  handleWebClient();

  // Duration auto-off (manual timer)
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    if (relayDurationEnd[i] > 0 && now >= relayDurationEnd[i]) {
      releaseRelay(i + 1, OWN_MANUAL);
      relayDurationEnd[i] = 0;
      Serial.printf("CH%d auto-OFF\n", i + 1);
    }
  }

  // MQTT: duration_secタイマー → auto-OFF
  {
    unsigned long now2 = millis();
    for (int i = 0; i < 8; i++) {
      if (mqttDurationEnd[i] > 0 && now2 >= mqttDurationEnd[i]) {
        releaseRelay(i + 1, OWN_MQTT);
        mqttDurationEnd[i] = 0;
        Serial.printf("[MQTT-DUR] CH%d auto-OFF (duration expired)\n", i + 1);
      }
    }
  }

  // MQTT: 接続断でOWN_MQTT全解放
  if (!mqttClient.connected() && mqttOnline) {
    mqttOnline = false;
    for (int i = 0; i < 8; i++) {
      releaseRelay(i + 1, OWN_MQTT);
      mqttDurationEnd[i] = 0;
    }
    Serial.println("[MQTT] disconnected — all MQTT claims released");
  }

  // DI interrupt
  if (diInterruptFlag && (now - diLastDebounce >= DI_DEBOUNCE_MS)) {
    diInterruptFlag = false;
    diLastDebounce  = now;
    if (readDI()) {
      // DI→リレー連動
      for (int i = 0; i < 8; i++) {
        if (relayCh[i].di_link < 0 || relayCh[i].di_link > 7) continue;
        bool di_on = diState[relayCh[i].di_link];
        bool target = relayCh[i].di_invert ? !di_on : di_on;
        if (target) claimRelay(i + 1, OWN_MANUAL);
        else        releaseRelay(i + 1, OWN_MANUAL);
        Serial.printf("[DI-LINK] DI%d=%s → CH%d %s\n",
                      relayCh[i].di_link + 1, di_on ? "ON" : "OFF",
                      i + 1, target ? "ON" : "OFF");
      }
    }
  }

  // Periodic: sensor read + MQTT publish
  static unsigned long lastBroadcast = 0;
  if (now - lastBroadcast >= (unsigned long)mqttCfg.publish_interval * 1000UL) {
    lastBroadcast = now;

    readSensors();
    pollDrainSensor();
    // MQTT solar fallback
    if (!ads1110_detected && !isnan(g_mqtt_solar_wm2)) {
      g_solar_wm2 = g_mqtt_solar_wm2;
    }
    mqttPublishAll();

    Serial.printf("[%d] relay=0x%02X epoch=%lu uptime=%lus\n",
                  loopCount, relayState, getCurrentEpoch(), millis() / 1000);
  }

  // MQTT reconnect + loop
  mqttReconnect();
  if (mqttCfg.enabled) mqttClient.loop();

  // Aperture (side window) control
  apertureControl(now);

  // Greenhouse local control (every loop for duty cycle switching)
  greenhouseControl(now);

  // Solar irrigation control (accumulation + trigger)
  irrigationControl(now);

  // Dew prevention (sunrise-based)
  dewPreventionControl(now);

  // Temperature rate guard
  tempRateGuardControl(now);

  // CO2 guard
  co2GuardControl(now);

  // Relay arbitration: claims → physical output
  resolveAllRelays();

  // NTP re-sync
  static unsigned long lastNtpSync = 0;
  if (now - lastNtpSync >= NTP_SYNC_INTERVAL) {
    lastNtpSync = now;
    syncNTP();
  }

  // Serial command handler — type "status" or "?" to get IP/mDNS/uptime
  static String serialCmd = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialCmd.trim();
      if (serialCmd == "status" || serialCmd == "?" || serialCmd == "ip") {
        Serial.printf("[INFO] hostname: %s.local\n", mdnsHostname.c_str());
        Serial.printf("[INFO] ip: %s gw: %s mask: %s\n",
                      eth.localIP().toString().c_str(),
                      eth.gatewayIP().toString().c_str(),
                      eth.subnetMask().toString().c_str());
        byte mac[6]; eth.macAddress(mac);
        Serial.printf("[INFO] mac: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Serial.printf("[INFO] uptime: %lus relay: 0x%02X\n",
                      millis() / 1000, relayState);
        Serial.printf("[INFO] mdns: %s eth: %s\n",
                      mdns_enabled ? "OK" : "OFF",
                      eth.connected() ? "OK" : "DISC");
      } else if (serialCmd == "help") {
        Serial.println("Commands: status/ip/? help reboot");
      } else if (serialCmd == "reboot") {
        rebootWithReason("serial_cmd");
      } else if (serialCmd.length() > 0) {
        Serial.printf("Unknown: '%s' (type 'help')\n", serialCmd.c_str());
      }
      serialCmd = "";
    } else {
      serialCmd += c;
    }
  }

  // RGB LED status: 緑=正常, 赤=Ethernet断, 黄=リレーON中, 青=起動直後
  static unsigned long lastLedUpdate = 0;
  if (now - lastLedUpdate >= 1000) {
    lastLedUpdate = now;
    if (!eth.connected()) {
      rgbLED.setPixelColor(0, rgbLED.Color(80, 0, 0));    // 赤=Ethernet断
    } else if (relayState > 0) {
      rgbLED.setPixelColor(0, rgbLED.Color(60, 40, 0));   // 黄=リレー稼働中
    } else {
      rgbLED.setPixelColor(0, rgbLED.Color(0, 50, 0));    // 緑=正常待機
    }
    rgbLED.show();
  }

  delay(50);
}
