#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include "MAX30100_Registers.h"
#include "MAX30100.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// NUS UUIDs 
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Phone -> ESP32
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP32 -> Phone

// Objects
Adafruit_MPU6050 mpu;
MAX30100 sensor;

BLECharacteristic *pTxCharacteristic = nullptr;
BLECharacteristic *pRxCharacteristic = nullptr;
BLEServer *pServer = nullptr;

volatile bool deviceConnected = false;

// Emergency Button 
const int PIN_EMERGENCY = 27;      // GPIO27
const uint32_t DEBOUNCE_MS = 250;
uint32_t lastBtnMs = 0;
bool lastBtnLevel = HIGH;

// MAX30100 settings
const float alphaDC = 0.95;
const float alphaEMA = 0.10; 
const float minAbsoluteThreshold = 20.0;
const float thresholdFactor = 0.85; 
const long minPulseInterval = 550; 

// MAX30100 variables
float currentInput = 0;
float lastInput = 0;
float acSignal = 0;
float lastAcSignal = 0;
float filteredSignal = 0;
float prevFilteredSignal = 0;
float runningMax = 0;
float dynamicThreshold = 0;
long lastBeatTime = 0;
float bpm = 0;
uint16_t lastRawIR = 0;
bool finger = false;

// State control for prevent the double peak
bool waitingForFall = false; 

// MPU6050 timing
unsigned long lastMpuReport = 0;
#define MPU_REPORT_PERIOD_MS 100

// Debug counters
uint32_t maxSamples = 0;
uint32_t lastMaxPrint = 0;

// MAX no-sample watchdog 
uint32_t lastMaxDataMs = 0;
uint32_t maxNoSampleReinits = 0;

// Thresholds received from Flutter (stored)
float thrAX = 25, thrAY = 25, thrAZ = 25;
float thrGX = 25, thrGY = 25, thrGZ = 25;
float thrBpmMin = 50, thrBpmMax = 100;
float thrFallDelta = 12;
float thrInactScore = 3;
int   thrInactSec = 60;
float thrK = 2.5;

// I2C scan
void i2cScan() {
  Serial.println("I2C scan...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(" - Found 0x");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("I2C scan done.");
}

// Reinit MAX30100 robustly
bool configMax30100() {
  if (!sensor.begin()) return false;

  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_27_1MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);

  // algorythm state reset
  currentInput = lastInput = 0;
  acSignal = lastAcSignal = 0;
  filteredSignal = prevFilteredSignal = 0;
  runningMax = 0;
  dynamicThreshold = 0;
  lastBeatTime = 0;
  bpm = 0;
  lastRawIR = 0;
  finger = false;
  waitingForFall = false; // Burası da sıfırlanmalı

  lastMaxDataMs = millis();
  return true;
}

// BLE callbacks 
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    Serial.println(">> BLE connected");
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    Serial.println(">> BLE disconnected -> advertising restart");
    s->getAdvertising()->start();
  }
};

class MyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() == 0) return;
    rxValue.trim();
    Serial.print("RX: "); Serial.println(rxValue);
    int first = rxValue.indexOf(',');
    if (first < 0) return;
    String type = rxValue.substring(0, first);
    String rest = rxValue.substring(first + 1);
    if (type == "THR") {
      int second = rest.indexOf(',');
      if (second < 0) return;
      String key = rest.substring(0, second);
      float f = rest.substring(second + 1).toFloat();
      if (key == "AX") thrAX = f;
      else if (key == "AY") thrAY = f;
      else if (key == "AZ") thrAZ = f;
      else if (key == "GX") thrGX = f;
      else if (key == "GY") thrGY = f;
      else if (key == "GZ") thrGZ = f;
      else if (key == "BPM_MIN") thrBpmMin = f;
      else if (key == "BPM_MAX") thrBpmMax = f;
      else if (key == "FALL") thrFallDelta = f;
      else if (key == "INACT") thrInactScore = f;
      else if (key == "INACT_SEC") thrInactSec = (int)f;
      else if (key == "K") thrK = f;
      Serial.printf("THR set %s = %.3f\n", key.c_str(), f);
    }
  }
};

void handleEmergencyButton() {
  bool lvl = digitalRead(PIN_EMERGENCY);
  uint32_t now = millis();
  if (lvl != lastBtnLevel) { lastBtnMs = now; lastBtnLevel = lvl; }
  static bool pressedLatched = false;
  if ((now - lastBtnMs) > DEBOUNCE_MS) {
    if (lastBtnLevel == LOW && !pressedLatched) {
      pressedLatched = true;
      Serial.println("EVT,EMERGENCY");
      if (deviceConnected && pTxCharacteristic) {
        const char *msg = "EVT,EMERGENCY";
        pTxCharacteristic->setValue((uint8_t*)msg, strlen(msg));
        pTxCharacteristic->notify();
      }
    }
    if (lastBtnLevel == HIGH) pressedLatched = false;
  }
}

