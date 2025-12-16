#define BLYNK_TEMPLATE_ID "TMPL6iITEgG-M"
#define BLYNK_TEMPLATE_NAME "Smart Coaster"
#define BLYNK_AUTH_TOKEN "c3_2LHfyCsmH5prZCU59rsgvctT_oZb5"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <HX711.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <time.h>
#include <EEPROM.h>

// ================= WiFi Credentials =================
char ssid[] = "好想連上";
char pass[] = "00000000";

// ================= Blynk Timer =================
BlynkTimer timer;

// ================= 資料儲存結構 (EEPROM) =================
struct StorageData {
  float savedEmptyWeight;
  float savedTodayIntake;
  bool  savedEmptySet;
  int   savedDayOfYear;        // 0-365
  int   initializedMarker;     // 用來檢查 EEPROM 是否已初始化
};

StorageData storedData;
const int EEPROM_SIZE  = sizeof(StorageData);
const int MARKER_VALUE = 12345;

// ================= HX711 =================
const int LOADCELL_DOUT_PIN = 8;
const int LOADCELL_SCK_PIN  = 9;
HX711 scale;
float calibration_factor = 407.09f;

// ================= Button =================
const int BUTTON_PIN = 18;   // INPUT_PULLUP
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ===== 長按設定 =====
const unsigned long LONG_PRESS_MS = 3500;
unsigned long pressStartMs = 0;
bool longPressFired = false;
bool emptySet = false;  // 會由 EEPROM 載入

// ================= NeoPixel =================
const int NEOPIXEL_PIN = 10;
const int NUM_PIXELS   = 9;
Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ================= LED blink =================
const unsigned long BLINK_MS = 300;
unsigned long lastBlinkMs = 0;
bool blinkOn = false;

// ================= State =================
enum State { NEED_TARE, READY };
State state = NEED_TARE;

// ================= Drink detect params =================
const float CUP_PRESENT_ON  = 25.0f;
const float CUP_PRESENT_OFF = 10.0f;

const int   READ_SAMPLES = 8;
const float JITTER_DEADBAND = 1.0f;

// 放回穩定期
const unsigned long PLACEMENT_SETTLE_MS = 2000;
const float STABLE_DELTA_G = 2.0f;

// ===== 判斷裝水喝水（穩健版）=====
bool  drinkingInProgress = false;
const float ACTION_MIN_G = 20.0f;        // 絕對門檻（至少超過 20g 才算）
const float ACTION_RATIO = 0.06f;        // 相對門檻（超過「拿起前水量」的 6%）
const float MIN_BASELINE_WATER = 50.0f;  // 拿起前水量 < 50g 不判斷喝水/裝水

float baselineWaterStable = 0.0f;        // 只存「放回穩定後」的水量（可靠基準）

// ================= 第三次量測控制 =================
const int VALID_SAMPLE_INDEX = 3;
bool samplingActive = false;
int  sampleCounter  = 0;

// ================= 今日累積 =================
float todayIntake = 0.0f;  // 由 EEPROM 載入
int   drinkCount  = 0;
int   pickupCount = 0;

// ================= 重量相關 =================
float emptyWeight = 0.0f;  // 由 EEPROM 載入
float lastStableWeight = 0.0f;
float lastWaterWeight = 0.0f;      // 當前杯墊上 water（顯示用）
float weightBeforeLift = 0.0f;     // 拿起前 water（用 baselineWaterStable 鎖定）

// ================= 杯子狀態 =================
bool cupPresent = false;
bool lastCupPresent = false;

// ===== 未放回專用計時 =====
bool cupAbsentTimerActive = false;
unsigned long cupAbsentSinceMs = 0;

// ================= 放回穩定期 =================
bool placementSettling = false;
unsigned long placeStartMs = 0;
float settleSum = 0.0f;
int   settleCount = 0;
float settleLast = 0.0f;

// ================= LED 提醒/慶祝參數 =================
const float LOW_WATER_LIMIT_G = 100.0f;
const float EMPTY_IGNORE_G    = 10.0f;

// 測試參數（你可改回正式）
//const unsigned long NO_PICKUP_LIMIT_MS = 30UL * 60UL * 1000UL; // 30 分鐘
//const unsigned long NO_RETURN_LIMIT_MS = 10UL * 60UL * 1000UL; // 10 分鐘
const unsigned long NO_PICKUP_LIMIT_MS = 25UL * 1000UL; // 測試 25 秒
const unsigned long NO_RETURN_LIMIT_MS = 10UL * 1000UL; // 測試 10 秒

