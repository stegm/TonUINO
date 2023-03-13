#include "Buttons.hpp"


void Buttons::begin(void)
{
  _pause.begin();
}

void Buttons::loop(void)
{
  _evaluateState(_pauseState, _pause);
  _evaluateState(_upState, _up);
  _evaluateState(_downState, _down);
}

bool Buttons::factoryReset(void)
{
    bool reset = false;
    loop();
    if (_pause.isPressed() && _up.isPressed() && _down.isPressed())
    {
        while (_pause.isPressed() || _up.isPressed() || _down.isPressed())
        {
            loop();
            if (_pause.pressedFor(LONG_PRESS) && _up.pressedFor(LONG_PRESS) && _down.pressedFor(LONG_PRESS))
            {
                reset = true;
            }
        }
    }

    return reset;
}

void Buttons::_evaluateState(ButtonState state, Button &button)
{
    button.read();

    switch (state)
    {
        case RELEASED:
            if (button.isPressed())
            {
                state = PRESSED;
            }
            break;

        case PRESSED:
            if (button.pressedFor(LONG_PRESS))
            {
                state = PRESSED_LONG_EVENT;
            }
            else if (button.isReleased())
            {
                state = RELEASED_AFTER_SHORT_PRESS_EVENT;
            }
            break;

        case PRESSED_LONG_EVENT:
            state = button.isPressed() ? PRESSED_LONG : RELEASED;
            break;

        case PRESSED_LONG:
            if (button.isReleased())
            {
                state = RELEASED;
            }
            break;

        case RELEASED_AFTER_SHORT_PRESS_EVENT:
            state = button.isReleased() ? RELEASED : PRESSED;
            break;
    }
}