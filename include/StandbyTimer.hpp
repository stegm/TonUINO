#pragma once

#include <Arduino.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>

#include "Player.hpp"

class StandbyTimer
{
    public:
        StandbyTimer(MFRC522 &rfid, Mp3Player &player, uint8_t shutdownPin)
            : _rfid(rfid), _player(player), _shutdownPin(shutdownPin)
            {}
        void loop(void);

        void start(unsigned long standbyMillis);
        void stop(void);

    private:
        MFRC522 &_rfid;
        Mp3Player &_player;
        const uint8_t _shutdownPin;
        unsigned long _startTime;
        unsigned long _standbyTime;
};