unsigned long lastPickupMs = 0;

// 每 500g 慶祝
const float CELEBRATE_STEP_G = 500.0f;
int celebrateMilestone = 0;

// 慶祝動畫狀態（非阻塞）
bool celebrateActive = false;
unsigned long celebrateStartMs = 0;
unsigned long lastCelebrateFrameMs = 0;
const unsigned long CELEBRATE_DURATION_MS = 3500;
const unsigned long CELEBRATE_FRAME_MS    = 80;

// Blynk LED 狀態追蹤
String lastBlynkColor = "";

// ================= 時間設定 (NTP) =================
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;
const int   daylightOffset_sec = 0;

// ================= WiFi/Blynk flags =================
bool wifiOK  = false;
bool blynkOK = false;

// =====================================================
// WiFi / Blynk connect with timeout (避免阻塞杯墊)
// =====================================================
bool connectWiFiWithTimeout(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("[WiFi] Connecting");

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] OK IP=");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("[WiFi] Failed -> offline mode.");
    return false;
  }
}

bool connectBlynkWithTimeout(unsigned long timeoutMs) {
  Blynk.config(BLYNK_AUTH_TOKEN);
  Serial.print("[Blynk] Connecting");

  unsigned long t0 = millis();
  while (!Blynk.connected() && millis() - t0 < timeoutMs) {
    Blynk.run();
    delay(50);
    Serial.print(".");
  }
  Serial.println();

  if (Blynk.connected()) {
    Serial.println("[Blynk] Connected!");
    return true;
  } else {
    Serial.println("[Blynk] Failed -> offline mode.");
    return false;
  }
}

// =====================================================
// EEPROM
// =====================================================
void saveToEEPROM() {
  storedData.savedEmptyWeight     = emptyWeight;
  storedData.savedTodayIntake     = todayIntake;
  storedData.savedEmptySet        = emptySet;
  // savedDayOfYear 在 checkOvernightReset 更新
  EEPROM.put(0, storedData);
  EEPROM.commit();
}

void loadFromEEPROM() {
  EEPROM.get(0, storedData);

  if (storedData.initializedMarker != MARKER_VALUE) {
    Serial.println("[EEPROM] First run -> init.");
    emptyWeight = 0.0f;
    todayIntake = 0.0f;
    emptySet = false;
    storedData.savedDayOfYear = -1;
    storedData.initializedMarker = MARKER_VALUE;
    saveToEEPROM();
  } else {
    emptyWeight = storedData.savedEmptyWeight;
    todayIntake = storedData.savedTodayIntake;
    emptySet    = storedData.savedEmptySet;

    Serial.println("[EEPROM] Loaded.");
    Serial.print("  emptySet="); Serial.println(emptySet);
    Serial.print("  emptyWeight="); Serial.println(emptyWeight, 1);
    Serial.print("  todayIntake="); Serial.println(todayIntake, 1);

    celebrateMilestone = (int)floor(todayIntake / CELEBRATE_STEP_G);

    if (emptySet) state = READY;
  }
}

// 隔夜歸零（需要 NTP，離線就不做）
void checkOvernightReset() {
  if (!wifiOK) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int currentDay = timeinfo.tm_yday;

  if (storedData.savedDayOfYear == -1) {
    storedData.savedDayOfYear = currentDay;
    saveToEEPROM();
    Serial.print("[TIME] dayOfYear init: ");
    Serial.println(currentDay);
    return;
  }

  if (currentDay != storedData.savedDayOfYear) {
    Serial.println("[NEW DAY] reset todayIntake to 0.");
    todayIntake = 0.0f;
    drinkCount = 0;
    celebrateMilestone = 0;

    storedData.savedDayOfYear = currentDay;
    saveToEEPROM();

    if (Blynk.connected()) Blynk.virtualWrite(V1, (int)todayIntake);
  }
}

// =====================================================
// LED helpers
// =====================================================
void setRingColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void ringOff() { setRingColor(0, 0, 0); }

void blinkToggle() {
  unsigned long now = millis();
  if (now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
  }
}

