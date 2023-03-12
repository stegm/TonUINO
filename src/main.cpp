#include <Arduino.h>

#include <ArduinoLog.h>

#include "Types.hpp"
#include "Settings.hpp"
#include "Player.hpp"
#include "StandbyTimer.hpp"
#include "Tracks.hpp"
#include "PlayMode.hpp"

#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <stdbool.h>

/*
   _____         _____ _____ _____ _____
  |_   _|___ ___|  |  |     |   | |     |
    | | | . |   |  |  |-   -| | | |  |  |
    |_| |___|_|_|_____|_____|_|___|_____|
    TonUINO Version 2.1

    created by Thorsten Voß and licensed under GNU/GPL.
    Information and contribution at https://tonuino.de.
*/

// uncomment the below line to enable five button support
//#define FIVEBUTTONS

static const uint32_t cardCookie = 322417479;

uint16_t numTracksInFolder;
uint16_t currentTrack;
uint16_t firstTrack;
uint8_t queue[255];
uint8_t volume;

// this object stores nfc tag data
struct nfcTagObject
{
  uint32_t cookie;
  uint8_t version;
  FolderSettings nfcFolderSettings;
  //  uint8_t folder;
  //  uint8_t mode;
  //  uint8_t special;
  //  uint8_t special2;
};

#define ADMIN_MENU_LOCKED_CARD 1u
#define ADMIN_MENU_LOCKED_PIN 2u
#define ADMIN_MENU_LOCKED_CHECK 3u

nfcTagObject myCard;
FolderSettings *myFolder;
static uint16_t _lastTrackFinished;

// MFRC522
#define RST_PIN 9                 // Configurable, see typical pin layout above
#define SS_PIN 10                 // Configurable, see typical pin layout above
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522
MFRC522::MIFARE_Key key;
bool successRead;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
MFRC522::StatusCode status;

#define buttonPause A0
#define buttonUp A1
#define buttonDown A2
#define busyPin 4
#define shutdownPin 7
#define openAnalogPin A7

#ifdef FIVEBUTTONS
#define buttonFourPin A3
#define buttonFivePin A4
#endif

#define LONG_PRESS 1000

Button pauseButton(buttonPause);
Button upButton(buttonUp);
Button downButton(buttonDown);
#ifdef FIVEBUTTONS
Button buttonFour(buttonFourPin);
Button buttonFive(buttonFivePin);
#endif
bool ignorePauseButton = false;
bool ignoreUpButton = false;
bool ignoreDownButton = false;
#ifdef FIVEBUTTONS
bool ignoreButtonFour = false;
bool ignoreButtonFive = false;
#endif

SoftwareSerial playerSerial(2, 3); // RX, TX
Mp3Player player(playerSerial, busyPin);
StandbyTimer standby(mfrc522, player, shutdownPin);

static void onTrackFinished(uint16_t track);
uint8_t voiceMenu(uint8_t numberOfOptions, uint16_t startMessage, uint16_t messageOffset,
                  bool preview = false, uint8_t previewFromFolder = 0, uint8_t defaultValue = 0u, bool exitWithLongPress = false);
void writeCard(nfcTagObject nfcTag);
void dump_byte_array(byte *buffer, byte bufferSize);
void adminMenu(bool fromCard = false);
void playFolder();
bool playShortCut(uint8_t shortCut);
bool readCard(nfcTagObject *nfcTag);
void setupCard();
bool askCode(uint8_t *code, size_t digit);
void resetCard();
bool setupFolder(FolderSettings *theFolder);
bool knownCard = false;

void shuffleQueue()
{
  // Queue für die Zufallswiedergabe erstellen
  for (uint8_t x = 0; x < numTracksInFolder - firstTrack + 1; x++)
    queue[x] = x + firstTrack;
  // Rest mit 0 auffüllen
  for (uint8_t x = numTracksInFolder - firstTrack + 1; x < 255; x++)
    queue[x] = 0;
  // Queue mischen
  for (uint8_t i = 0; i < numTracksInFolder - firstTrack + 1; i++)
  {
    uint8_t j = random(0, numTracksInFolder - firstTrack + 1);
    uint8_t t = queue[i];
    queue[i] = queue[j];
    queue[j] = t;
  }
  /*  Serial.println(F("Queue :"));
    for (uint8_t x = 0; x < numTracksInFolder - firstTrack + 1 ; x++)
      Serial.println(queue[x]);
  */
}

class Modifier
{
public:
  virtual void loop() {}
  virtual bool handlePause()
  {
    return false;
  }
  virtual bool handleNext()
  {
    return false;
  }
  virtual bool handlePrevious()
  {
    return false;
  }
  virtual bool handleNextButton()
  {
    return false;
  }
  virtual bool handlePreviousButton()
  {
    return false;
  }
  virtual bool handleVolumeUp()
  {
    return false;
  }
  virtual bool handleVolumeDown()
  {
    return false;
  }
  virtual bool handleRFID(nfcTagObject *newCard)
  {
    return false;
  }
  virtual uint8_t getActive()
  {
    return 0;
  }
  Modifier()
  {
  }
};

Modifier *activeModifier = NULL;

class SleepTimer : public Modifier
{
private:
  const unsigned long sleepMillis;
  const unsigned long startMillis;

public:
  SleepTimer(uint8_t minutes)
      : sleepMillis(minutes * 60000u), startMillis(millis())
  {
    Serial.println(F("=== SleepTimer()"));
    Serial.println(minutes);
    //      if (player.isPlaying())
    //        player.playAdvertisement(302);
    //      delay(500);
  }

  void loop()
  {
    if ((millis() - this->sleepMillis) > this->sleepMillis)
    {
      Serial.println(F("=== SleepTimer::loop() -> SLEEP!"));
      player.pause();
      standby.start(mySettings.standbyTimer * 60 * 1000);
      activeModifier = NULL;
      delete this;
    }
  }

  uint8_t getActive()
  {
    Serial.println(F("== SleepTimer::getActive()"));
    return 1;
  }
};

class FreezeDance : public Modifier
{
private:
  unsigned long nextStopAtMillis = 0;
  const uint8_t minSecondsBetweenStops = 5;
  const uint8_t maxSecondsBetweenStops = 30;

