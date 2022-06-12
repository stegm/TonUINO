#pragma once

#include <Arduino.h>
#include <DFMiniMp3.h>
#include <SoftwareSerial.h>



class Mp3Notify;

typedef DFMiniMp3<SoftwareSerial, Mp3Notify> Mp3Player;

class Mp3Notify
{
public:
    static void OnError(uint16_t errorCode);
    static void PrintlnSourceAction(DfMp3_PlaySources source, const char *action);
    static void OnPlayFinished(DfMp3_PlaySources source, uint16_t track);
    static void OnPlaySourceOnline(DfMp3_PlaySources source);
    static void OnPlaySourceInserted(DfMp3_PlaySources source);
    static void OnPlaySourceRemoved(DfMp3_PlaySources source);

    static void RegisterOnPlayFinished(void(*handler)(uint16_t))
    {
        _onPlayFinishedHandler = handler;
    }

private:
    static void(*_onPlayFinishedHandler)(uint16_t);
};

class Player
{
    public:
        Player(uint8_t busyPin, SoftwareSerial serial) 
            : _busyPin(busyPin), _player(serial) 
            {}

        void loop(void) { _player.loop(); }

        Mp3Player &GetMp3Player(void) { return _player; }
        bool waitForTrackToFinish(void);
        bool isPlaying(void) { return !digitalRead(_busyPin); }

        void say(uint16_t track);
    private:
        const uint8_t _busyPin;
        Mp3Player _player;
};

