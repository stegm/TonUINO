#pragma once

#include <Arduino.h>
#include <DFMiniMp3.h>
#include <SoftwareSerial.h>

class Mp3Notify;

typedef DFMiniMp3<SoftwareSerial, Mp3Notify> Player;

class Mp3Notify
{
public:
    static void OnError(uint16_t errorCode);
    static void PrintlnSourceAction(DfMp3_PlaySources source, const char *action);
    static void OnPlayFinished(DfMp3_PlaySources source, uint16_t track);
    static void OnPlaySourceOnline(DfMp3_PlaySources source);
    static void OnPlaySourceInserted(DfMp3_PlaySources source);
    static void OnPlaySourceRemoved(DfMp3_PlaySources source);

    static void RegisterOnPlayFinished(void (*handler)(uint16_t))
    {
        _onPlayFinishedHandler = handler;
    }

private:
    static void (*_onPlayFinishedHandler)(uint16_t);
};

class Mp3Player
{
public:
    Mp3Player(SoftwareSerial &serial, uint8_t busyPin)
        : _player(serial), _busyPin(busyPin)
    {
        pinMode(busyPin, INPUT);        
    }

    void begin(void);
    void loop(void);
    void sleep(void) { _player.sleep(); }
    
    void play(uint8_t folder, uint16_t track);
    void playNotification(uint16_t track);
    void playAdvertisement(uint16_t track);
    void pause(void);
    void start(void);
    void stop(void);

    void setEqualizer(DfMp3_Eq equalizer) { _player.setEq(equalizer); }
    void setVolume(uint8_t volume) { _player.setVolume(volume); }
    void increaseVolume(void) { _player.increaseVolume(); }
    void decreaseVolume(void) { _player.decreaseVolume(); }

    bool isPlaying(void) { return !digitalRead(_busyPin); }
    void waitForTrackToFinish(void);

    uint16_t getTrackCountForFolder(uint16_t folder) { return _player.getFolderTrackCount(folder); }
    uint16_t getFolderCountTotal(void) { return _player.getTotalFolderCount(); }
    uint16_t getTotalTrackCount(void) { return _player.getTotalTrackCount(DfMp3_PlaySource_Sd); }
    uint16_t getReliableTrackCountForFolder(uint16_t folder);

    Player& getInst(void) { return _player; }

private:
    Player _player;
    const uint8_t _busyPin;
};