  void setNextStopAtMillis()
  {
    uint16_t seconds = random(this->minSecondsBetweenStops, this->maxSecondsBetweenStops + 1);
    Serial.println(F("=== FreezeDance::setNextStopAtMillis()"));
    Serial.println(seconds);
    this->nextStopAtMillis = millis() + seconds * 1000;
  }

public:
  void loop()
  {
    if (this->nextStopAtMillis != 0 && millis() > this->nextStopAtMillis)
    {
      Serial.println(F("== FreezeDance::loop() -> FREEZE!"));
      if (player.isPlaying())
      {
        player.playAdvertisement(301);
        delay(500);
      }
      setNextStopAtMillis();
    }
  }
  FreezeDance(void)
  {
    Serial.println(F("=== FreezeDance()"));
    if (player.isPlaying())
    {
      delay(1000);
      player.playAdvertisement(300);
      delay(500);
    }
    setNextStopAtMillis();
  }
  uint8_t getActive()
  {
    Serial.println(F("== FreezeDance::getActive()"));
    return 2;
  }
};

class Locked : public Modifier
{
public:
  virtual bool handlePause()
  {
    Serial.println(F("== Locked::handlePause() -> LOCKED!"));
    return true;
  }
  virtual bool handleNextButton()
  {
    Serial.println(F("== Locked::handleNextButton() -> LOCKED!"));
    return true;
  }
  virtual bool handlePreviousButton()
  {
    Serial.println(F("== Locked::handlePreviousButton() -> LOCKED!"));
    return true;
  }
  virtual bool handleVolumeUp()
  {
    Serial.println(F("== Locked::handleVolumeUp() -> LOCKED!"));
    return true;
  }
  virtual bool handleVolumeDown()
  {
    Serial.println(F("== Locked::handleVolumeDown() -> LOCKED!"));
    return true;
  }
  virtual bool handleRFID(nfcTagObject *newCard)
  {
    Serial.println(F("== Locked::handleRFID() -> LOCKED!"));
    return true;
  }
  Locked(void)
  {
    Serial.println(F("=== Locked()"));
    //      if (player.isPlaying())
    //        player.playAdvertisement(303);
  }
  uint8_t getActive()
  {
    return 3;
  }
};

class ToddlerMode : public Modifier
{
public:
  virtual bool handlePause()
  {
    Serial.println(F("== ToddlerMode::handlePause() -> LOCKED!"));
    return true;
  }
  virtual bool handleNextButton()
  {
    Serial.println(F("== ToddlerMode::handleNextButton() -> LOCKED!"));
    return true;
  }
  virtual bool handlePreviousButton()
  {
    Serial.println(F("== ToddlerMode::handlePreviousButton() -> LOCKED!"));
    return true;
  }
  virtual bool handleVolumeUp()
  {
    Serial.println(F("== ToddlerMode::handleVolumeUp() -> LOCKED!"));
    return true;
  }
  virtual bool handleVolumeDown()
  {
    Serial.println(F("== ToddlerMode::handleVolumeDown() -> LOCKED!"));
    return true;
  }
  ToddlerMode(void)
  {
    Serial.println(F("=== ToddlerMode()"));
    //      if (player.isPlaying())
    //        player.playAdvertisement(304);
  }
  uint8_t getActive()
  {
    Serial.println(F("== ToddlerMode::getActive()"));
    return 4;
  }
};

class KindergardenMode : public Modifier
{
private:
  nfcTagObject nextCard;
  bool cardQueued = false;

public:
  virtual bool handleNext()
  {
    Serial.println(F("== KindergardenMode::handleNext() -> NEXT"));
    // if (this->nextCard.cookie == cardCookie && this->nextCard.nfcFolderSettings.folder != 0 && this->nextCard.nfcFolderSettings.mode != 0) {
    // myFolder = &this->nextCard.nfcFolderSettings;
    if (this->cardQueued == true)
    {
      this->cardQueued = false;

      myCard = nextCard;
      myFolder = &myCard.nfcFolderSettings;
      Serial.println(myFolder->folder);
      Serial.println(myFolder->mode);
      playFolder();
      return true;
    }
    return false;
  }
  //    virtual bool handlePause()     {
  //      Serial.println(F("== KindergardenMode::handlePause() -> LOCKED!"));
  //      return true;
  //    }
  virtual bool handleNextButton()
  {
    Serial.println(F("== KindergardenMode::handleNextButton() -> LOCKED!"));
    return true;
  }
  virtual bool handlePreviousButton()
  {
    Serial.println(F("== KindergardenMode::handlePreviousButton() -> LOCKED!"));
    return true;
  }
  virtual bool handleRFID(nfcTagObject *newCard)
  { // lot of work to do!
    Serial.println(F("== KindergardenMode::handleRFID() -> queued!"));
    this->nextCard = *newCard;
    this->cardQueued = true;
    if (!player.isPlaying())
    {
      handleNext();
    }
    return true;
  }
  KindergardenMode()
  {
    Serial.println(F("=== KindergardenMode()"));
    //      if (player.isPlaying())
    //        player.playAdvertisement(305);
    //      delay(500);
  }
  uint8_t getActive()
  {
    Serial.println(F("== KindergardenMode::getActive()"));
    return 5;
  }
};

class RepeatSingleModifier : public Modifier
{
public:
  virtual bool handleNext()
  {
    Serial.println(F("== RepeatSingleModifier::handleNext() -> REPEAT CURRENT TRACK"));
    delay(50);
    if (player.isPlaying())
      return true;
    if (myFolder->mode == PlayMode_Party || myFolder->mode == PlayMode_PartyRange)
    {
      player.play(myFolder->folder, queue[currentTrack - 1]);
    }
    else
    {
      player.play(myFolder->folder, currentTrack);
    }
    _lastTrackFinished = 0;
    return true;
  }
  RepeatSingleModifier()
  {
    Serial.println(F("=== RepeatSingleModifier()"));
  }
  uint8_t getActive()
  {
    Serial.println(F("== RepeatSingleModifier::getActive()"));
    return 6;
  }
};

// An modifier can also do somethings in addition to the modified action
// by returning false (not handled) at the end
// This simple FeedbackModifier will tell the volume before changing it and
// give some feedback once a RFID card is detected.
class FeedbackModifier : public Modifier
{
public:
  virtual bool handleVolumeDown()
  {
    if (volume > mySettings.minVolume)
    {
      player.playAdvertisement(volume - 1);
    }
    else
    {
      player.playAdvertisement(volume);
    }
    delay(500);
    Serial.println(F("== FeedbackModifier::handleVolumeDown()!"));
    return false;
  }
  virtual bool handleVolumeUp()
  {
    if (volume < mySettings.maxVolume)
    {
      player.playAdvertisement(volume + 1);
    }
    else
    {
      player.playAdvertisement(volume);
    }
    delay(500);
    Serial.println(F("== FeedbackModifier::handleVolumeUp()!"));
    return false;
  }
  virtual bool handleRFID(nfcTagObject *newCard)
  {
    Serial.println(F("== FeedbackModifier::handleRFID()"));
    return false;
  }
};


