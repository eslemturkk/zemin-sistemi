#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define ZONE_PIN A0
#define CAP_PIN A3
#define LED_STEP 4

const float VREF = 5.0;
const int ADC_MAX = 1023;
const int MIN_VALID_MV = 20;
const unsigned long DISPLAY_MS = 250;
const unsigned long SERIAL_MS = 250;
const unsigned long DEBOUNCE_MS = 150;

unsigned long stepCount = 0;
int currentMV = 0;
int prevMV = 0;
float energyMJ = 0.0;
bool securityMode = false;
bool stepLedState = false;
unsigned long ledOffMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastSerialMs = 0;
unsigned long lastStepHitMs = 0;
unsigned long lastSecurityBlinkMs = 0;
bool securityBlinkState = false;

char rxBuf[32];
uint8_t rxIdx = 0;

int readMV(int pin, int samples = 8) {
  analogRead(pin);
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delayMicroseconds(250);
  }

  int raw = total / samples;
  int mv = (int)(raw * (VREF / ADC_MAX) * 1000.0);
  if (mv < MIN_VALID_MV) mv = 0;
  return mv;
}

float readVoltage(int pin, int samples = 8) {
  analogRead(pin);
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delayMicroseconds(250);
  }

  int raw = total / samples;
  return (float)raw * (VREF / ADC_MAX);
}

bool initOLED() {
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) return true;
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) return true;
  return false;
}

void handleSerialCommand(const char *cmd) {
  if (strncmp(cmd, "GMOD:1", 6) == 0) {
    securityMode = true;
    Serial.println(F("OK GMOD:1"));
  } else if (strncmp(cmd, "GMOD:0", 6) == 0) {
    securityMode = false;
    Serial.println(F("OK GMOD:0"));
  }
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (rxIdx > 0) {
        rxBuf[rxIdx] = '\0';
        handleSerialCommand(rxBuf);
        rxIdx = 0;
      }
    } else if (rxIdx < sizeof(rxBuf) - 1) {
      rxBuf[rxIdx++] = c;
    }
  }
}

void scanAndCount() {
  currentMV = readMV(ZONE_PIN, 8);

  bool risingEdge = (currentMV > prevMV);
  bool debounceOK = ((millis() - lastStepHitMs) >= DEBOUNCE_MS);

  if (risingEdge && debounceOK) {
    stepCount++;
    lastStepHitMs = millis();
    stepLedState = true;
    ledOffMs = millis() + 60;

    float v = readVoltage(CAP_PIN, 4);
    energyMJ += v * 4.8;
  }

  prevMV = currentMV;
}

void updateLED() {
  if (securityMode) {
    if ((millis() - lastSecurityBlinkMs) >= 300) {
      lastSecurityBlinkMs = millis();
      securityBlinkState = !securityBlinkState;
    }
    digitalWrite(LED_STEP, securityBlinkState ? HIGH : LOW);
    return;
  }

  if (ledOffMs != 0 && millis() >= ledOffMs) {
    stepLedState = false;
    ledOffMs = 0;
  }

  digitalWrite(LED_STEP, stepLedState ? HIGH : LOW);
}

void updateDisplay() {
  if (!display.width()) return;
  if (millis() - lastDisplayMs < DISPLAY_MS) return;
  lastDisplayMs = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(F("Adim:"));
  display.setCursor(0, 22);
  display.print(stepCount);

  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print(F("mV: "));
  display.print(currentMV);
  display.print(F("  GMOD:"));
  display.print(securityMode ? 1 : 0);

  display.display();
}

void sendSerial() {
  if (millis() - lastSerialMs < SERIAL_MS) return;
  lastSerialMs = millis();

  float voltage = (float)currentMV / 1000.0;

  Serial.print(F("{"));
  Serial.print(F("\"steps\":"));
  Serial.print(stepCount);
  Serial.print(F(",\"voltage\":"));
  Serial.print(voltage, 2);
  Serial.print(F(",\"energy_mj\":"));
  Serial.print(energyMJ, 2);
  Serial.print(F(",\"alarm\":false"));
  Serial.print(F(",\"security_mode\":"));
  Serial.print(securityMode ? 1 : 0);
  Serial.println(F("}"));
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_STEP, OUTPUT);
  digitalWrite(LED_STEP, LOW);

  Wire.begin();

  if (!initOLED()) {
    while (1) {
      digitalWrite(LED_STEP, HIGH);
      delay(200);
      digitalWrite(LED_STEP, LOW);
      delay(200);
    }
  }

  currentMV = readMV(ZONE_PIN, 8);
  prevMV = currentMV;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("UNO Arayuz Hazir"));
  display.println(F("Serial JSON + GMOD"));
  display.display();
  delay(1000);
}

void loop() {
  readSerial();
  scanAndCount();
  updateLED();
  updateDisplay();
  sendSerial();
  delay(40);
}