// ✅ 立刻渲染一幀慶祝（解決「紅燈蓋住慶祝」）
void renderCelebrateFrame() {
  // 殘影衰減
  for (int i = 0; i < NUM_PIXELS; i++) {
    uint32_t c = pixels.getPixelColor(i);
    uint8_t r = (uint8_t)(c >> 16);
    uint8_t g = (uint8_t)(c >> 8);
    uint8_t b = (uint8_t)(c);
    r = (uint8_t)(r * 0.6f);
    g = (uint8_t)(g * 0.6f);
    b = (uint8_t)(b * 0.6f);
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }

  // 隨機亮 2 顆
  for (int k = 0; k < 2; k++) {
    int idx = random(NUM_PIXELS);
    pixels.setPixelColor(idx, pixels.Color(random(80, 255), random(80, 255), random(80, 255)));
  }
  pixels.show();
}

void updateCelebrateAnimation() {
  unsigned long now = millis();
  if (!celebrateActive) return;

  if (now - celebrateStartMs >= CELEBRATE_DURATION_MS) {
    celebrateActive = false;
    return;
  }

  if (now - lastCelebrateFrameMs < CELEBRATE_FRAME_MS) return;
  lastCelebrateFrameMs = now;

  renderCelebrateFrame();
}

void startCelebrateIfNeeded() {
  int targetMilestone = (int)floor(todayIntake / CELEBRATE_STEP_G);
  if (targetMilestone > celebrateMilestone) {
    celebrateMilestone = targetMilestone;
    celebrateActive = true;
    celebrateStartMs = millis();
    lastCelebrateFrameMs = 0;

    // ✅ 先清掉上一個狀態（例如紅燈），並立即畫一幀
    ringOff();
    renderCelebrateFrame();

    Serial.print("[CELEBRATE] milestone reached: ");
    Serial.println(celebrateMilestone);

    if (Blynk.connected()) {
      Blynk.setProperty(V3, "color", "#D000FF");
      Blynk.virtualWrite(V3, 255);
      lastBlynkColor = "#D000FF";
    }
  }
}

// =====================================================
// HX711 stable read
// =====================================================
float readWeightStable() {
  if (!scale.wait_ready_timeout(120)) return lastStableWeight;

  float w = scale.get_units(READ_SAMPLES);
  if (fabs(w - lastStableWeight) < JITTER_DEADBAND)
    return lastStableWeight;

  lastStableWeight = w;
  return w;
}

// 杯子存在判斷（遲滯）
bool updateCupPresent(float gross, bool prev) {
  if (!prev) return (gross > CUP_PRESENT_ON);
  else       return (gross > CUP_PRESENT_OFF);
}

// 長按按鈕
bool buttonLongPressEvent() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if (millis() - lastDebounceTime > debounceDelay) {
    if (lastButtonState == HIGH && reading == LOW) {
      pressStartMs = millis();
      longPressFired = false;
    }
    if (reading == LOW && !longPressFired &&
        millis() - pressStartMs >= LONG_PRESS_MS) {
      longPressFired = true;
      lastButtonState = reading;
      return true;
    }
  }
  lastButtonState = reading;
  return false;
}

void resetPlacementSettling() {
  placementSettling = false;
  settleSum = 0;
  settleCount = 0;
  settleLast = 0;
}

// 第三次量測器（用在 SET EMPTY）
bool sampleWeightOnce(float gross, float &result) {
  if (!samplingActive) return false;

  sampleCounter++;
  if (sampleCounter == VALID_SAMPLE_INDEX) {
    result = gross;
    samplingActive = false;
    return true;
  }
  return false;
}

// 設定空瓶：啟動第三次量測
void doSetEmptyBottle() {
  samplingActive = true;
  sampleCounter = 0;
  Serial.println("[SET EMPTY] sampling started...");
}

// =====================================================
// Blynk data send
// =====================================================
void sendBlynkData() {
  if (!Blynk.connected()) return;

  if (cupPresent && emptySet) Blynk.virtualWrite(V0, (int)lastWaterWeight);
  else                       Blynk.virtualWrite(V0, 0);

  Blynk.virtualWrite(V1, (int)todayIntake);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeString[20];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d", &timeinfo);
    Blynk.virtualWrite(V2, timeString);
  }
}