static void nextTrack()
{
  Serial.println(F("=== nextTrack()"));

  if (myFolder->mode == PlayMode_RandomTrack || myFolder->mode == PlayMode_RandomRange)
  {
    Serial.println(F("Hörspielmodus ist aktiv -> keinen neuen Track spielen"));
    standby.start(mySettings.standbyTimer * 60 * 1000);
    //    mp3.sleep(); // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
  }
  else if (myFolder->mode == PlayMode_Album || myFolder->mode == PlayMode_AlbumRange)
  {
    if (currentTrack < numTracksInFolder)
    {
      currentTrack = currentTrack + 1;
      player.play(myFolder->folder, currentTrack);
      Serial.print(F("Albummodus ist aktiv -> nächster Track: "));
      Serial.println(currentTrack);
    }
    else
    {
      player.stop();
      //      mp3.sleep();   // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
      standby.start(mySettings.standbyTimer * 60 * 1000);
    }
  }
  else if (myFolder->mode == PlayMode_Party || myFolder->mode == PlayMode_PartyRange)
  {
    if (currentTrack != (numTracksInFolder - firstTrack + 1))
    {
      Serial.print(F("Party -> weiter in der Queue "));
      currentTrack++;
    }
    else
    {
      Serial.println(F("Ende der Queue -> beginne von vorne"));
      currentTrack = 1;
      //// Wenn am Ende der Queue neu gemischt werden soll bitte die Zeilen wieder aktivieren
      //     Serial.println(F("Ende der Queue -> mische neu"));
      //     shuffleQueue();
    }
    Serial.println(queue[currentTrack - 1]);
    player.play(myFolder->folder, queue[currentTrack - 1]);
  }
  else if (myFolder->mode == PlayMode_SingleTrack)
  {
    Serial.println(F("Einzel Modus aktiv -> Strom sparen"));
    //    mp3.sleep();      // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
    standby.start(mySettings.standbyTimer * 60 * 1000);
  }
  else if (myFolder->mode == PlayMode_AudioBook)
  {
    if (currentTrack <= numTracksInFolder)
    {
      currentTrack = currentTrack + 1;
      Serial.print(F("Hörbuch Modus ist aktiv -> nächster Track und "
                     "Fortschritt speichern: "));
      Serial.println(currentTrack);
      player.play(myFolder->folder, currentTrack);
      // Fortschritt im EEPROM abspeichern
      EEPROM.update(myFolder->folder, currentTrack);
    }
    else
    {
      //      mp3.sleep();  // Je nach Modul kommt es nicht mehr zurück aus dem Sleep!
      // Fortschritt zurück setzen
      EEPROM.update(myFolder->folder, 1);
      standby.start(mySettings.standbyTimer * 60 * 1000);
    }
  }
  delay(500);
}

static void previousTrack()
{
  Serial.println(F("=== previousTrack()"));

  switch (myFolder->mode)
  {
    case PlayMode_RandomTrack:
    case PlayMode_RandomRange:
      // TODO
      // Serial.println(F("Hörspielmodus ist aktiv -> Track von vorne spielen"));
      // mp3.playFolderTrack(myCard.folder, currentTrack);
      break;

    case PlayMode_Album:
    case PlayMode_AlbumRange:
      Serial.println(F("Albummodus ist aktiv -> vorheriger Track"));
      if (currentTrack > firstTrack)
      {
        currentTrack = currentTrack - 1;
      }
      player.play(myFolder->folder, currentTrack);
      break;

    case PlayMode_Party:
    case PlayMode_PartyRange:
      if (currentTrack != 1)
      {
        Serial.print(F("Party Modus ist aktiv -> zurück in der Qeueue "));
        currentTrack--;
      }
      else
      {
        Serial.print(F("Anfang der Queue -> springe ans Ende "));
        currentTrack = numTracksInFolder;
      }
      Serial.println(queue[currentTrack - 1]);
      player.play(myFolder->folder, queue[currentTrack - 1]);
      break;

    case PlayMode_SingleTrack:
      Serial.println(F("Einzel Modus aktiv -> Track von vorne spielen"));
      player.play(myFolder->folder, currentTrack);
      break;

    case PlayMode_AudioBook:
      Serial.println(F("Hörbuch Modus ist aktiv -> vorheriger Track und "
                      "Fortschritt speichern"));
      if (currentTrack != 1)
      {
        currentTrack = currentTrack - 1;
      }
      player.play(myFolder->folder, currentTrack);
      // Fortschritt im EEPROM abspeichern
      EEPROM.update(myFolder->folder, currentTrack);    
      break;

    default:
      break;
  }

  delay(500);
}

// Leider kann das Modul selbst keine Queue abspielen, daher müssen wir selbst die Queue verwalten
static void onTrackFinished(uint16_t track)
{

  Serial.print(F("=== onTrackFinished("));
  Serial.print(track);
  Serial.println(F(")"));

  // TODO rename handleNext
  if (activeModifier != NULL && activeModifier->handleNext())
    return;

  if (track == _lastTrackFinished)
    return;

  _lastTrackFinished = track;

  // Wenn eine neue Karte angelernt wird soll das Ende eines Tracks nicht
  // verarbeitet werden
  if (!knownCard)
    return;

  nextTrack();
}

void setup()
{

  Serial.begin(115200);
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);

  // Wert für randomSeed() erzeugen durch das mehrfache Sammeln von rauschenden LSBs eines offenen Analogeingangs
  uint32_t ADC_LSB;
  uint32_t ADCSeed;
  for (uint8_t i = 0; i < 128; i++)
  {
    ADC_LSB = analogRead(openAnalogPin) & 0x1;
    ADCSeed ^= ADC_LSB << (i % 32);
  }
  // Zufallsgenerator initialisieren
  randomSeed(ADCSeed); 

  // Dieser Hinweis darf nicht entfernt werden
  Serial.println(F("\n _____         _____ _____ _____ _____"));
  Serial.println(F("|_   _|___ ___|  |  |     |   | |     |"));
  Serial.println(F("  | | | . |   |  |  |-   -| | | |  |  |"));
  Serial.println(F("  |_| |___|_|_|_____|_____|_|___|_____|\n"));
  Serial.println(F("TonUINO Version 2.1"));
  Serial.println(F("created by Thorsten Voß and licensed under GNU/GPL."));
  Serial.println(F("Information and contribution at https://tonuino.de.\n"));

  // load Settings from EEPROM
  loadSettingsFromFlash(cardCookie, myFolder);

  // activate standby timer
  standby.start(mySettings.standbyTimer * 60 * 1000);

  // DFPlayer Mini initialisieren
  Mp3Notify::RegisterOnPlayFinished(onTrackFinished);
  player.begin();
  // Zwei Sekunden warten bis der DFPlayer Mini initialisiert ist
  delay(2000);
  volume = mySettings.initVolume;
  player.setVolume(volume);
  player.setEqualizer((DfMp3_Eq)(mySettings.eq - 1));
  // Fix für das Problem mit dem Timeout (ist jetzt in Upstream daher nicht mehr nötig!)
  // mySoftwareSerial.setTimeout(10000);

  // NFC Leser initialisieren
  SPI.begin();                       // Init SPI bus
  mfrc522.PCD_Init();                // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
  for (byte i = 0; i < 6; i++)
  {
    key.keyByte[i] = 0xFF;
  }

  pinMode(buttonPause, INPUT_PULLUP);
  pinMode(buttonUp, INPUT_PULLUP);
  pinMode(buttonDown, INPUT_PULLUP);
