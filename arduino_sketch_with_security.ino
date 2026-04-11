// ================================================================
//  PİEZO ZEMİN SİSTEMİ — 16 PİEZO / 4 BÖLGE / ARDUINO UNO + GÜVENLIK MODU
//  (Orijinal kodun güvenlik modu özellikleri eklenen sürümü)
//
//  ── GÜVENLIK MODU EKLEMELER ──────────────────────────────────
//
//  Frontend arayüzünden:
//    GMOD:1  → güvenlik modunu AÇ  (tüm LED'ler yanıp söner)
//    GMOD:0  → güvenlik modunu KAPA (normal mod)
//
//  Güvenlik modunda:
//    • Tüm bölge LED'leri eşzamanlı yanıp söner (uyarı efekti)
//    • JSON çıkışına "security_mode": 1 / 0 eklenir
//    • Adım sayma devam eder
//    • Kapasitör ölçümü devam eder
//
// ================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <math.h>

// ────────────────────────────────────────────────────────────────
//  OLED
// ────────────────────────────────────────────────────────────────
#define SCREEN_W 128
#define SCREEN_H 64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledOK = false;

// ────────────────────────────────────────────────────────────────
//  PİN TANIMLARI
// ────────────────────────────────────────────────────────────────
#define NUM_ZONES 4

const int zonePins[NUM_ZONES] = {A0, A1, A2, A3};

#define CAP_PIN A3

#define PIN_LED_NORMAL 2
#define PIN_LED_ENERGY 3
const int zoneLedPins[NUM_ZONES] = {4, 5, 6, 7};

// ────────────────────────────────────────────────────────────────
//  FİZİKSEL SABİTLER
// ────────────────────────────────────────────────────────────────
const float VREF    = 5.0;
const int   ADC_MAX = 1023;

const float CAP_FARAD = 1.5;

const float ENERGY_LED_THRESH = 2.0; // Volt

// ────────────────────────────────────────────────────────────────
//  BÖLGE EŞİĞİ — KALİBRASYON
// ────────────────────────────────────────────────────────────────
const int ZONE_THRESH = 150;
const unsigned long DEBOUNCE_MS = 150;

// ────────────────────────────────────────────────────────────────
//  EEPROM ADRESLERİ
// ────────────────────────────────────────────────────────────────
const int EEPROM_ADDR_TOTAL  = 0;
const int EEPROM_ADDR_DAILY  = sizeof(unsigned long);
const int EEPROM_ADDR_ENERGY = sizeof(unsigned long) * 2;

const unsigned long EEPROM_INTERVAL = 60000UL;
const unsigned long EEPROM_BATCH    = 10;

// ────────────────────────────────────────────────────────────────
//  ZAMANLAMA
// ────────────────────────────────────────────────────────────────
const unsigned long DISPLAY_MS = 300;
const unsigned long SERIAL_MS  = 1000;
const unsigned long SECURITY_BLINK_MS = 300;

// ────────────────────────────────────────────────────────────────
//  DURUM DEĞİŞKENLERİ
// ────────────────────────────────────────────────────────────────
bool          zoneActive[NUM_ZONES] = {false};
unsigned long zoneLastHit[NUM_ZONES] = {0};
unsigned long zoneCount[NUM_ZONES]   = {0};

int zoneRaw[NUM_ZONES] = {0};

unsigned long totalSteps = 0;
unsigned long dailySteps = 0;

float capV     = 0.0;
float capVPrev = 0.0;
float totalEJ  = 0.0;

unsigned long lastEEPROMms   = 0;
unsigned long lastSavedTotal = 0;

char curDate[11]  = "";
char lastDate[11] = "";
bool dateReceived = false;

char    rxBuf[32];
uint8_t rxIdx = 0;

unsigned long lastDisplayMs = 0;
unsigned long lastSerialMs  = 0;

// ────────────────────────────────────────────────────────────────
//  GÜVENLIK MODU — YENİ
// ────────────────────────────────────────────────────────────────
bool securityMode = false;
unsigned long lastSecurityBlinkMs = 0;
bool securityBlinkState = false;

// ────────────────────────────────────────────────────────────────
//  YARDIMCI FONKSİYONLAR
// ────────────────────────────────────────────────────────────────
float safeF(float v) {
  if (isnan(v) || isinf(v)) return 0.0;
  return v;
}

float toVolt(int raw) {
  return (float)raw * (VREF / (float)ADC_MAX);
}

int toPercent(float v) {
  int p = (int)((v / VREF) * 100.0);
  return (p < 0) ? 0 : (p > 100 ? 100 : p);
}

void drawBar(int x, int y, int w, int h, int pct) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = map(pct, 0, 100, 0, w - 2);
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

// ────────────────────────────────────────────────────────────────
//  EEPROM: YÜKLEME
// ────────────────────────────────────────────────────────────────
void loadEEPROM() {
  EEPROM.get(EEPROM_ADDR_TOTAL,  totalSteps);
  EEPROM.get(EEPROM_ADDR_DAILY,  dailySteps);
  EEPROM.get(EEPROM_ADDR_ENERGY, totalEJ);

  if (totalSteps > 10000000UL) totalSteps = 0;
  if (dailySteps > 10000000UL) dailySteps = 0;
  if (isnan(totalEJ) || isinf(totalEJ) || totalEJ < 0.0) totalEJ = 0.0;

  lastSavedTotal = totalSteps;
}

