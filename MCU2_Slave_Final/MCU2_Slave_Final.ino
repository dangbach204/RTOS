/*
 * Robot Car v3 - MCU2 - ESP32-C3
 * CAN:   GPIO2=CTX  GPIO3=CRX  @ 500Kbps
 * Servo: GPIO10
 *
 * Thu vien can cai:
 *   - ESP32Servo (Kevin Harrington)
 */

#include <ESP32Servo.h>
#include "driver/twai.h"

#define CAN_TX_PIN    2
#define CAN_RX_PIN    3
#define SERVO_PIN    10
#define CAN_ID_SERVO  0x100

Servo myServo;
int   servoAngle = 0;

void canInit() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
    (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    Serial.println("[CAN] Install FAILED"); return;
  }
  if (twai_start() != ESP_OK) {
    Serial.println("[CAN] Start FAILED"); return;
  }
  Serial.println("[CAN] OK 500Kbps TX=GPIO2 RX=GPIO3");
}

void canReceiveServo() {
  twai_message_t m;
  if (twai_receive(&m, 0) != ESP_OK) return;
  if (m.identifier == CAN_ID_SERVO && m.data_length_code >= 1) {
    uint8_t angle = m.data[0];
    if (angle == 0 || angle == 180) {
      servoAngle = angle;
      myServo.write(servoAngle);
      Serial.printf("[SERVO] -> %d deg\n", servoAngle);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MCU2 BOOT ===");

  myServo.attach(SERVO_PIN, 500, 2400);
  myServo.write(0);
  Serial.println("[SERVO] Init -> 0 deg");

  canInit();
  Serial.println("=== MCU2 Ready ===");
}

void loop() {
  canReceiveServo();
}
