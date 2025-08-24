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

const uint8_t LED_RUN = 16;     // D0 = GPIO16 - externí LED indikace běhu
const uint8_t LED_RED = 12;     // D6 = GPIO12 - červená složka RGB LED (PWM)
const uint8_t LED_GREEN = 13;   // D7 = GPIO13 - zelená složka RGB LED (PWM)
const uint8_t LED_BLUE = 15;    // D8 = GPIO15 - modrá složka RGB LED (PWM)

bool ledState = false;

ICACHE_RAM_ATTR void int2Handler() {
  as3935.interruptHandler();
}

void setRGBColorPWM(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(LED_RED, map(red, 0, 255, 0, 1023));
  analogWrite(LED_GREEN, map(green, 0, 255, 0, 1023));
  analogWrite(LED_BLUE, map(blue, 0, 255, 0, 1023));
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
  pinMode(LED_RUN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // Vypnutí interní LED na GPIO2 (D4)
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  digitalWrite(LED_RUN, LOW);
  setRGBColorPWM(0, 0, 0);

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

      if (dist <= 5) {
        setRGBColorPWM(255, 0, 0);
      } else if (dist <= 15) {
        setRGBColorPWM(255, 165, 0);
      } else {
        setRGBColorPWM(0, 255, 0);
      }
      ledFlashTimer.start(500, AsyncDelay::MILLIS);
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

  if (d.isExpired()) {
    ledState = !ledState;
    digitalWrite(LED_RUN, ledState ? HIGH : LOW);
    d.start(1000, AsyncDelay::MILLIS);
  }

  if (ledFlashTimer.isExpired()) {
    setRGBColorPWM(0, 0, 0);
    ledFlashTimer.start(0xFFFFFFFF, AsyncDelay::MILLIS);
  }
}
