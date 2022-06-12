#pragma once

#include "Player.hpp"
#include <MFRC522.h>
#include "Types.hpp"


// this object stores nfc tag data
typedef struct  {
  uint32_t cookie;
  uint8_t version;
  FolderSettings nfcFolderSettings;
  //  uint8_t folder;
  //  uint8_t mode;
  //  uint8_t special;
  //  uint8_t special2;
} NfcTagObject;


enum CardManagerError
{
    CardManagerSuccess,
    CardManagerAuthenticationFailed,
    CardManagerWriteFailed,
};

class CardManager
{
    public:
        CardManager(uint8_t ss_pin, uint8_t rst_pin) 
            : _mfrc522(ss_pin, rst_pin)
            {}

        void begin(void);

        MFRC522 &GetReader(void) { return _mfrc522; }

        bool readCard(NfcTagObject &nfcTag);
        CardManagerError writeCard(const NfcTagObject &nfcTag);

    private:
        MFRC522 _mfrc522;

};
