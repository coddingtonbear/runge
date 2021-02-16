#include <Arduino.h>
#include <U8g2lib.h>
#include <HX711.h>
#include <Adafruit_MCP23017.h>
#include <Rotary.h>
#include <Bounce2mcp.h>
#include <avr/wdt.h>

#define INTERFACE_INTERRUPT_PIN 2

#define INTERFACE_ROTARY_SIG 8
#define INTERFACE_ROTARY_GND 9
#define INTERFACE_ROTARY_SIG_DIR 10

#define INTERFACE_BUTTON_SIG 11
#define INTERFACE_BUTTON_GND 12

#define GRINDER_SIG 8

#define LOAD_CELL_DT 4
#define LOAD_CELL_SCK 3

#define PORTAFILTER_WEIGHT 145

#define STATE_SLEEP 0
#define STATE_GRAMS 1
#define STATE_PRE_CALIBRATE 2
#define STATE_CALIBRATE 3
#define STATE_GRINDING 4
#define STATE_DONE 5

#define MESSAGE_INTERVAL 250

Adafruit_MCP23017 interface;
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C displayCtl(U8G2_R0);
HX711 scale;
Rotary rotary;
BounceMcp button;

uint8_t state = 0;
uint8_t gramsSelected = 13;
float currentWeight = 0;

unsigned long sleepTimeout = 0;

bool rotateLeft = false;
bool rotateRight = false;
bool buttonFell = false;
String messageDisplay = "";

volatile boolean interrupted = false;
volatile uint16_t messageCount = 0;

void interfaceInterruptHandler() {
  interrupted = true;
  EIFR = 0x01;
}

void setup() {
  Serial.begin(9600);
  Serial.println("[Runge (2020-02-16)]");

  wdt_enable(WDTO_4S);

  pinMode(INTERFACE_INTERRUPT_PIN, INPUT);

  messageDisplay.reserve(32);

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

  scale.begin(LOAD_CELL_DT, LOAD_CELL_SCK);
  scale.tare();
}

void handleInterfaceInterrupt() {
  detachInterrupt(digitalPinToInterrupt(INTERFACE_INTERRUPT_PIN));

  uint16_t interfaceStatus = interface.readINTCAPAB();

  uint8_t buttonState = bitRead(interfaceStatus, INTERFACE_BUTTON_SIG);
  uint8_t sig = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG);
  uint8_t sig_dir = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG_DIR);
  uint8_t event = rotary.process(sig, sig_dir);

  Serial.println("Interrupt condition");
  Serial.print("INTCAPAB: ");
  Serial.println(interfaceStatus, BIN);

  button.update(buttonState);

  if (event == DIR_CW) {
    rotateRight = true;
  } else if (event == DIR_CCW) {
    rotateLeft = true;
  }
  if (button.fell()) {
    buttonFell = true;
  }

  Serial.print("Button: ");
  Serial.println(buttonFell);
  Serial.print("Rotate Right: ");
  Serial.println(rotateRight);
  Serial.print("Rotate Left: ");
  Serial.println(rotateLeft);

  interrupted = false;
  attachInterrupt(digitalPinToInterrupt(INTERFACE_INTERRUPT_PIN), interfaceInterruptHandler, FALLING);
}

void setGrinderState(bool enabled) {
  digitalWrite(GRINDER_SIG, !enabled);
}

void updateSleepTimeout(uint8_t seconds = 15) {
  sleepTimeout = millis() + (seconds * 1000);
}

void setState(uint8_t _state) {
  Serial.print("State Change: ");
  Serial.println(_state);
  state = _state;
}

void loop() {
  wdt_reset();

  if (digitalRead(INTERFACE_INTERRUPT_PIN) == LOW) {
    interface.readGPIOAB();
  }

  messageDisplay = "";

  rotateLeft = false;
  rotateRight = false;
  buttonFell = false;

  if(interrupted) {
    handleInterfaceInterrupt();
  }

  if(buttonFell || rotateLeft || rotateRight) {
    updateSleepTimeout();
  }

  if (millis() > sleepTimeout && state != STATE_SLEEP) {
    setState(STATE_SLEEP);
  }
  if (state == STATE_SLEEP) {
    if (buttonFell) {
      setState(STATE_GRAMS);
    }
  } else if (state == STATE_GRAMS) {
    if (rotateRight) {
      gramsSelected++;
    } else if (rotateLeft) {
      gramsSelected--;
    }

    if (gramsSelected < 1) {
      gramsSelected = 1;
    } else if (gramsSelected > 20) {
      gramsSelected = 20;
    }

    if (buttonFell) {
      setState(STATE_PRE_CALIBRATE);
    } 

    messageDisplay = String(gramsSelected) + "g";
  } else if (state == STATE_PRE_CALIBRATE) {
    updateSleepTimeout();
    messageDisplay = "...";
    setState(STATE_CALIBRATE);
  } else if (state == STATE_CALIBRATE) {
    updateSleepTimeout();
    scale.callibrate_scale(PORTAFILTER_WEIGHT, 10);
    scale.tare();
    setState(STATE_GRINDING);
    messageDisplay = ".";
  } else if (state == STATE_GRINDING) {
    updateSleepTimeout();
    if(buttonFell) {
      setState(STATE_GRAMS);
    }

    currentWeight = scale.get_units(5);

    messageDisplay = String(round(currentWeight)) + "/" + String(gramsSelected);

    if (currentWeight > gramsSelected) {
      setState(STATE_DONE);
    }
  } else if (state == STATE_DONE) {
    messageDisplay = "Ready";

    if(buttonFell) {
      setState(STATE_GRAMS);
    }
  } else {
    // Unexpected state
    Serial.print("Unexpected state: ");
    Serial.println(state);
    setState(STATE_GRAMS);
  }

  setGrinderState(state == STATE_GRINDING);

  displayCtl.firstPage();
  do {
    displayCtl.setFont(u8g2_font_logisoso28_tf);
    displayCtl.drawStr(0, 28, messageDisplay.c_str());
  } while(displayCtl.nextPage());
}
