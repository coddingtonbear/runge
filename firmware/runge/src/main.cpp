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
#define STATE_LOCKOUT 4

#define MESSAGE_INTERVAL 250
#define GRINDER_SAFETY_LOCKOUT 30000

#define DEFAULT_SECONDS 10

#define SAVED_SECONDS_LOCATION 20

const char version[] = "v2021-05-22";

Adafruit_MCP23017 interface;
U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C displayCtl(U8G2_R0);
Rotary rotary;
BounceMcp button;

uint16_t tempInterface = 0;

uint8_t state = 0;
uint8_t secondsSelected = 0;

unsigned long resetAfterTimeout = (60UL * 60UL * 20UL) * 1000UL;

unsigned long grinderTimeout = 0;
unsigned long sleepTimeout = 0;
unsigned long grinderStart = 0;

bool rotateLeft = false;
bool rotateRight = false;
bool buttonFell = false;
String lastMessageDisplay = "";
String messageDisplay = "";

volatile uint16_t messageCount = 0;

void setup() {
  wdt_reset();
  wdt_enable(WDTO_4S);

  digitalWrite(GRINDER_SIG, HIGH);
  pinMode(GRINDER_SIG, OUTPUT);

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

  Serial.begin(9600);
  Serial.print("[Runge ");
  Serial.print(version);
  Serial.println("]");

  displayCtl.begin();

  displayCtl.firstPage();
  do {
    displayCtl.setFont(u8g2_font_luRS12_tf);
    displayCtl.drawStr(0, 14, "Runge");
    displayCtl.drawStr(0, 32, version);
  } while(displayCtl.nextPage());
  delay(1000);
  lastMessageDisplay = "Clear me";
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

  if(interfaceStatus != tempInterface) {
    displayCtl.firstPage();
    do {
      displayCtl.setFont(u8g2_font_luRS12_tf);
      displayCtl.drawStr(0, 14, String(interfaceStatus & 0xff, BIN).c_str());
      displayCtl.drawStr(0, 32, String(interfaceStatus >> 8, BIN).c_str());
    } while(displayCtl.nextPage());
    delay(100);
  }

  tempInterface = interfaceStatus;

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
  unsigned long now = millis();

  messageDisplay = "";

  rotateLeft = false;
  rotateRight = false;
  buttonFell = false;

  bool forceDisplay = false;

  handleInterface();

  // Sleep cycle handler
  if(buttonFell || rotateLeft || rotateRight) {
    updateSleepTimeout();
  } else if (
    (now > sleepTimeout)
    && (state != STATE_SLEEP)
    && (state != STATE_LOCKOUT)
  ) {
    setState(STATE_SLEEP);
  }

  // Sanity checks
  if (!interface.ping()) {
    Serial.print("Could not connect to controller!");
    messageDisplay = "ERR: IfcP";
    setState(STATE_LOCKOUT);
    forceDisplay = true;
  } else if (
    (state == STATE_GRINDING)
    && ((now - grinderStart) > GRINDER_SAFETY_LOCKOUT)
  ) {
    Serial.print("Grinder safety lockout!");
    messageDisplay = "ERR: GndT";
    forceDisplay = true;
    setState(STATE_LOCKOUT);
  }

  // State handler
  if (state == STATE_SLEEP) {
    if (buttonFell || rotateLeft || rotateRight) {
      setState(STATE_TIME);
    } else {
      // If we've been up for a while, and nothing's going on --
      // let's reset to make sure our values are reset.
      if (now > resetAfterTimeout) {
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
    } else if (secondsSelected > 20) {
      secondsSelected = 20;
    }

    if (buttonFell) {
      grinderStart = millis();
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
      grinderTimeout = now + (secondsSelected * 1000);
    }

    unsigned long millisRemaining = 0;
    if (now < grinderTimeout) {
      millisRemaining = grinderTimeout - now;
    }

    messageDisplay = String(round(millisRemaining / 1000) + 1) + "/" + String(secondsSelected) + "s";

    if (now > grinderTimeout) {
      setState(STATE_DONE);
    }
  } else if (state == STATE_DONE) {
    messageDisplay = "Ready";
    grinderTimeout = 0;

    if(buttonFell || rotateLeft || rotateRight) {
      setState(STATE_TIME);
    }
  } else if (state == STATE_LOCKOUT) {
    // To make sure the loop in this case isn't essentially instant,
    // let's delay a bit.  This'll also allow the screen a chance to
    // settle before we begin re-rendering it.
    delay(500);
  } else {
    // Unexpected state
    Serial.print("Unexpected state: ");
    Serial.println(state);
    setState(STATE_TIME);
  }

  setGrinderState(state == STATE_GRINDING);

  /*
  if(forceDisplay || (lastMessageDisplay != messageDisplay)) {
    displayCtl.firstPage();
    do {
      displayCtl.setFont(u8g2_font_luRS24_tf);
      displayCtl.drawStr(0, 28, messageDisplay.c_str());
    } while(displayCtl.nextPage());
    
    lastMessageDisplay = messageDisplay;
  }
  */
}