// =====================================================
// LED + Blynk status
// 優先順序：慶祝 > 紅燈(低水量) > 橘閃(未放回) > 黃閃(未拿起) > 綠(正常上杯) > 關燈
// =====================================================
void updateLEDAndBlynk(float water) {
  String currentBlynkHex = "#000000";
  bool shouldLightUp = true;

  // 1) 慶祝最高優先
  if (celebrateActive) {
    updateCelebrateAnimation();
    // 慶祝期間 Blynk 顏色維持紫色（startCelebrateIfNeeded 已設）
    return;
  }

  unsigned long now = millis();

  bool lowWater =
    cupPresent && emptySet &&
    (water > EMPTY_IGNORE_G) &&
    (water < LOW_WATER_LIMIT_G);

  bool noReturnOverdue =
    (!cupPresent) &&
    cupAbsentTimerActive &&
    (now - cupAbsentSinceMs >= NO_RETURN_LIMIT_MS);

  bool noPickupOverdue =
    cupPresent &&
    (now - lastPickupMs >= NO_PICKUP_LIMIT_MS);

  if (lowWater) {
    setRingColor(255, 0, 0);
    currentBlynkHex = "#FF0000";
  }
  else if (noReturnOverdue) {
    blinkToggle();
    if (blinkOn) setRingColor(255, 120, 0);
    else ringOff();
    currentBlynkHex = "#FF8C00";
  }
  else if (noPickupOverdue) {
    blinkToggle();
    if (blinkOn) setRingColor(255, 220, 0);
    else ringOff();
    currentBlynkHex = "#FFD700";
  }
  else {
    if (cupPresent && emptySet) {
      ringOff();
      currentBlynkHex = "#00FF00";
    } else {
      ringOff();
      currentBlynkHex = "#000000";
      shouldLightUp = false;
    }
  }

  if (Blynk.connected() && currentBlynkHex != lastBlynkColor) {
    Blynk.setProperty(V3, "color", currentBlynkHex);
    Blynk.virtualWrite(V3, shouldLightUp ? 255 : 0);
    lastBlynkColor = currentBlynkHex;
  }
}

// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("BOOT OK");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // HW init
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pixels.begin();
  ringOff();

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);

  // ✅ 重要：保持「舊杯墊邏輯」：開機一定 tare（避免 gross 漂移/負值）
  scale.tare();
  delay(200);

  randomSeed(analogRead(0));

  Serial.println("✅ HX711 ready");

  // Load memory
  loadFromEEPROM();

  // WiFi/Blynk
  Serial.println("Connecting WiFi/Blynk (non-blocking) ...");
  wifiOK = connectWiFiWithTimeout(8000);
  if (wifiOK) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    blynkOK = connectBlynkWithTimeout(5000);
  }

  // Timers
  timer.setInterval(1000L, sendBlynkData);
  timer.setInterval(60000L, checkOvernightReset);

  if (!emptySet) {
    state = NEED_TARE;
    Serial.println("=== LONG PRESS TO SET EMPTY BOTTLE ===");
  } else {
    state = READY;
    Serial.println("=== READY (Loaded from memory) ===");
  }

  lastPickupMs = millis();
}

