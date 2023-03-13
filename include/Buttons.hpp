#pragma once

#include <Arduino.h>

#include <JC_Button.h>


#define PIN_PAUSE A0
#define PIN_UP A1
#define PIN_DOWN A2

#define LONG_PRESS 1000


enum ButtonState {
    RELEASED,
    PRESSED,
    PRESSED_LONG_EVENT,
    PRESSED_LONG,
    RELEASED_AFTER_SHORT_PRESS_EVENT,
};


class Buttons
{
    public:
        Buttons(void)
            : _pause(PIN_PAUSE), _up(PIN_UP), _down(PIN_DOWN)
        {};
        void begin(void);
        void loop(void);

        ButtonState getPauseState(void) { return _pauseState; }
        ButtonState getUpState(void) { return _upState; }
        ButtonState getDownState(void) { return _downState; }

        bool factoryReset(void);

    private:
        Button _pause;
        Button _up;
        Button _down;
        ButtonState _pauseState;
        ButtonState _upState;
        ButtonState _downState;

        void _evaluateState(ButtonState state, Button &button);
};
