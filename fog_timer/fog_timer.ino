// Step 2: timed fog blasts over 433 MHz, with two knobs and an OLED.
// Board: Seeed XIAO RP2040 (Earle Philhower "Raspberry Pi Pico/RP2040" core).
//   Knob 1 (A0): blast length  1 - 20 s
//   Knob 2 (A1): pause length  5 - 180 s
//   Button (D6 to GND, optional): start a blast now
// Knob changes apply immediately, even mid-cycle.

#include <RCSwitch.h>
#include <U8g2lib.h>
#include <Wire.h>

// ---- Paste these from the capture sketch output ----
const unsigned long CODE_ON  = 0;
const unsigned long CODE_OFF = 0;
const unsigned int  BITS     = 24;
const unsigned int  PROTOCOL = 1;
const unsigned int  PULSE_US = 350;
// -----------------------------------------------------

const int POT_BLAST_PIN = A0;  // D0
const int POT_PAUSE_PIN = A1;  // D1
const int TX_PIN        = D3;  // 433 MHz transmitter DAT
const int BUTTON_PIN    = D6;
// OLED uses the default I2C pins: SDA = D4, SCL = D5

const float BLAST_MIN_S = 1,  BLAST_MAX_S = 20;
const float PAUSE_MIN_S = 5,  PAUSE_MAX_S = 180;

// 1.3" modules are usually SH1106. If the picture is shifted or garbled,
// swap to: U8G2_SSD1306_128X64_NONAME_F_HW_I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

RCSwitch tx;
bool fogging = false;
unsigned long phaseStart = 0;
unsigned long lastDraw = 0;

// Averaged read, squared curve so the low end of the knob has finer control.
float readKnob(int pin, float lo, float hi) {
  long sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(pin);
  float x = (sum / 16) / 4095.0;
  return lo + (hi - lo) * x * x;
}

void setFog(bool on) {
  tx.send(on ? CODE_ON : CODE_OFF, BITS);
  fogging = on;
  phaseStart = millis();
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);  // XIAO LED is active-low
  Serial.println(on ? "FOG ON" : "FOG OFF");
}

void draw(float blastS, float pauseS) {
  float phaseS = fogging ? blastS : pauseS;
  float elapsedS = (millis() - phaseStart) / 1000.0;
  float leftS = max(0.0f, phaseS - elapsedS);
  char buf[24];

  oled.clearBuffer();

  oled.setFont(u8g2_font_helvB12_tr);
  oled.drawStr(0, 14, fogging ? "FOGGING" : "WAITING");

  oled.setFont(u8g2_font_logisoso22_tn);
  snprintf(buf, sizeof buf, "%.0f", ceil(leftS));
  oled.drawStr(128 - oled.getStrWidth(buf), 24, buf);

  int barW = (int)(124 * min(1.0f, elapsedS / phaseS));
  oled.drawFrame(0, 30, 128, 10);
  oled.drawBox(2, 32, barW, 6);

  oled.setFont(u8g2_font_6x12_tr);
  snprintf(buf, sizeof buf, "Blast: %.1f s", blastS);
  oled.drawStr(0, 52, buf);
  snprintf(buf, sizeof buf, "Pause: %.0f s", pauseS);
  oled.drawStr(0, 64, buf);

  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);        // 0 - 4095

  oled.begin();

  tx.enableTransmit(TX_PIN);
  tx.setProtocol(PROTOCOL);        // must come before setPulseLength
  tx.setPulseLength(PULSE_US);
  tx.setRepeatTransmit(10);        // repeat each code for reliability

  setFog(false);                   // known state at power-up
}

void loop() {
  float blastS = readKnob(POT_BLAST_PIN, BLAST_MIN_S, BLAST_MAX_S);
  float pauseS = readKnob(POT_PAUSE_PIN, PAUSE_MIN_S, PAUSE_MAX_S);
  unsigned long elapsed = millis() - phaseStart;

  if (!fogging && digitalRead(BUTTON_PIN) == LOW) {
    setFog(true);
  } else if (fogging && elapsed >= blastS * 1000) {
    setFog(false);
  } else if (!fogging && elapsed >= pauseS * 1000) {
    setFog(true);
  }

  if (millis() - lastDraw > 100) {
    lastDraw = millis();
    draw(blastS, pauseS);
  }
  delay(10);
}
