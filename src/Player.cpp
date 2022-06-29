#include "Player.hpp"

void Mp3Notify::OnError(uint16_t errorCode)
{
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print("Com Error ");
    Serial.println(errorCode);
}

void Mp3Notify::PrintlnSourceAction(DfMp3_PlaySources source, const char *action)
{
    if (source & DfMp3_PlaySources_Sd)
        Serial.print("SD Karte ");
    if (source & DfMp3_PlaySources_Usb)
        Serial.print("USB ");
    if (source & DfMp3_PlaySources_Flash)
        Serial.print("Flash ");
    Serial.println(action);
}

void Mp3Notify::OnPlayFinished(DfMp3_PlaySources source, uint16_t track)
{
    PrintlnSourceAction(source, "fertig");
    if (_onPlayFinishedHandler)
    {
        _onPlayFinishedHandler(track);
    }
}

void Mp3Notify::OnPlaySourceOnline(DfMp3_PlaySources source)
{
    PrintlnSourceAction(source, "online");
}

void Mp3Notify::OnPlaySourceInserted(DfMp3_PlaySources source)
{
    PrintlnSourceAction(source, "bereit");
}

void Mp3Notify::OnPlaySourceRemoved(DfMp3_PlaySources source)
{
    PrintlnSourceAction(source, "entfernt");
}

void (*Mp3Notify::_onPlayFinishedHandler)(uint16_t);


void Mp3Player::begin(void)
{
    _player.begin();
}

void Mp3Player::loop(void)
{
    _player.loop();
}

void Mp3Player::play(uint8_t folder, uint16_t track)
{
    _player.playFolderTrack(folder, track);
}

void Mp3Player::playNotification(uint16_t track)
{
    _player.playMp3FolderTrack(track);
}

void Mp3Player::playAdvertisement(uint16_t track)
{
    _player.playAdvertisement(track);
}

void Mp3Player::pause(void)
{
    _player.pause();
}

void Mp3Player::start(void)
{
    _player.start();
}

void Mp3Player::stop(void)
{
    _player.stop();
}

void Mp3Player::waitForTrackToFinish(void)
{
    const unsigned long startTime = millis();
    do
    {
        _player.loop();
    } while (!isPlaying() && (millis() - startTime) < 1000u);
    delay(1000);
    do
    {
        _player.loop();
    } while (isPlaying());
}

uint16_t Mp3Player::getReliableTrackCountForFolder(uint16_t folder)
{
    // Workaround getTotalFolderCount liefert falschen Wert, wenn
    // der Folder nicht vorher angewählt wurde
    _player.playFolderTrack(folder, 1);
    delay(200);
    _player.stop();
    delay(200);

    return _player.getFolderTrackCount(folder);
}
