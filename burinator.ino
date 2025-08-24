#include <AsyncDelay.h>
#include <SoftWire.h>
#include <AS3935.h>
#ifdef JTD
#include <DisableJTAG.h>
#endif

AS3935 as3935;
AsyncDelay d;
AsyncDelay noiseCalibTimer;
AsyncDelay ledFlashTimer;

// Definice pinů pro 4 LED diody
const uint8_t LED_1 = 0;  // D3, GPIO0
const uint8_t LED_2 = 2;  // D4, GPIO2
const uint8_t LED_3 = 12; // D6, GPIO12
const uint8_t LED_4 = 13; // D7, GPIO13

bool ledState = false;

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

void turnOffAllLeds() {
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  digitalWrite(LED_3, LOW);
  digitalWrite(LED_4, LOW);
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

  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  pinMode(LED_4, OUTPUT);

  turnOffAllLeds();

  d.start(1000, AsyncDelay::MILLIS);
  noiseCalibTimer.start(60000, AsyncDelay::MILLIS);
  ledFlashTimer.start(0xFFFFFFFF, AsyncDelay::MILLIS);
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

      turnOffAllLeds();
      if (dist > 15) {
        digitalWrite(LED_1, HIGH);  // 1 LED
      } else if (dist > 6) {
        digitalWrite(LED_1, HIGH);
        digitalWrite(LED_2, HIGH);  // 2 LED
      } else if (dist > 5) {
        digitalWrite(LED_1, HIGH);
        digitalWrite(LED_2, HIGH);
        digitalWrite(LED_3, HIGH);  // 3 LED
      } else {
        digitalWrite(LED_1, HIGH);
        digitalWrite(LED_2, HIGH);
        digitalWrite(LED_3, HIGH);
        digitalWrite(LED_4, HIGH);  // 4 LED
      }

      ledFlashTimer.start(5000, AsyncDelay::MILLIS);
    }
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
    turnOffAllLeds();
    ledFlashTimer.start(0xFFFFFFFF, AsyncDelay::MILLIS);
  }

  if (d.isExpired()) {
    d.start(1000, AsyncDelay::MILLIS);
  }
}
