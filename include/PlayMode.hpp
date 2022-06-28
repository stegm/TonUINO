#pragma once

enum PlayMode
{
    PlayMode_Unitialized = 0,
    // Hörspielmodus: Eine zufällige Datei aus dem Ordner wiedergeben
    PlayMode_RandomTrack = 1,
    // Albummodus: Den kompletten Ordner wiedergeben
    PlayMode_Album = 2,
    // Party Modus: Ordner zufällig wiedergeben
    PlayMode_Party = 3,
    // Einzel Modus: Eine bestimmte Datei im Ordner wiedergeben
    PlayMode_SingleTrack = 4,
    // Hörbuch Modus: Einen Ordner wiedergeben und den Fortschritt speichern
    PlayMode_AudioBook = 5,
    // Admin Funktionen
    PlayMode_Admin = 6,
    // Spezialmodus Von-Bis, Hörspiel: Eine zufällige Datei zwischen der Start und Enddatei wiedergeben
    PlayMode_RandomRange = 7,
    // Spezialmodus Von-Bis, Album: Alle Dateien zwischen der Start und Enddatei wiedergeben
    PlayMode_AlbumRange = 8,
    // Spezialmodus Von-Bis, Party: Alle Dateien zwischen der Start und Enddatei zufällig wiedergeben
    PlayMode_PartyRange = 9,

    PlayMode_AdminMenu = 255,
};