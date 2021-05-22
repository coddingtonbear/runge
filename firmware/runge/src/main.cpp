#include <Arduino.h>
#include <U8g2lib.h>
#include <Adafruit_MCP23017.h>
#include <Rotary.h>
#include <Bounce2mcp.h>
#include <avr/wdt.h>
#include <Atmega328Pins.h>
#include <EEPROM.h>

#define INTERFACE_ROTARY_SIG 8
#define INTERFACE_ROTARY_GND 9
#define INTERFACE_ROTARY_SIG_DIR 10

#define INTERFACE_BUTTON_SIG 11
#define INTERFACE_BUTTON_GND 12

#define GRINDER_SIG PIN_PB0

#define PORTAFILTER_WEIGHT 171

#define STATE_SLEEP 0
#define STATE_TIME 1
#define STATE_GRINDING 2
#define STATE_DONE 3

#define MESSAGE_INTERVAL 250

#define DEFAULT_SECONDS 10

#define SAVED_SECONDS_LOCATION 20

Adafruit_MCP23017 interface;
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C displayCtl(U8G2_R0);
Rotary rotary;
BounceMcp button;

uint8_t state = 0;
uint8_t secondsSelected = 0;

unsigned long resetAfterTimeout = (2^32) / 2;

unsigned long grinderTimeout = 0;
unsigned long sleepTimeout = 0;

bool rotateLeft = false;
bool rotateRight = false;
bool buttonFell = false;
String lastMessageDisplay = "";
String messageDisplay = "";

volatile uint16_t messageCount = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("[Runge (2021-03-21)]");

  digitalWrite(GRINDER_SIG, HIGH);
  pinMode(GRINDER_SIG, OUTPUT);

  wdt_enable(WDTO_4S);

  lastMessageDisplay.reserve(32);
  messageDisplay.reserve(32);

  interface.begin();

  interface.pinMode(INTERFACE_ROTARY_SIG, INPUT);
  interface.pullUp(INTERFACE_ROTARY_SIG, HIGH);

  interface.pinMode(INTERFACE_ROTARY_GND, OUTPUT);
  interface.digitalWrite(INTERFACE_ROTARY_GND, LOW);

  interface.pinMode(INTERFACE_ROTARY_SIG_DIR, INPUT);
  interface.pullUp(INTERFACE_ROTARY_SIG_DIR, HIGH);

  interface.pinMode(INTERFACE_BUTTON_GND, OUTPUT);
  interface.digitalWrite(INTERFACE_BUTTON_GND, LOW);

  interface.pinMode(INTERFACE_BUTTON_SIG, INPUT);
  interface.pullUp(INTERFACE_BUTTON_SIG, HIGH);

  displayCtl.begin();
}

void setSavedSeconds(uint8_t value) {
  uint8_t saved = EEPROM.read(SAVED_SECONDS_LOCATION);
  if (saved != value) {
    EEPROM.write(SAVED_SECONDS_LOCATION, value);
  }
}

uint8_t getSavedSeconds() {
  uint8_t value = EEPROM.read(SAVED_SECONDS_LOCATION);

  if(value == 255) {
    setSavedSeconds(DEFAULT_SECONDS);
    return DEFAULT_SECONDS;
  }

  return value;
}

void (*resetNow)(void) = 0;

void handleInterface() {
  uint16_t interfaceStatus = interface.readGPIOAB();

  uint8_t buttonState = bitRead(interfaceStatus, INTERFACE_BUTTON_SIG);
  uint8_t sig = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG);
  uint8_t sig_dir = bitRead(interfaceStatus, INTERFACE_ROTARY_SIG_DIR);
  uint8_t event = rotary.process(sig, sig_dir);

  button.update(buttonState);

  if (event == DIR_CW) {
    rotateRight = true;
  } else if (event == DIR_CCW) {
    rotateLeft = true;
  }
  if (button.fell()) {
    buttonFell = true;
  }
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

  messageDisplay = "";

  rotateLeft = false;
  rotateRight = false;
  buttonFell = false;

  handleInterface();

  if(buttonFell || rotateLeft || rotateRight) {
    updateSleepTimeout();
  }

  if (millis() > sleepTimeout && state != STATE_SLEEP) {
    setState(STATE_SLEEP);
  }
  if (state == STATE_SLEEP) {
    if (buttonFell || rotateLeft || rotateRight) {
      setState(STATE_TIME);
    } else {
      // If we've been up for a while, and nothing's going on --
      // let's reset to make sure our values are reset.
      if (millis() > resetAfterTimeout) {
        resetNow();
      }
    }
  } else if (state == STATE_TIME) {
    if (secondsSelected == 0) {
      secondsSelected = getSavedSeconds();
    }
    if (rotateRight) {
      secondsSelected++;
    } else if (rotateLeft) {
      secondsSelected--;
    }

    if (secondsSelected < 1) {
      secondsSelected = 1;
    } else if (secondsSelected > 60) {
      secondsSelected = 60;
    }

    if (buttonFell) {
      setSavedSeconds(secondsSelected);
      setState(STATE_GRINDING);
    } 

    messageDisplay = String(secondsSelected) + "s";
  } else if (state == STATE_GRINDING) {
    updateSleepTimeout();
    if(buttonFell) {
      setState(STATE_DONE);
    }
    if(grinderTimeout == 0) {
      grinderTimeout = millis() + (secondsSelected * 1000);
    }

    unsigned long secondsRemaining = grinderTimeout - millis();

    messageDisplay = String(round(secondsRemaining / 1000)) + "/" + String(secondsSelected) + "s";

    if (millis() > grinderTimeout) {
      setState(STATE_DONE);
    }
  } else if (state == STATE_DONE) {
    messageDisplay = "Ready";
    grinderTimeout = 0;

    if(buttonFell || rotateLeft || rotateRight) {
      setState(STATE_TIME);
    }
  } else {
    // Unexpected state
    Serial.print("Unexpected state: ");
    Serial.println(state);
    setState(STATE_TIME);
  }

  setGrinderState(state == STATE_GRINDING);

  if(lastMessageDisplay != messageDisplay) {
    displayCtl.firstPage();
    do {
      displayCtl.setFont(u8g2_font_logisoso28_tf);
      displayCtl.drawStr(0, 28, messageDisplay.c_str());
    } while(displayCtl.nextPage());
    
    lastMessageDisplay = messageDisplay;
  }
}