// ────────────────────────────────────────────────────────────────
//  EEPROM: KOŞULLU KAYDETME
// ────────────────────────────────────────────────────────────────
void saveEEPROMIfNeeded() {
  bool timeOk = ((millis() - lastEEPROMms) >= EEPROM_INTERVAL);
  bool stepOk = ((totalSteps - lastSavedTotal) >= EEPROM_BATCH);

  if (timeOk || stepOk) {
    EEPROM.put(EEPROM_ADDR_TOTAL,  totalSteps);
    EEPROM.put(EEPROM_ADDR_DAILY,  dailySteps);
    EEPROM.put(EEPROM_ADDR_ENERGY, totalEJ);
    lastEEPROMms   = millis();
    lastSavedTotal = totalSteps;
  }
}

// ────────────────────────────────────────────────────────────────
//  ENERJİ ÖLÇÜMÜ
// ────────────────────────────────────────────────────────────────
void updateEnergy() {
  capVPrev = capV;

  analogRead(CAP_PIN);
  int raw = analogRead(CAP_PIN);
  capV = safeF(toVolt(raw));

  float dE = 0.5 * CAP_FARAD *
             ((capV * capV) - (capVPrev * capVPrev));
  if (dE > 0.0) totalEJ += dE;
}

// ────────────────────────────────────────────────────────────────
//  BÖLGE TARAMA VE ADIM SAYMA
// ────────────────────────────────────────────────────────────────
void scanZones() {
  for (int i = 0; i < NUM_ZONES; i++) {
    analogRead(zonePins[i]);
    int raw = analogRead(zonePins[i]);
    zoneRaw[i] = raw;

    bool prevActive = zoneActive[i];
    bool nowActive  = (raw > ZONE_THRESH);

    bool risingEdge = (nowActive && !prevActive);
    bool debounceOK = ((millis() - zoneLastHit[i]) >= DEBOUNCE_MS);

    if (risingEdge && debounceOK) {
      totalSteps++;
      dailySteps++;
      zoneCount[i]++;
      zoneLastHit[i] = millis();
    }

    zoneActive[i] = nowActive;
  }
}

// ────────────────────────────────────────────────────────────────
//  LED GÜNCELLEME — GÜVENLIK MODU DESTEĞI İLE
// ────────────────────────────────────────────────────────────────
void updateLEDs() {
  digitalWrite(PIN_LED_NORMAL, HIGH);

  // Güvenlik modunda tüm LED'ler yanıp söner
  if (securityMode) {
    // Blink her 300ms
    if ((millis() - lastSecurityBlinkMs) >= SECURITY_BLINK_MS) {
      lastSecurityBlinkMs = millis();
      securityBlinkState = !securityBlinkState;
    }

    // Tüm LED'ler aynı state'te
    int state = securityBlinkState ? HIGH : LOW;
    digitalWrite(PIN_LED_ENERGY, state);
    for (int i = 0; i < NUM_ZONES; i++) {
      digitalWrite(zoneLedPins[i], state);
    }
  } else {
    // Normal mod: enerji ve bölge LED'leri normal davranış
    digitalWrite(PIN_LED_ENERGY, (capV >= ENERGY_LED_THRESH) ? HIGH : LOW);

    for (int i = 0; i < NUM_ZONES; i++) {
      digitalWrite(zoneLedPins[i], zoneActive[i] ? HIGH : LOW);
    }
  }
}

// ────────────────────────────────────────────────────────────────
//  SERİ KOMUT OKUMA — GÜVENLIK MOD KOMUTU DESTEĞI İLE
// ────────────────────────────────────────────────────────────────
void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      rxBuf[rxIdx] = '\0';

      // DATE komutu
      if (rxIdx >= 15 &&
          rxBuf[0]=='D' && rxBuf[1]=='A' &&
          rxBuf[2]=='T' && rxBuf[3]=='E' && rxBuf[4]==':') {

        char newDate[11];
        strncpy(newDate, rxBuf + 5, 10);
        newDate[10] = '\0';

        if (dateReceived && strcmp(newDate, lastDate) != 0) {
          dailySteps = 0;
          EEPROM.put(EEPROM_ADDR_DAILY, dailySteps);
        }

        strncpy(curDate,  newDate, 11);
        strncpy(lastDate, newDate, 11);
        dateReceived = true;
      }

      // GMOD komutu — YENİ
      // GMOD:1 veya GMOD:0
      else if (rxIdx >= 6 && 
               rxBuf[0]=='G' && rxBuf[1]=='M' &&
               rxBuf[2]=='O' && rxBuf[3]=='D' && rxBuf[4]==':') {

        char modeChar = rxBuf[5];
        if (modeChar == '1') {
          securityMode = true;
          Serial.println(F("GMOD:1 - Guvenlik Modu AKTIF"));
        } else if (modeChar == '0') {
          securityMode = false;
          Serial.println(F("GMOD:0 - Guvenlik Modu PASIF"));
        }
      }

      rxIdx = 0;

    } else {
      if (rxIdx < sizeof(rxBuf) - 1) {
        rxBuf[rxIdx++] = c;
      }
    }
  }
}

