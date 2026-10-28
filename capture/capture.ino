// Step 1: record the fog remote's On and Off codes.
// Board: Seeed XIAO RP2040 (Earle Philhower "Raspberry Pi Pico/RP2040" core).
// Open Serial Monitor at 115200, hold the remote ~30 cm from the receiver,
// press On a few times, then Off a few times. Copy the printed values
// into fog_timer.ino.

#include <RCSwitch.h>

const int RX_PIN = D2;  // RX470C DO pin

RCSwitch rx;

void setup() {
  Serial.begin(115200);
  rx.enableReceive(digitalPinToInterrupt(RX_PIN));
  Serial.println("Listening... press the remote buttons.");
}

void loop() {
  if (rx.available()) {
    unsigned long value = rx.getReceivedValue();
    if (value == 0) {
      Serial.println("Got a signal but couldn't decode it.");
    } else {
      Serial.printf("code=%lu  bits=%u  protocol=%u  pulse=%u\n",
                    value,
                    rx.getReceivedBitlength(),
                    rx.getReceivedProtocol(),
                    rx.getReceivedDelay());
    }
    rx.resetAvailable();
  }
}