// =====================================================
void loop() {
  // 讓 Blynk + timer 跑（不影響杯墊）
  if (blynkOK) Blynk.run();
  timer.run();

  // ========== NEED_TARE ==========
  if (state == NEED_TARE) {
    blinkToggle();
    if (blinkOn) setRingColor(0, 0, 255);
    else ringOff();

    if (buttonLongPressEvent()) {
      doSetEmptyBottle();
      ringOff();
      state = READY;
      Serial.println("[STATE] READY");
    }
    delay(10);
    return;
  }

  // ========== READY ==========
  if (buttonLongPressEvent()) {
    doSetEmptyBottle();
  }

  float gross = readWeightStable();
  float water = gross - emptyWeight;
  if (water < 0) water = 0;

  // ===== 空瓶設定：第三次量測判斷 =====
  float validW;
  if (sampleWeightOnce(gross, validW)) {
    if (validW >= CUP_PRESENT_ON) {
      emptyWeight = validW;
      emptySet = true;

      // reset runtime states
      lastWaterWeight = 0.0f;
      baselineWaterStable = 0.0f;
      weightBeforeLift = 0.0f;
      drinkingInProgress = false;

      // 你舊版是設定空瓶就清空今日
      todayIntake = 0.0f;
      drinkCount = 0;
      pickupCount = 0;
      celebrateMilestone = 0;
      celebrateActive = false;

      lastPickupMs = millis();

      Serial.print("[SET EMPTY] emptyWeight=");
      Serial.print(emptyWeight, 1);
      Serial.println(" g ✔");

      saveToEEPROM();
    } else {
      Serial.println("[SET EMPTY] no bottle detected, aborted.");
    }
  }

  cupPresent = updateCupPresent(gross, lastCupPresent);

  // ===== 拿起 =====
  if (lastCupPresent && !cupPresent) {
    pickupCount++;
    lastPickupMs = millis();

    cupAbsentTimerActive = true;
    cupAbsentSinceMs = millis();

    if (baselineWaterStable < MIN_BASELINE_WATER) {
      Serial.println("[PICKUP] baseline water too low, ignore drink/refill.");
      resetPlacementSettling();
      lastCupPresent = cupPresent;
      updateLEDAndBlynk(water);
      return;
    }

    weightBeforeLift = baselineWaterStable;
    drinkingInProgress = true;

    resetPlacementSettling();
    Serial.print("[PICKUP] count=");
    Serial.println(pickupCount);
  }

  // ===== 放回 =====
  if (!lastCupPresent && cupPresent) {
    placementSettling = true;
    placeStartMs = millis();
    settleSum = 0;
    settleCount = 0;
    settleLast = water;

    // ✅ 放回就停止「未放回」計時，避免橘燈繼續
    cupAbsentTimerActive = false;

    // ✅ FIX：杯子放回來，表示「有互動」，25 秒未拿起要從此刻重新開始算
    lastPickupMs = millis();

    Serial.println("[PLACE] settling...");
  }


  // ===== 穩定期 =====
  if (placementSettling) {
    if (!cupPresent) {
      resetPlacementSettling();
    } else {
      settleSum += water;
      settleCount++;

      if (fabs(water - settleLast) > STABLE_DELTA_G) {
        placeStartMs = millis();
        settleSum = water;
        settleCount = 1;
      }
      settleLast = water;

      if (millis() - placeStartMs >= PLACEMENT_SETTLE_MS) {
        float stableWater = settleSum / settleCount;

        float threshold = ACTION_MIN_G;
        float ratioThr = weightBeforeLift * ACTION_RATIO;
        if (ratioThr > threshold) threshold = ratioThr;

        float diff = weightBeforeLift - stableWater;

        if (drinkingInProgress) {
          if (fabs(diff) < threshold) {
            Serial.print("[ACTION] small change ignored. thr=");
            Serial.println(threshold, 1);
          } else if (diff >= threshold) {
            todayIntake += diff;
            drinkCount++;

            Serial.print("[DRINK] -");
            Serial.print(diff, 1);
            Serial.print(" g | thr=");
            Serial.print(threshold, 1);
            Serial.print(" | today=");
            Serial.print(todayIntake, 1);
            Serial.print(" | count=");
            Serial.println(drinkCount);

            startCelebrateIfNeeded();
            saveToEEPROM();
          } else {
            Serial.print("[REFILL] +");
            Serial.print(-diff, 1);
            Serial.print(" g (ignored) | thr=");
            Serial.println(threshold, 1);
          }
        }

        lastWaterWeight = stableWater;
        baselineWaterStable = stableWater;

        drinkingInProgress = false;
        resetPlacementSettling();
      }
    }
  }

  if (cupPresent && !placementSettling && !drinkingInProgress) {
    lastWaterWeight = water;
  }

  lastCupPresent = cupPresent;

  // LED & Blynk
  updateLEDAndBlynk(water);

  // debug print
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    Serial.print("gross=");
    Serial.print(gross, 1);
    Serial.print(" | water=");
    Serial.print(water, 1);
    Serial.print(" | empty=");
    Serial.print(emptyWeight, 1);
    Serial.print(" | base=");
    Serial.print(baselineWaterStable, 1);
    Serial.print(" | pickup=");
    Serial.print(pickupCount);
    Serial.print(" | drink=");
    Serial.print(drinkCount);
    Serial.print(" | today=");
    Serial.println(todayIntake, 1);
    lastPrint = millis();
  }

  delay(200);
}
