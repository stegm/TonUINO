#include "StandbyTimer.hpp"

#include <avr/sleep.h>

void StandbyTimer::loop(void)
{
    if (_standbyTime && ((millis() - _startTime) > _standbyTime))
    {
        Serial.println(F("=== power off!"));
        // enter sleep state
        digitalWrite(_shutdownPin, HIGH);
        delay(500);

        // http://discourse.voss.earth/t/intenso-s10000-powerbank-automatische-abschaltung-software-only/805
        // powerdown to 27mA (powerbank switches off after 30-60s)
        _rfid.PCD_AntennaOff();
        _rfid.PCD_SoftPowerDown();
        _player.sleep();

        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        cli(); // Disable interrupts
        sleep_mode();
    }
}

void StandbyTimer::start(unsigned long standbyMillis)
{
    Serial.println(F("=== setstandbyTimer()"));
    Serial.println(standbyMillis);

    if (standbyMillis != 0)
    {
        _startTime = millis();
        _standbyTime = standbyMillis;
    }
    else
    {
        _standbyTime = 0;
    }
}

void StandbyTimer::stop(void)
{
    Serial.println(F("=== disablestandby()"));
    _standbyTime = 0;
}