// ────────────────────────────────────────────────────────────────
//  OLED GÜNCELLEME
// ────────────────────────────────────────────────────────────────
void updateDisplay() {
  if (!oledOK) return;
  if ((millis() - lastDisplayMs) < DISPLAY_MS) return;
  lastDisplayMs = millis();

  int pct = toPercent(capV);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(F("TOT:"));
  display.print(totalSteps);

  display.setCursor(0, 10);
  display.print(F("GUN:"));
  display.print(dailySteps);

  display.setCursor(0, 20);
  display.print(F("V:"));
  display.print(capV, 2);
  display.print(F("V"));

  display.setCursor(0, 30);
  display.print(F("E:"));
  display.print(pct);
  display.print(F("%"));
  drawBar(42, 30, 82, 8, pct);

  display.setCursor(0, 42);
  display.print(F("B:"));
  for (int i = 0; i < NUM_ZONES; i++) {
    display.print(zoneActive[i] ? F("1") : F("0"));
    if (i < NUM_ZONES - 1) display.print(F(" "));
  }

  // GMOD durumu ekranında göster — YENİ
  display.setCursor(0, 54);
  if (securityMode) {
    display.print(F("GMOD:1 [AKTIF]"));
  } else if (dateReceived) {
    display.print(curDate);
  } else {
    display.print(F("?PC baglanmadi"));
  }

  display.display();
}

// ────────────────────────────────────────────────────────────────
//  SERİ VERİ GÖNDERME — JSON + GÜVENLIK MOD ALANI
// ────────────────────────────────────────────────────────────────
void sendSerial() {
  if ((millis() - lastSerialMs) < SERIAL_MS) return;
  lastSerialMs = millis();

  Serial.print(F("{"));

  Serial.print(F("\"date\":\""));
  Serial.print(dateReceived ? curDate : "");
  Serial.print(F("\""));

  Serial.print(F(",\"total\":"));
  Serial.print(totalSteps);

  Serial.print(F(",\"daily\":"));
  Serial.print(dailySteps);

  Serial.print(F(",\"vcap\":"));
  Serial.print(safeF(capV), 3);

  Serial.print(F(",\"epct\":"));
  Serial.print(toPercent(capV));

  Serial.print(F(",\"etotal_j\":"));
  Serial.print(safeF(totalEJ), 6);

  // Güvenlik modu durumu — YENİ
  Serial.print(F(",\"security_mode\":"));
  Serial.print(securityMode ? 1 : 0);

  Serial.print(F(",\"zones\":["));
  for (int i = 0; i < NUM_ZONES; i++) {
    Serial.print(zoneActive[i] ? 1 : 0);
    if (i < NUM_ZONES - 1) Serial.print(F(","));
  }
  Serial.print(F("]"));

  Serial.print(F(",\"zcounts\":["));
  for (int i = 0; i < NUM_ZONES; i++) {
    Serial.print(zoneCount[i]);
    if (i < NUM_ZONES - 1) Serial.print(F(","));
  }
  Serial.print(F("]"));

  Serial.print(F(",\"zraw\":["));
  for (int i = 0; i < NUM_ZONES; i++) {
    Serial.print(zoneRaw[i]);
    if (i < NUM_ZONES - 1) Serial.print(F(","));
  }
  Serial.print(F("]}"));
  Serial.println();
}

// ────────────────────────────────────────────────────────────────
//  SETUP
// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_NORMAL, OUTPUT);
  pinMode(PIN_LED_ENERGY, OUTPUT);
  for (int i = 0; i < NUM_ZONES; i++) {
    pinMode(zoneLedPins[i], OUTPUT);
    digitalWrite(zoneLedPins[i], LOW);
  }
  digitalWrite(PIN_LED_NORMAL, HIGH);
  digitalWrite(PIN_LED_ENERGY, LOW);

  loadEEPROM();
  Wire.begin();

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledOK = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Piezo Sistemi"));
    display.println(F("16 piezo / 4 bolge"));
    display.print(F("Toplam: "));
    display.println(totalSteps);
    display.display();
    delay(1500);
  } else {
    Serial.println(F("HATA: OLED bulunamadi. 0x3C veya 0x3D?"));
  }

  analogRead(CAP_PIN);
  capV     = safeF(toVolt(analogRead(CAP_PIN)));
  capVPrev = capV;

  Serial.println(F("Hazir. Komutlar: DATE:YYYY-MM-DD   GMOD:1/0"));
}

// ────────────────────────────────────────────────────────────────
//  LOOP
// ────────────────────────────────────────────────────────────────
void loop() {
  readSerial();
  updateEnergy();
  scanZones();
  updateLEDs();
  updateDisplay();
  sendSerial();
  saveEEPROMIfNeeded();

  delay(15);
}
