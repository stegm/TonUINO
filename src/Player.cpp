#include "Player.hpp"


void Mp3Notify::OnError(uint16_t errorCode) {
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print("Com Error ");
    Serial.println(errorCode);
}

void Mp3Notify::PrintlnSourceAction(DfMp3_PlaySources source, const char* action) {
    if (source & DfMp3_PlaySources_Sd) Serial.print("SD Karte ");
    if (source & DfMp3_PlaySources_Usb) Serial.print("USB ");
    if (source & DfMp3_PlaySources_Flash) Serial.print("Flash ");
    Serial.println(action);
}

void Mp3Notify::OnPlayFinished(DfMp3_PlaySources source, uint16_t track) {
    //      Serial.print("Track beendet");
    //      Serial.println(track);
    //      delay(100);
    if (_onPlayFinishedHandler)
    {
        _onPlayFinishedHandler(track);
    }
}

void Mp3Notify::OnPlaySourceOnline(DfMp3_PlaySources source) {
    PrintlnSourceAction(source, "online");
}

void Mp3Notify::OnPlaySourceInserted(DfMp3_PlaySources source) {
    PrintlnSourceAction(source, "bereit");
}

void Mp3Notify::OnPlaySourceRemoved(DfMp3_PlaySources source) {
    PrintlnSourceAction(source, "entfernt");
}

void(*Mp3Notify::_onPlayFinishedHandler)(uint16_t);