void updateMax30100() {
  sensor.update();
  uint16_t rawIR, rawRed;
  bool gotAny = false;

  while (sensor.getRawValues(&rawIR, &rawRed)) {
    gotAny = true;
    maxSamples++;
    lastMaxDataMs = millis();

    lastRawIR = rawIR;
    currentInput = (float)rawIR;

    // DC blocking
    acSignal = alphaDC * (lastAcSignal + currentInput - lastInput);
    lastInput = currentInput;
    lastAcSignal = acSignal;

    // EMA
    filteredSignal = (alphaEMA * acSignal) + ((1.0 - alphaEMA) * filteredSignal);

    // dynamic threshold
    runningMax = runningMax * 0.98;
    if (filteredSignal > runningMax) runningMax = filteredSignal;
    dynamicThreshold = runningMax * thresholdFactor;
    if (dynamicThreshold < minAbsoluteThreshold) dynamicThreshold = minAbsoluteThreshold;

    finger = (lastRawIR > 500);

    // Peak detect
    bool isPeak = (filteredSignal < prevFilteredSignal) && (prevFilteredSignal > dynamicThreshold);

    if (isPeak && !waitingForFall && (millis() - lastBeatTime > minPulseInterval)) {
      long delta = millis() - lastBeatTime;
      lastBeatTime = millis();
      float instantBpm = 60000.0 / delta;
      if (instantBpm > 40 && instantBpm < 180) {
        bpm = (bpm * 0.85) + (instantBpm * 0.15);
      }
      waitingForFall = true; 
    }

    
    if (waitingForFall && (filteredSignal < (dynamicThreshold * 0.75))) {
      waitingForFall = false;
    }
 

    prevFilteredSignal = filteredSignal;
  }

  if (!gotAny && (millis() - lastMaxDataMs > 500)) {
    maxNoSampleReinits++;
    Serial.print("[MAX] NO SAMPLES -> REINIT #");
    Serial.println(maxNoSampleReinits);
    delay(5);
    bool ok = configMax30100();
    Serial.print("[MAX] reinit ok=");
    Serial.println(ok ? "1" : "0");
    if (maxNoSampleReinits % 5 == 1) i2cScan();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== Dual Sensor + BLE (MAX Auto-Reinit) ===");

  Wire.begin(21, 22);
  Wire.setClock(100000);   
  Wire.setTimeout(10);    

  pinMode(PIN_EMERGENCY, INPUT_PULLUP);

  Serial.print("MPU6050...");
  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("FAIL");
    while (1) delay(10);
  }
  Serial.println("OK");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.print("MAX30100...");
  bool ok = configMax30100();
  Serial.println(ok ? "OK" : "FAIL");
  if (!ok) {
    i2cScan();
    while (1) delay(10);
  }

  i2cScan();

  BLEDevice::init("ESP32-DUAL");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRxCharacteristic->setCallbacks(new MyRxCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE advertising started");
  lastMaxDataMs = millis();
}

void loop() {
  handleEmergencyButton();
  updateMax30100();

  if (millis() - lastMaxPrint > 1000) {
    lastMaxPrint = millis();
    Serial.print("[MAX] samples/s=");
    Serial.print(maxSamples);
    Serial.print(" lastRawIR=");
    Serial.print(lastRawIR);
    Serial.print(" reinits=");
    Serial.println(maxNoSampleReinits);
    maxSamples = 0;
  }

  if (millis() - lastMpuReport >= MPU_REPORT_PERIOD_MS) {
    lastMpuReport = millis();
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    char out[160];
    snprintf(out, sizeof(out), "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.2f,%.2f,%.1f,%d",
      a.acceleration.x, a.acceleration.y, a.acceleration.z,
      g.gyro.x, g.gyro.y, g.gyro.z,
      (unsigned)lastRawIR, filteredSignal, dynamicThreshold, bpm, finger ? 1 : 0);

    Serial.print("AX:"); Serial.print(a.acceleration.x, 2);
    Serial.print(" AY:"); Serial.print(a.acceleration.y, 2);
    Serial.print(" AZ:"); Serial.print(a.acceleration.z, 2);
    Serial.print(" | RawIR:"); Serial.print(lastRawIR);
    Serial.print(" | BPM:");
    if (!finger) Serial.println("Parmak yok");
    else Serial.println(bpm, 1);

    if (deviceConnected && pTxCharacteristic) {
      pTxCharacteristic->setValue((uint8_t*)out, strlen(out));
      pTxCharacteristic->notify();
    }
  }
  delay(5);
}