#ifdef FIVEBUTTONS
  pinMode(buttonFourPin, INPUT_PULLUP);
  pinMode(buttonFivePin, INPUT_PULLUP);
#endif
  pinMode(shutdownPin, OUTPUT);
  digitalWrite(shutdownPin, LOW);

  // RESET --- ALLE DREI KNÖPFE BEIM STARTEN GEDRÜCKT HALTEN -> alle EINSTELLUNGEN werden gelöscht
  if (digitalRead(buttonPause) == LOW && digitalRead(buttonUp) == LOW &&
      digitalRead(buttonDown) == LOW)
  {
    Log.warning(F("Reset -> EEPROM wird gelöscht" CR));
    for (uint16_t i = 0; i < EEPROM.length(); i++)
    {
      EEPROM.update(i, 0u);
    }

    loadSettingsFromFlash(cardCookie, myFolder);
  }

  // Start Shortcut "at Startup" - e.g. Welcome Sound
  if (!playShortCut(3)) {
    player.playNotification(TRACK_STARTUP);
  }

  Log.info(F("Startup finished" CR));
}

void readButtons()
{
  pauseButton.read();
  upButton.read();
  downButton.read();
#ifdef FIVEBUTTONS
  buttonFour.read();
  buttonFive.read();
#endif
}

void volumeUpButton()
{
  if (activeModifier != NULL)
    if (activeModifier->handleVolumeUp())
      return;

    if (volume < mySettings.maxVolume)
  {
    player.increaseVolume();
    volume++;
  }

  Log.info(F("Volume up to %u" CR) , volume);
}

void volumeDownButton()
{
  if (activeModifier != NULL)
    if (activeModifier->handleVolumeDown())
      return;

  if (volume > mySettings.minVolume)
  {
    player.decreaseVolume();
    volume--;
  }

  Log.info(F("Volume down to %u" CR) , volume);
}

void nextButton()
{
  if (activeModifier != NULL)
    if (activeModifier->handleNextButton())
      return;

  nextTrack();
}

void previousButton()
{
  if (activeModifier != NULL)
    if (activeModifier->handlePreviousButton())
      return;

  previousTrack();
}

void playFolder()
{
  Log.info(F("play folder" CR));

  standby.stop();
  knownCard = true;
  _lastTrackFinished = 0u;

  numTracksInFolder = player.getReliableTrackCountForFolder(myFolder->folder);
  firstTrack = 1u;

  Log.notice(F("%u file in folder %u"), numTracksInFolder, myFolder->folder);

  // Hörspielmodus: eine zufällige Datei aus dem Ordner
  if (myFolder->mode == 1)
  {
    Log.info(F("Hörspielmodus -> play random track in folder" CR));
    currentTrack = random(1, numTracksInFolder + 1);
    Log.notice(F("random track is %u" CR), currentTrack);
    player.play(myFolder->folder, currentTrack);
  }

  // Album Modus: kompletten Ordner spielen
  if (myFolder->mode == 2)
  {
    Log.info(F("Album Modus -> play complete folder." CR));
    currentTrack = 1u;
    player.play(myFolder->folder, currentTrack);
  }

  // Party Modus: Ordner in zufälliger Reihenfolge
  if (myFolder->mode == 3)
  {
    Log.info(F("Party Modus -> play folder in random order" CR));
    shuffleQueue();
    currentTrack = 1u;
    player.play(myFolder->folder, queue[currentTrack - 1]);
  }

  // Einzel Modus: eine Datei aus dem Ordner abspielen
  if (myFolder->mode == 4)
  {
    Log.info(F("Einzel Modus -> play a single track in an folder" CR));
    currentTrack = myFolder->special;
    player.play(myFolder->folder, currentTrack);
  }

  // Hörbuch Modus: kompletten Ordner spielen und Fortschritt merken
  if (myFolder->mode == 5)
  {
    Log.info(F("Hörbuch Modus -> play complete folder and save progress" CR));
    currentTrack = EEPROM.read(myFolder->folder);
    if (currentTrack == 0 || currentTrack > numTracksInFolder)
    {
      Log.notice(F("Start at track 1" CR));
      currentTrack = 1u;
    }
    player.play(myFolder->folder, currentTrack);
  }

  // Spezialmodus Von-Bin: Hörspiel: eine zufällige Datei aus dem Ordner
  if (myFolder->mode == 7)
  {
    Log.info(F("Spezialmodus von %u bis %u Hörspiel -> play random track" CR), myFolder->special, myFolder->special2);
    numTracksInFolder = myFolder->special2;
    currentTrack = random(myFolder->special, numTracksInFolder + 1u);

    Log.notice(F("Play track %u" CR), currentTrack);
    player.play(myFolder->folder, currentTrack);
  }

  // Spezialmodus Von-Bis: Album: alle Dateien zwischen Start und Ende spielen
  if (myFolder->mode == 8)
  {
    Log.info(F("Spezialmodus von %u bis %u Album -> play all tracks between start and end" CR), myFolder->special, myFolder->special2);
    numTracksInFolder = myFolder->special2;
    currentTrack = myFolder->special;
    player.play(myFolder->folder, currentTrack);
  }

  // Spezialmodus Von-Bis: Party Ordner in zufälliger Reihenfolge
  if (myFolder->mode == 9)
  {
    Log.info(F("Spezialmodus von %u bis %u Party -> play folder in random order" CR), myFolder->special, myFolder->special2);
    firstTrack = myFolder->special;
    numTracksInFolder = myFolder->special2;
    shuffleQueue();
    currentTrack = 1u;
    player.play(myFolder->folder, queue[currentTrack - 1u]);
  }
}

