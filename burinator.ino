#include <AsyncDelay.h>
#include <SoftWire.h>
#include <AS3935.h>
#ifdef JTD
#include <DisableJTAG.h>
#endif

AS3935 as3935;
bool ledState = true;
AsyncDelay d;
AsyncDelay noiseCalibTimer;
AsyncDelay ledFlashTimer;
const uint8_t LED_PIN = 16; // D0 na ESP8266

ICACHE_RAM_ATTR void int2Handler() {
  as3935.interruptHandler();
}

void readRegs(uint8_t start, uint8_t end) {
  Serial.println(F("Reading registers:"));
  for (uint8_t reg = start; reg < end; ++reg) {
    delay(20);
    uint8_t val;
    if (as3935.readRegister(reg, val)) {
      Serial.print(F("Reg 0x"));
      Serial.print(reg, HEX);
      Serial.print(F(": 0x"));
      Serial.println(val, HEX);
    } else {
      Serial.print(F("Reg 0x"));
      Serial.print(reg, HEX);
      Serial.println(F(": read error"));
    }
  }
  Serial.println(F("------------------"));
}

void printInterruptReason(Stream &s, uint8_t flags, const char *prefix = nullptr) {
  if (flags & AS3935::intNoiseLevelTooHigh) {
    if (prefix) s.print(prefix);
    s.println(F("Warning: Noise level too high"));
  }
  if (flags & AS3935::intDisturberDetected) {
    if (prefix) s.print(prefix);
    s.println(F("Warning: Disturber detected"));
  }
  if (flags & AS3935::intLightningDetected) {
    if (prefix) s.print(prefix);
    s.println(F("Lightning detected!"));
  }
}

void autoCalibrateNoiseFloor() {
  Serial.println(F("Starting auto-calibration of noise floor..."));
  uint8_t bestFloor = 0;
  uint8_t minNoise = 255;
  for (uint8_t nf = 0; nf <= 7; nf++) {
    as3935.setNoiseFloor(nf);
    delay(200);
    uint8_t flags = as3935.getInterruptFlags();
    uint8_t noise = (flags & AS3935::intNoiseLevelTooHigh) ? 1 : 0;
    Serial.print(F("Noise floor "));
    Serial.print(nf);
    Serial.print(" -> Noise level too high flag: ");
    Serial.println(noise);

    if (noise < minNoise) {
      minNoise = noise;
      bestFloor = nf;
      if (noise == 0) break;
    }
  }
  as3935.setNoiseFloor(bestFloor);
  Serial.print(F("Auto-calibration done. Set noise floor to "));
  Serial.println(bestFloor);
}

void setup() {
#ifdef JTD
  disableJTAG();
#endif

  Serial.begin(115200);
  delay(1000);

  Serial.println(F("Initializing AS3935 sensor..."));

  as3935.initialise(4, 5, 0x03, 3, true, NULL);
  as3935.start();

  attachInterrupt(14, int2Handler, RISING);

  autoCalibrateNoiseFloor();
  as3935.setIndoor(true);
  as3935.setSpikeRejection(7);

  Serial.println(F("Sensor setup complete."));
  readRegs(0, 0x09);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, ledState);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED zhasnutá

  d.start(1000, AsyncDelay::MILLIS);
  noiseCalibTimer.start(60000, AsyncDelay::MILLIS); // 60s kalibrace
  ledFlashTimer.start(0xFFFFFFFF, AsyncDelay::MILLIS); // deaktivace časovače LED
}

void loop() {
  if (as3935.process()) {
    uint8_t flags = as3935.getInterruptFlags();
    printInterruptReason(Serial, flags, "  ");

    if (flags & AS3935::intLightningDetected) {
      uint8_t dist = as3935.getDistance();
      Serial.print(F("Distance to lightning: "));
      Serial.print(dist);
      Serial.println(F(" km"));
      digitalWrite(LED_PIN, HIGH);  // Rozsvítit LED na D0
      ledFlashTimer.start(500, AsyncDelay::MILLIS); // LED svítí 500 ms
    }
    Serial.println(F("----"));
  }

  if (as3935.getBusError()) {
    Serial.println(F("Bus error detected! Clearing..."));
    as3935.clearBusError();
  }

  if (noiseCalibTimer.isExpired()) {
    autoCalibrateNoiseFloor();
    noiseCalibTimer.start(60000, AsyncDelay::MILLIS);
  }

  if (ledFlashTimer.isExpired()) {
    digitalWrite(LED_PIN, LOW);   // Zhasnout LED po 500ms
    ledFlashTimer.start(0xFFFFFFFF, AsyncDelay::MILLIS); // deaktivace časovače LED
  }

  if (d.isExpired()) {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
    d.start(1000, AsyncDelay::MILLIS);
  }
}
