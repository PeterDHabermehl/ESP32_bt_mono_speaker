#include <Arduino.h>
#include "version.h"
#include <FS.h>
#include <SPIFFS.h>

void printVersion() {
  Serial.println(".");
  Serial.println(F("=== Firmware Info ==="));

  Serial.println(".");

  Serial.print(F("Company   : "));
  Serial.println(FW_COMPANY);

  Serial.println(".");

  Serial.print(F("Name      : "));
  Serial.println(FW_NAME);

  Serial.print(F("Device    : "));
  Serial.println(FW_DEVICE);

  Serial.println(".");

  Serial.println(FW_CATCHPHRASE);

  Serial.println(".");

  Serial.print(F("Version   : "));
  Serial.println(FW_VERSION);

  Serial.print(F("Build     : "));
  Serial.print(FW_BUILD_DATE);
  Serial.print(F(" "));
  Serial.println(FW_BUILD_TIME);

  Serial.println(".");

  Serial.println(F("=== Firmware Info End ==="));
  Serial.println();
}

/*
void serial_read(char *buffer)
{
    if (!buffer) {
        return;
    }

    // Default: 24x 'x'
    memset(buffer, 'x', SERIAL_LENGTH);
    buffer[SERIAL_LENGTH] = '\0';

    FILE *f = fopen(SERIAL_FILENAME, "r");
    if (!f) {
        return;
    }

    size_t read = fread(buffer, 1, SERIAL_LENGTH, f);
    fclose(f);

    if (read < SERIAL_LENGTH) {
        // Datei zu kurz → Fallback bleibt aktiv
        memset(buffer, 'x', SERIAL_LENGTH);
        buffer[SERIAL_LENGTH] = '\0';
    }
}

void serial_write(const char *buffer)
{
    if (!buffer) {
        return;
    }

    char temp[SERIAL_LENGTH];

    // Länge des Eingabepuffers ermitteln
    size_t len = strlen(buffer);

    // Kopieren und ggf. auffüllen
    if (len >= SERIAL_LENGTH) {
        memcpy(temp, buffer, SERIAL_LENGTH); // kürzen
    } else {
        memcpy(temp, buffer, len);
        // Rest mit '*' auffüllen
        memset(temp + len, '*', SERIAL_LENGTH - len);
    }

    // Datei öffnen und schreiben
    FILE *f = fopen(SERIAL_FILENAME, "w");
    if (!f) {
        return;
    }

    fwrite(temp, 1, SERIAL_LENGTH, f);
    fclose(f);
}
*/


// Prüft, ob SPIFFS bereit ist, und mountet es bei Bedarf
bool ensureSPIFFS()
{
    if (!SPIFFS.begin(true)) {  // true = formatieren, falls nicht gemountet
        Serial.println("SPIFFS mount failed!");
        return false;
    }
    return true;
}

// ================== serial_write ==================
void serial_write(const char *buffer)
{
    if (!buffer) return;

    if (!ensureSPIFFS()) return; // SPIFSS bereitstellen

    char temp[SERIAL_LENGTH];
    size_t len = strlen(buffer);

    if (len >= SERIAL_LENGTH) {
        memcpy(temp, buffer, SERIAL_LENGTH);
    } else {
        memcpy(temp, buffer, len);
        memset(temp + len, '*', SERIAL_LENGTH - len);  // mit '*' auffüllen
    }

    File f = SPIFFS.open(SERIAL_FILENAME, FILE_WRITE);
    if (!f) {
        Serial.println("serial_write: open failed");
        return;
    }

    f.write((const uint8_t *)temp, SERIAL_LENGTH);
    f.close();
}

// ================== serial_read ==================
void serial_read(char *buffer)
{
    if (!buffer) return;

    if (!ensureSPIFFS()) {
        // Falls SPIFFS nicht geht → mit '*' auffüllen
        memset(buffer, '*', SERIAL_LENGTH);
        buffer[SERIAL_LENGTH] = '\0';
        return;
    }

    memset(buffer, '*', SERIAL_LENGTH);  // Default-Fallback
    buffer[SERIAL_LENGTH] = '\0';

    File f = SPIFFS.open(SERIAL_FILENAME, FILE_READ);
    if (!f) {
        return; // Datei nicht vorhanden → Fallback bleibt aktiv
    }

    size_t read = f.readBytes(buffer, SERIAL_LENGTH);
    f.close();

    if (read < SERIAL_LENGTH) {
        memset(buffer, '*', SERIAL_LENGTH);  // Datei zu kurz → Fallback
        buffer[SERIAL_LENGTH] = '\0';
    }
}