bool playShortCut(uint8_t shortCut)
{
  Log.info(F("Play shortcut %u" CR), shortCut);

  if (mySettings.shortCuts[shortCut].folder != 0 && mySettings.shortCuts[shortCut].folder != 255)
  {
    myFolder = &mySettings.shortCuts[shortCut];
    playFolder();
    standby.stop();
    delay(1000);
    return true;
  }
  else
  {
    Log.warning(F("Shortcut %u not configured" CR), shortCut);
    return false;
  }
}

void loop()
{
  do
  {
    standby.loop();
    player.loop();

    // Modifier : WIP!
    if (activeModifier != NULL)
    {
      activeModifier->loop();
    }

    // Buttons werden nun über JS_Button gehandelt, dadurch kann jede Taste
    // doppelt belegt werden
    readButtons();

    // admin menu
    if ((pauseButton.pressedFor(LONG_PRESS) || upButton.pressedFor(LONG_PRESS) || downButton.pressedFor(LONG_PRESS)) 
        && pauseButton.isPressed() && upButton.isPressed() && downButton.isPressed())
    {
      player.pause();
      do
      {
        readButtons();
      } while (pauseButton.isPressed() || upButton.isPressed() || downButton.isPressed());

      adminMenu();
      break;
    }

    if (pauseButton.wasReleased())
    {
      if (activeModifier != NULL)
        if (activeModifier->handlePause() == true)
          return;
      if (!ignorePauseButton)
      {
        if (player.isPlaying())
        {
          player.pause();
          standby.start(mySettings.standbyTimer * 60 * 1000);
        }
        else if (knownCard)
        {
          player.start();
          standby.stop();
        }
      }
      ignorePauseButton = false;
    }
    else if (pauseButton.pressedFor(LONG_PRESS) && !ignorePauseButton)
    {
      if (activeModifier != NULL)
        if (activeModifier->handlePause())
          return;
      if (player.isPlaying())
      {
        uint8_t advertTrack;
        if (myFolder->mode == 3 || myFolder->mode == 9)
        {
          advertTrack = (queue[currentTrack - 1]);
        }
        else
        {
          advertTrack = currentTrack;
        }
        // Spezialmodus Von-Bis für Album und Party gibt die Dateinummer relativ zur Startposition wieder
        if (myFolder->mode == 8 || myFolder->mode == 9)
        {
          advertTrack = advertTrack - myFolder->special + 1;
        }
        player.playAdvertisement(advertTrack);
      }
      else
      {
        playShortCut(0);
      }
      ignorePauseButton = true;
    }

    if (upButton.pressedFor(LONG_PRESS) && !ignoreUpButton)
    {
#ifndef FIVEBUTTONS
      if (player.isPlaying())
      {
        if (!mySettings.invertVolumeButtons)
        {
          volumeUpButton();
        }
        else
        {
          nextButton();
        }
      }
      else
      {
        playShortCut(1);
      }
      ignoreUpButton = true;
#endif
    }
    else if (upButton.wasReleased())
    {
      if (!ignoreUpButton)
      {
        if (!mySettings.invertVolumeButtons)
        {
          nextButton();
        }
        else
        {
          volumeUpButton();
        }
      }
      ignoreUpButton = false;
    }

    if (downButton.pressedFor(LONG_PRESS) && !ignoreDownButton)
    {
#ifndef FIVEBUTTONS
      if (player.isPlaying())
      {
        if (!mySettings.invertVolumeButtons)
        {
          volumeDownButton();
        }
        else
        {
          previousButton();
        }
      }
      else
      {
        playShortCut(2);
      }
      ignoreDownButton = true;
#endif
    }
    else if (downButton.wasReleased())
    {
      if (!ignoreDownButton)
      {
        if (!mySettings.invertVolumeButtons)
        {
          previousButton();
        }
        else
        {
          volumeDownButton();
        }
      }
      ignoreDownButton = false;
    }
#ifdef FIVEBUTTONS
    if (buttonFour.wasReleased())
    {
      if (player.isPlaying())
      {
        if (!mySettings.invertVolumeButtons)
        {
          volumeUpButton();
        }
        else
        {
          nextButton();
        }
      }
      else
      {
        playShortCut(1);
      }
    }
    if (buttonFive.wasReleased())
    {
      if (player.isPlaying())
      {
        if (!mySettings.invertVolumeButtons)
        {
          volumeDownButton();
        }
        else
        {
          previousButton();
        }
      }
      else
      {
        playShortCut(2);
      }
    }
#endif
    // Ende der Buttons
  } while (!mfrc522.PICC_IsNewCardPresent());

  // RFID Karte wurde aufgelegt

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  if (readCard(&myCard) == true)
  {
    if (myCard.cookie == cardCookie && myCard.nfcFolderSettings.folder != 0 && myCard.nfcFolderSettings.mode != 0)
    {
      playFolder();
    }

    // Neue Karte konfigurieren
    else if (myCard.cookie != cardCookie)
    {
      knownCard = false;
      player.playNotification(TRACK_NEW_CARD);
      player.waitForTrackToFinish();
      setupCard();
    }
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void adminMenu(bool fromCard)
{
  standby.stop();
  player.pause();

  Log.notice(F("Enter admin menu" CR));

  knownCard = false;
  if (!fromCard)
  {
    if (mySettings.adminMenuLocked == ADMIN_MENU_LOCKED_CARD)
    {
      // Admin menu has been locked - it still can be trigged via admin card
      return;
    }
    else if (mySettings.adminMenuLocked == ADMIN_MENU_LOCKED_PIN)
    {
      // Pin check
      uint8_t pin[ARRAY_SIZE(mySettings.adminMenuPin)];
      player.playNotification(TRACK_INPUT_PIN);
      if (!askCode(pin, ARRAY_SIZE(pin)) || (memcmp(pin, mySettings.adminMenuPin, ARRAY_SIZE(mySettings.adminMenuPin)) != 0))
      {
        return;
      }
    }
    else if (mySettings.adminMenuLocked == ADMIN_MENU_LOCKED_CHECK)
    {
      // Match check
      uint8_t a = random(10, 20);
      uint8_t b = random(1, 10);
      uint8_t c;
      player.playNotification(TRACK_SUM_OF);
      player.waitForTrackToFinish();
      player.playNotification(a);

      if (random(1, 3) == 2)
      {
        // a + b
        c = a + b;
        player.waitForTrackToFinish();
        player.playNotification(TRACK_PLUS);
      }
      else
      {
        // a - b
        b = random(1, a);
        c = a - b;
        player.waitForTrackToFinish();
        player.playNotification(TRACK_MINUS);
      }
      player.waitForTrackToFinish();
      player.playNotification(b);
      Serial.println(c);
      uint8_t temp = voiceMenu(255, 0, 0, false);
      if (temp != c)
      {
        return;
      }
    }
  }

  int subMenu = voiceMenu(12, 900, 900, false, false, 0, true);
  if (subMenu == 0u)
  {
    return;
  }

  if (subMenu == 1u)
  {
    resetCard();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
  else if (subMenu == 2)
  {
    // Maximum Volume
    mySettings.maxVolume = voiceMenu(30 - mySettings.minVolume, 930, mySettings.minVolume, false, false, mySettings.maxVolume - mySettings.minVolume) + mySettings.minVolume;
  }
  else if (subMenu == 3)
  {
    // Minimum Volume
    mySettings.minVolume = voiceMenu(mySettings.maxVolume - 1, 931, 0, false, false, mySettings.minVolume);
  }
  else if (subMenu == 4)
  {
    // Initial Volume
    mySettings.initVolume = voiceMenu(mySettings.maxVolume - mySettings.minVolume + 1, 932, mySettings.minVolume - 1, false, false, mySettings.initVolume - mySettings.minVolume + 1) + mySettings.minVolume - 1;
  }
  else if (subMenu == 5)
  {
    // EQ
    mySettings.eq = voiceMenu(6, 920, 920, false, false, mySettings.eq);
    player.setEqualizer((DfMp3_Eq)(mySettings.eq - 1));
  }
  else if (subMenu == 6)
  {
    // create modifier card
    nfcTagObject tempCard;
    tempCard.cookie = cardCookie;
    tempCard.version = 1;
    tempCard.nfcFolderSettings.folder = 0;
    tempCard.nfcFolderSettings.special = 0;
    tempCard.nfcFolderSettings.special2 = 0;
    tempCard.nfcFolderSettings.mode = (PlayMode)voiceMenu(6, 970, 970, false, false, 0, true);

    if (tempCard.nfcFolderSettings.mode != 0)
    {
      if (tempCard.nfcFolderSettings.mode == 1)
      {
        switch (voiceMenu(4, 960, 960))
        {
        case 1:
          tempCard.nfcFolderSettings.special = 5;
          break;
        case 2:
          tempCard.nfcFolderSettings.special = 15;
          break;
        case 3:
          tempCard.nfcFolderSettings.special = 30;
          break;
        case 4:
          tempCard.nfcFolderSettings.special = 60;
          break;
        }
      }
      player.playNotification(TRACK_PLACE_CARD);
      do
      {
        readButtons();
        if (upButton.wasReleased() || downButton.wasReleased())
        {
          Serial.println(F("Abgebrochen!"));
          player.playNotification(TRACK_CANCELLED);
          return;
        }
      } while (!mfrc522.PICC_IsNewCardPresent());

      // RFID Karte wurde aufgelegt
      if (mfrc522.PICC_ReadCardSerial())
      {
        Serial.println(F("schreibe Karte..."));
        writeCard(tempCard);
        delay(100);
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        player.waitForTrackToFinish();
      }
    }
  }
  else if (subMenu == 7)
  {
    uint8_t shortcut = voiceMenu(4, 940, 940);
    setupFolder(&mySettings.shortCuts[shortcut - 1]);
    player.playNotification(TRACK_CARD_CONFIGURED);
  }
  else if (subMenu == 8u)
  {
    // standby timer
    switch (voiceMenu(5, 960, 960))
    {
    case 1u:
      mySettings.standbyTimer = 5;
      break;
    case 2u:
      mySettings.standbyTimer = 15;
      break;
    case 3u:
      mySettings.standbyTimer = 30;
      break;
    case 4u:
      mySettings.standbyTimer = 60;
      break;
    case 5u:
      mySettings.standbyTimer = 0;
      break;
    }
  }
  else if (subMenu == 9u)
  {
    // Create Cards for Folder
    // Ordner abfragen
    nfcTagObject tempCard;
    tempCard.cookie = cardCookie;
    tempCard.version = 1;
    tempCard.nfcFolderSettings.mode = PlayMode_SingleTrack;
    tempCard.nfcFolderSettings.folder = voiceMenu(99, 301, 0, true);
    uint16_t trackCount = player.getTrackCountForFolder(tempCard.nfcFolderSettings.folder);
    uint8_t special = voiceMenu(trackCount, 321, 0,
                                true, tempCard.nfcFolderSettings.folder);
    uint8_t special2 = voiceMenu(trackCount, 322, 0,
                                 true, tempCard.nfcFolderSettings.folder, special);

    player.playNotification(TRACK_BATCH_CARD_INTRO);
    player.waitForTrackToFinish();
    for (uint8_t x = special; x <= special2; x++)
    {
      player.playNotification(x);
      tempCard.nfcFolderSettings.special = x;
      Serial.print(x);
      Serial.println(F(" Karte auflegen"));
      do
      {
        readButtons();
        if (upButton.wasReleased() || downButton.wasReleased())
        {
          Serial.println(F("Abgebrochen!"));
          player.playNotification(TRACK_CANCELLED);
          return;
        }
      } while (!mfrc522.PICC_IsNewCardPresent());

      // RFID Karte wurde aufgelegt
      if (mfrc522.PICC_ReadCardSerial())
      {
        Serial.println(F("schreibe Karte..."));
        writeCard(tempCard);
        delay(100);
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        player.waitForTrackToFinish();
      }
    }
  }
  else if (subMenu == 10u)
  {
    // Invert Functions for Up/Down Buttons
    int temp = voiceMenu(2, 933, 933, false);
    mySettings.invertVolumeButtons = temp ==2;
  }
  else if (subMenu == 11)
  {
    Serial.println(F("Reset -> EEPROM wird gelöscht"));
    for (uint16_t i = 0; i < EEPROM.length(); i++)
    {
      EEPROM.update(i, 0);
    }
    resetSettings(cardCookie, myFolder);
    player.playNotification(TRACK_RESET_DONE);
  }
  // lock admin menu
  else if (subMenu == 12)
  {
    int temp = voiceMenu(4, 980, 980, false);
    if (temp == 1)
    {
      mySettings.adminMenuLocked = 0;
    }
    else if (temp == 2)
    {
      mySettings.adminMenuLocked = ADMIN_MENU_LOCKED_CARD;
    }
    else if (temp == 3)
    {
      uint8_t pin[ARRAY_SIZE(mySettings.adminMenuPin)];
      player.playNotification(TRACK_INPUT_PIN);
      if (askCode(pin, ARRAY_SIZE(pin)))
      {
        memcpy(mySettings.adminMenuPin, pin, ARRAY_SIZE(mySettings.adminMenuPin));
        mySettings.adminMenuLocked = ADMIN_MENU_LOCKED_PIN;
      }
    }
    else if (temp == 4)
    {
      mySettings.adminMenuLocked = ADMIN_MENU_LOCKED_CHECK;
    }
  }
  writeSettingsToFlash(myFolder);
  standby.start(mySettings.standbyTimer * 60 * 1000);
}

bool askCode(uint8_t *code, size_t digit)
{
  size_t x = 0u;
  while (x < digit)
  {
    readButtons();
    if (pauseButton.pressedFor(LONG_PRESS))
    {
      return false;
    }

    if (pauseButton.wasReleased())
    {
      code[x++] = 1u;
    }

    if (upButton.wasReleased())
    {
      code[x++] = 2u;
    }

    if (downButton.wasReleased())
    {
      code[x++] = 3u;
    }
  }

  return true;
}

uint8_t voiceMenu(uint8_t numberOfOptions, uint16_t startMessage, uint16_t messageOffset,
                  bool preview, uint8_t previewFromFolder, uint8_t defaultValue, bool exitWithLongPress)
{
  uint8_t returnValue = defaultValue;

  if (startMessage != 0u) 
  {
    player.playNotification(startMessage);
  }

  Log.info(F("Voice menu with %d options" CR), numberOfOptions);

  do
  {
    if (Serial.available() > 0)
    {
      long optionSerial = Serial.parseInt();
      if (optionSerial > 0 && optionSerial <= numberOfOptions)
      {
        return (uint8_t)optionSerial;
      }
    }

    readButtons();
    player.loop();

    if (pauseButton.pressedFor(LONG_PRESS))
    {
      Log.notice(F("Voice menu cancelled - returning %u" CR), defaultValue);
      player.playNotification(TRACK_CANCELLED);
      ignorePauseButton = true;
      standby.loop();
      return defaultValue;
    }

    if (pauseButton.wasReleased())
    {
      if (returnValue != 0u)
      {
        Log.info(F("Option %u chosen" CR), returnValue);
        return returnValue;
      }

      Log.notice(F("No option chosen - pause ignored" CR));
    }

    bool newReturnValue = false;

    if (upButton.pressedFor(LONG_PRESS))
    {
      returnValue = min(returnValue + 10u, numberOfOptions);
      newReturnValue = true;
      ignoreUpButton = true;
    }
    else if (upButton.wasReleased())
    {
      if (!ignoreUpButton)
      {
        returnValue = min(returnValue + 1u, numberOfOptions);
        newReturnValue = true;
      }
      else
      {
        ignoreUpButton = false;
      }
    }

    if (downButton.pressedFor(LONG_PRESS))
    {
      returnValue = returnValue > 10u ? returnValue - 10u : 1u;
      newReturnValue = true;
      ignoreDownButton = true;
    }
    else if (downButton.wasReleased())
    {
      if (!ignoreDownButton)
      {
        returnValue = returnValue > 1u ? returnValue - 1u : 1u;
        newReturnValue = true;
      }
      else
      {
        ignoreDownButton = false;
      }
    }

    if (newReturnValue) {
      newReturnValue = false;
      Log.notice(F("New option %u" CR), returnValue);
      player.playNotification(messageOffset + returnValue);
      player.waitForTrackToFinish();

      if (preview)
      {
        if (previewFromFolder == 0u)
        {
          player.play(returnValue, 1u);
        }
        else
        {
          player.play(previewFromFolder, returnValue);
        }
        delay(1000);
      }
    }
  } while (true);
}

void resetCard()
{
  player.playNotification(TRACK_PLACE_CARD);
  do
  {
    pauseButton.read();
    upButton.read();
    downButton.read();

    if (upButton.wasReleased() || downButton.wasReleased())
    {
      Serial.print(F("Abgebrochen!"));
      player.playNotification(TRACK_CANCELLED);
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  Serial.print(F("Karte wird neu konfiguriert!"));
  setupCard();
}

bool setupFolder(FolderSettings *theFolder)
{
  // Ordner abfragen
  theFolder->folder = voiceMenu(99, 301, 0, true, 0, 0, true);
  if (theFolder->folder == 0)
    return false;

  // Wiedergabemodus abfragen
  theFolder->mode = (PlayMode)voiceMenu(9, 310, 310, false, 0, 0, true);
  if (theFolder->mode == PlayMode_Unitialized)
    return false;

  //  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  //  EEPROM.update(theFolder->folder, 1);

  // Einzelmodus -> Datei abfragen
  if (theFolder->mode == PlayMode_SingleTrack)
  {
    theFolder->special = voiceMenu(player.getTrackCountForFolder(theFolder->folder), 320, 0,
                                   true, theFolder->folder);
  }
  // Admin Funktionen
  else if (theFolder->mode == PlayMode_Admin)
  {
    // TODO warum 255?
    // theFolder->special = voiceMenu(3, 320, 320);
    theFolder->folder = 0;
    theFolder->mode = (PlayMode)255;
  }
  // Spezialmodus Von-Bis
  else if (theFolder->mode == PlayMode_RandomRange || theFolder->mode == PlayMode_AlbumRange || theFolder->mode == PlayMode_PartyRange)
  {
    theFolder->special = voiceMenu(player.getTrackCountForFolder(theFolder->folder), 321, 0,
                                   true, theFolder->folder);
    theFolder->special2 = voiceMenu(player.getTrackCountForFolder(theFolder->folder), 322, 0,
                                    true, theFolder->folder, theFolder->special);
  }

  return true;
}

void setupCard()
{
  player.pause();
  Serial.println(F("=== setupCard()"));
  nfcTagObject newCard;
  if (setupFolder(&newCard.nfcFolderSettings) == true)
  {
    // Karte ist konfiguriert -> speichern
    player.pause();
    do
    {
    } while (player.isPlaying());
    writeCard(newCard);
  }
  delay(1000);
}
bool readCard(nfcTagObject *nfcTag)
{
  nfcTagObject tempCard;
  // Show some details of the PICC (that is: the tag/card)
  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  byte buffer[18];
  byte size = sizeof(buffer);

  // Authenticate using key A
  if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
      (piccType == MFRC522::PICC_TYPE_MIFARE_1K) ||
      (piccType == MFRC522::PICC_TYPE_MIFARE_4K))
  {
    Serial.println(F("Authenticating Classic using key A..."));
    status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  }
  else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL)
  {
    byte pACK[] = {0, 0}; // 16 bit PassWord ACK returned by the tempCard

    // Authenticate using key A
    Serial.println(F("Authenticating MIFARE UL..."));
    status = mfrc522.PCD_NTAG216_AUTH(key.keyByte, pACK);
  }

  if (status != MFRC522::STATUS_OK)
  {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Show the whole sector as it currently is
  // Serial.println(F("Current data in sector:"));
  // mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
  // Serial.println();

  // Read data from the block
  if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
      (piccType == MFRC522::PICC_TYPE_MIFARE_1K) ||
      (piccType == MFRC522::PICC_TYPE_MIFARE_4K))
  {
    Serial.print(F("Reading data from block "));
    Serial.print(blockAddr);
    Serial.println(F(" ..."));
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Read() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      return false;
    }
  }
  else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL)
  {
    byte buffer2[18];
    byte size2 = sizeof(buffer2);

    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(8, buffer2, &size2);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Read_1() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      return false;
    }
    memcpy(buffer, buffer2, 4);

    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(9, buffer2, &size2);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Read_2() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      return false;
    }
    memcpy(buffer + 4, buffer2, 4);

    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(10, buffer2, &size2);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Read_3() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      return false;
    }
    memcpy(buffer + 8, buffer2, 4);

    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(11, buffer2, &size2);
    if (status != MFRC522::STATUS_OK)
    {
      Serial.print(F("MIFARE_Read_4() failed: "));
      Serial.println(mfrc522.GetStatusCodeName(status));
      return false;
    }
    memcpy(buffer + 12, buffer2, 4);
  }

  Serial.println(F("Data on Card :"));
  dump_byte_array(buffer, 16);
  Serial.println();
  Serial.println();

  uint32_t tempCookie;
  tempCookie = (uint32_t)buffer[0] << 24;
  tempCookie += (uint32_t)buffer[1] << 16;
  tempCookie += (uint32_t)buffer[2] << 8;
  tempCookie += (uint32_t)buffer[3];

  tempCard.cookie = tempCookie;
  tempCard.version = buffer[4];
  tempCard.nfcFolderSettings.folder = buffer[5];
  tempCard.nfcFolderSettings.mode = (PlayMode)buffer[6];
  tempCard.nfcFolderSettings.special = buffer[7];
  tempCard.nfcFolderSettings.special2 = buffer[8];

  if (tempCard.cookie == cardCookie)
  {
    if (activeModifier != NULL && tempCard.nfcFolderSettings.folder != 0)
    {
      if (activeModifier->handleRFID(&tempCard))
      {
        return false;
      }
    }

    if (tempCard.nfcFolderSettings.folder == 0)
    {
      if (activeModifier != NULL)
      {
        if (activeModifier->getActive() == tempCard.nfcFolderSettings.mode)
        {
          activeModifier = NULL;
          Serial.println(F("modifier removed"));
          if (player.isPlaying())
          {
            player.playAdvertisement(261);
          }
          else
          {
            player.start();
            delay(100);
            player.playAdvertisement(261);
            delay(100);
            player.pause();
          }
          delay(2000);
          return false;
        }
      }
      if (tempCard.nfcFolderSettings.mode != 0 && tempCard.nfcFolderSettings.mode != 255)
      {
        if (player.isPlaying())
        {
          player.playAdvertisement(260);
        }
        else
        {
          player.start();
          delay(100);
          player.playAdvertisement(260);
          delay(100);
          player.pause();
        }
      }
      switch (tempCard.nfcFolderSettings.mode)
      {
      case 0:
      case 255:
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        adminMenu(true);
        break;
      case 1:
        activeModifier = new SleepTimer(tempCard.nfcFolderSettings.special);
        break;
      case 2:
        activeModifier = new FreezeDance();
        break;
      case 3:
        activeModifier = new Locked();
        break;
      case 4:
        activeModifier = new ToddlerMode();
        break;
      case 5:
        activeModifier = new KindergardenMode();
        break;
      case 6:
        activeModifier = new RepeatSingleModifier();
        break;
      }
      delay(2000);
      return false;
    }
    else
    {
      memcpy(nfcTag, &tempCard, sizeof(nfcTagObject));
      myFolder = &nfcTag->nfcFolderSettings;
    }
    return true;
  }
  else
  {
    memcpy(nfcTag, &tempCard, sizeof(nfcTagObject));
    return true;
  }
}

