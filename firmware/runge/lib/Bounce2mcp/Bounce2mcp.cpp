// Please read Bounce2.h for information about the liscence and authors

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#include "Bounce2mcp.h"

#define DEBOUNCED_STATE 0
#define UNSTABLE_STATE  1
#define STATE_CHANGED   3


BounceMcp::BounceMcp()
    : previous_millis(0)
    , interval_millis(10)
    , state(0)
    , pin(0)
{}

void BounceMcp::interval(uint16_t interval_millis)
{
    this->interval_millis = interval_millis;
}

bool BounceMcp::update(bool currentState)
{
    state &= ~_BV(STATE_CHANGED);
    // Ignore everything if we are locked out
    if (millis() - previous_millis >= interval_millis) {
        if ((bool)(state & _BV(DEBOUNCED_STATE)) != currentState) {
            previous_millis = millis();
            state ^= _BV(DEBOUNCED_STATE);
            state |= _BV(STATE_CHANGED);
        }
    }
    return state & _BV(STATE_CHANGED);
}

bool BounceMcp::read()
{
    return state & _BV(DEBOUNCED_STATE);
}

bool BounceMcp::rose()
{
    return ( state & _BV(DEBOUNCED_STATE) ) && ( state & _BV(STATE_CHANGED));
}

bool BounceMcp::fell()
{
    return !( state & _BV(DEBOUNCED_STATE) ) && ( state & _BV(STATE_CHANGED));
}
