#include "CardManager.hpp"

/**
  Helper routine to dump a byte array as hex values to Serial.
*/
static void dump_byte_array(byte *buffer, byte bufferSize)
{
    for (byte i = 0; i < bufferSize; i++)
    {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

void CardManager::begin(void)
{
  _mfrc522.PCD_Init(); // Init MFRC522
  _mfrc522.PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader    
}

bool CardManager::readCard(NfcTagObject &nfcTag)
{
    // Show some details of the PICC (that is: the tag/card)
    Serial.print(F("Card UID:"));
    dump_byte_array(_mfrc522.uid.uidByte, _mfrc522.uid.size);
    Serial.println();
    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = _mfrc522.PICC_GetType(_mfrc522.uid.sak);
    Serial.println(_mfrc522.PICC_GetTypeName(piccType));

    byte trailerBlock = 7;
    byte blockAddr = 4;
    MFRC522::MIFARE_Key key = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    MFRC522::StatusCode status;

    // Authenticate using key A
    if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
        (piccType == MFRC522::PICC_TYPE_MIFARE_1K) ||
        (piccType == MFRC522::PICC_TYPE_MIFARE_4K))
    {
        Serial.println(F("Authenticating Classic using key A..."));
        status = _mfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(_mfrc522.uid));
    }
    else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL)
    {
        byte pACK[] = {0, 0}; // 16 bit PassWord ACK returned by the tempCard

        // Authenticate using key A
        Serial.println(F("Authenticating MIFARE UL..."));
        status = _mfrc522.PCD_NTAG216_AUTH(key.keyByte, pACK);
    }
    else
    {
        Serial.print(F("Unhandled type"));
        return false;
    }

    if (status != MFRC522::STATUS_OK)
    {
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(_mfrc522.GetStatusCodeName(status));
        return false;
    }

    // Show the whole sector as it currently is
    // Serial.println(F("Current data in sector:"));
    // mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
    // Serial.println();

    byte buffer[18];
    byte size = sizeof(buffer);

    // Read data from the block
    if ((piccType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
        (piccType == MFRC522::PICC_TYPE_MIFARE_1K) ||
        (piccType == MFRC522::PICC_TYPE_MIFARE_4K))
    {
        Serial.print(F("Reading data from block "));
        Serial.print(blockAddr);
        Serial.println(F(" ..."));
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Read(blockAddr, buffer, &size);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print(F("MIFARE_Read() failed: "));
            Serial.println(_mfrc522.GetStatusCodeName(status));
            return false;
        }
    }
    else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL)
    {
        byte buffer2[18];
        byte size2 = sizeof(buffer2);

        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Read(8, buffer2, &size2);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print(F("MIFARE_Read_1() failed: "));
            Serial.println(_mfrc522.GetStatusCodeName(status));
            return false;
        }
        memcpy(buffer, buffer2, 4);

        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Read(9, buffer2, &size2);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print(F("MIFARE_Read_2() failed: "));
            Serial.println(_mfrc522.GetStatusCodeName(status));
            return false;
        }
        memcpy(buffer + 4, buffer2, 4);

        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Read(10, buffer2, &size2);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print(F("MIFARE_Read_3() failed: "));
            Serial.println(_mfrc522.GetStatusCodeName(status));
            return false;
        }
        memcpy(buffer + 8, buffer2, 4);

        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Read(11, buffer2, &size2);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print(F("MIFARE_Read_4() failed: "));
            Serial.println(_mfrc522.GetStatusCodeName(status));
            return false;
        }
        memcpy(buffer + 12, buffer2, 4);
    }

    Serial.print(F("Data on Card "));
    Serial.println(F(":"));
    dump_byte_array(buffer, 16);
    Serial.println();
    Serial.println();

    uint32_t tempCookie;
    tempCookie = (uint32_t)buffer[0] << 24;
    tempCookie += (uint32_t)buffer[1] << 16;
    tempCookie += (uint32_t)buffer[2] << 8;
    tempCookie += (uint32_t)buffer[3];

    nfcTag.cookie = tempCookie;
    nfcTag.version = buffer[4];
    nfcTag.nfcFolderSettings.folder = buffer[5];
    nfcTag.nfcFolderSettings.mode = buffer[6];
    nfcTag.nfcFolderSettings.special = buffer[7];
    nfcTag.nfcFolderSettings.special2 = buffer[8];

    return true;
}

CardManagerError CardManager::writeCard(const NfcTagObject &nfcTag)
{
    byte buffer[16] = {0x13, 0x37, 0xb3, 0x47,           // 0x1337 0xb347 magic cookie to
                                                         // identify our nfc tags
                       0x02,                             // version 2
                       nfcTag.nfcFolderSettings.folder,  // the folder picked by the user
                       nfcTag.nfcFolderSettings.mode,    // the playback mode picked by the user
                       nfcTag.nfcFolderSettings.special, // track or function for admin cards
                       nfcTag.nfcFolderSettings.special2,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    MFRC522::PICC_Type mifareType = _mfrc522.PICC_GetType(_mfrc522.uid.sak);

    MFRC522::StatusCode status;
    byte trailerBlock = 7;
    byte blockAddr = 4;
    MFRC522::MIFARE_Key key = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    // Authenticate using key B
    // authentificate with the card and set card specific parameters
    if ((mifareType == MFRC522::PICC_TYPE_MIFARE_MINI) ||
        (mifareType == MFRC522::PICC_TYPE_MIFARE_1K) ||
        (mifareType == MFRC522::PICC_TYPE_MIFARE_4K))
    {
        Serial.println(F("Authenticating again using key A..."));
        status = _mfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(_mfrc522.uid));
    }
    else if (mifareType == MFRC522::PICC_TYPE_MIFARE_UL)
    {
        byte pACK[] = {0, 0}; // 16 bit PassWord ACK returned by the NFCtag

        // Authenticate using key A
        Serial.println(F("Authenticating UL..."));
        status = _mfrc522.PCD_NTAG216_AUTH(key.keyByte, pACK);
    }

    if (status != MFRC522::STATUS_OK)
    {
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(_mfrc522.GetStatusCodeName(status));
        return CardManagerError::CardManagerAuthenticationFailed;
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
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Write(blockAddr, buffer, 16);
    }
    else if (mifareType == MFRC522::PICC_TYPE_MIFARE_UL)
    {
        byte buffer2[16];
        byte size2 = sizeof(buffer2);

        memset(buffer2, 0, size2);
        memcpy(buffer2, buffer, 4);
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Write(8, buffer2, 16);

        memset(buffer2, 0, size2);
        memcpy(buffer2, buffer + 4, 4);
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Write(9, buffer2, 16);

        memset(buffer2, 0, size2);
        memcpy(buffer2, buffer + 8, 4);
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Write(10, buffer2, 16);

        memset(buffer2, 0, size2);
        memcpy(buffer2, buffer + 12, 4);
        status = (MFRC522::StatusCode)_mfrc522.MIFARE_Write(11, buffer2, 16);
    }

    if (status != MFRC522::STATUS_OK)
    {
        Serial.print(F("MIFARE_Write() failed: "));
        Serial.println(_mfrc522.GetStatusCodeName(status));
        return CardManagerError::CardManagerWriteFailed;
    }

    return CardManagerError::CardManagerSuccess;
}