void writeCard(nfcTagObject nfcTag)
{
  MFRC522::PICC_Type mifareType;
  byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                             // identify our nfc tags
                     0x02,                             // version 1
                     nfcTag.nfcFolderSettings.folder,  // the folder picked by the user
                     nfcTag.nfcFolderSettings.mode,    // the playback mode picked by the user
                     nfcTag.nfcFolderSettings.special, // track or function for admin cards
                     nfcTag.nfcFolderSettings.special2,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  mifareType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  // Authenticate using key B
  // authentificate with the card and set card specific parameters
  if ((mifareType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
      (mifareType == MFRC522::PICC_TYPE_MIFARE_1K) ||
      (mifareType == MFRC522::PICC_TYPE_MIFARE_4K))
  {
    Serial.println(F("Authenticating again using key A..."));
    status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  }
  else if (mifareType == MFRC522::PICC_TYPE_MIFARE_UL)
  {
    byte pACK[] = {0, 0}; // 16 bit PassWord ACK returned by the NFCtag

    // Authenticate using key A
    Serial.println(F("Authenticating UL..."));
    status = mfrc522.PCD_NTAG216_AUTH(key.keyByte, pACK);
  }

  if (status != MFRC522::STATUS_OK)
  {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    player.playNotification(TRACK_ERROR);
    return;
  }

  // Write data to the block
  Serial.print(F("Writing data into block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  dump_byte_array(buffer, 16);
  Serial.println();

  if ((mifareType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
      (mifareType == MFRC522::PICC_TYPE_MIFARE_1K) ||
      (mifareType == MFRC522::PICC_TYPE_MIFARE_4K))
  {
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  }
  else if (mifareType == MFRC522::PICC_TYPE_MIFARE_UL)
  {
    byte buffer2[16];
    byte size2 = sizeof(buffer2);

    memset(buffer2, 0, size2);
    memcpy(buffer2, buffer, 4);
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(8, buffer2, 16);

    memset(buffer2, 0, size2);
    memcpy(buffer2, buffer + 4, 4);
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(9, buffer2, 16);

    memset(buffer2, 0, size2);
    memcpy(buffer2, buffer + 8, 4);
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(10, buffer2, 16);

    memset(buffer2, 0, size2);
    memcpy(buffer2, buffer + 12, 4);
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(11, buffer2, 16);
  }

  if (status != MFRC522::STATUS_OK)
  {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    player.playNotification(TRACK_ERROR);
  }
  else
    player.playNotification(TRACK_CARD_CONFIGURED);
  Serial.println();
  delay(2000);
}

/**
  Helper routine to dump a byte array as hex values to Serial.
*/
void dump_byte_array(byte *buffer, byte bufferSize)
{
  for (byte i = 0; i < bufferSize; i++)
  {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
