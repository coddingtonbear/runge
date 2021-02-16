#include <Arduino.h>
#include <U8g2lib.h>
#include <HX711.h>
#include <Adafruit_MCP23017.h>
#include <Rotary.h>
#include <Bounce2mcp.h>

#define INTERFACE_INTERRUPT_PIN 2

#define INTERFACE_ROTARY_SIG 8
#define INTERFACE_ROTARY_GND 9
#define INTERFACE_ROTARY_SIG_DIR 10

#define INTERFACE_BUTTON_SIG 11
#define INTERFACE_BUTTON_GND 12

#define LOAD_CELL_DT 4
#define LOAD_CELL_SCK 3

Adafruit_MCP23017 interface;
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C displayCtl(U8G2_R0);
HX711 scale;
Rotary rotary;
BounceMcp button;

volatile boolean interrupted = false;

volatile uint16_t messageCount = 0;

void interfaceInterruptHandler() {
  interrupted = true;
  EIFR = 0x01;
}

void setup() {
  pinMode(INTERFACE_INTERRUPT_PIN, INPUT);

  interface.begin();
  interface.readINTCAPAB(); // Just to clear interrupts
  interface.setupInterrupts(true, false, LOW);

  interface.pinMode(INTERFACE_ROTARY_SIG, INPUT);
  interface.pullUp(INTERFACE_ROTARY_SIG, HIGH);
  interface.setupInterruptPin(INTERFACE_ROTARY_SIG, CHANGE);

  interface.pinMode(INTERFACE_ROTARY_GND, OUTPUT);
  interface.digitalWrite(INTERFACE_ROTARY_GND, LOW);

  interface.pinMode(INTERFACE_ROTARY_SIG_DIR, INPUT);
  interface.pullUp(INTERFACE_ROTARY_SIG_DIR, HIGH);
  interface.setupInterruptPin(INTERFACE_ROTARY_SIG_DIR, CHANGE);

  interface.pinMode(INTERFACE_BUTTON_GND, OUTPUT);
  interface.digitalWrite(INTERFACE_BUTTON_GND, LOW);

  interface.pinMode(INTERFACE_BUTTON_SIG, INPUT);
  interface.pullUp(INTERFACE_BUTTON_SIG, HIGH);
  interface.setupInterruptPin(INTERFACE_BUTTON_SIG, CHANGE);

  displayCtl.begin();

  attachInterrupt(digitalPinToInterrupt(INTERFACE_INTERRUPT_PIN), interfaceInterruptHandler, FALLING);
  //scale.begin(LOAD_CELL_DT, LOAD_CELL_SCK);
  //scale.tare();
  //delay(10000);
  //scale.callibrate_scale(10, 10);
}

void handleInterfaceInterrupt() {
  detachInterrupt(digitalPinToInterrupt(INTERFACE_INTERRUPT_PIN));

  uint16_t interfaceStatus = interface.readINTCAPAB();

  String message;

  displayCtl.firstPage();
  displayCtl.setFont(u8g2_font_logisoso32_tf);

  button.update(bitRead(interfaceStatus, INTERFACE_BUTTON_SIG));
  uint8_t sig = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG);
  uint8_t sig_dir = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG_DIR);
  uint8_t event = rotary.process(sig, sig_dir);

  if (button.fell()) {
    message = "B" + String(messageCount);
  } else if (event == DIR_CW) {
    message = "R" + String(messageCount);
  } else if (event == DIR_CCW) {
    message = "L" + String(messageCount);
  }

  if(message.length() > 0) {
    messageCount++;
    do {
      displayCtl.drawStr(0, 32, message.c_str());
    } while(displayCtl.nextPage());
    //delay(1000);
  }

  interrupted = false;
  attachInterrupt(digitalPinToInterrupt(INTERFACE_INTERRUPT_PIN), interfaceInterruptHandler, FALLING);
}

void loop() {
  if(digitalRead(INTERFACE_INTERRUPT_PIN) == LOW) {
    interface.digitalRead(0);
    interface.digitalRead(8);
  }


  if(interrupted) {
    handleInterfaceInterrupt();
  }

  if(false) {
    float value = scale.get_units(5);

    displayCtl.firstPage();
    do {
      displayCtl.setFont(u8g2_font_logisoso32_tf);
      displayCtl.drawStr(0, 32, String(value).c_str());
    } while(displayCtl.nextPage());
  }
}
