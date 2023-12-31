/**
 * @file iRadioAudio.cpp
 * @author Dieter Zumkehr, Arne v.Irmer (Dieter.Zumkehr @ fh-Dortmund.de, Arne.vonIrmer @ tu-dortmund.de)
 * @brief Hier befinden sich alle Implementierungen rund um die Audio-Hardware
 * @version 0.1
 * @date 2023-04-03
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <iRadioAudio.hpp>
#include <iRadioDisplay.hpp>
#include <Arduino.h>

// Logging-Tag für Easy-Logger
static const char *TAG = "AUDIO";

/** @name I²S-Verbindung
 *  Hier werden die Steuerleitungen zum I²S den GPIO-Pins am ESP32 zugeordnet.
 *  Genaueres dazu gibt es [hier](https://de.wikipedia.org/wiki/I%C2%B2S)
 */
/// @{
constexpr uint8_t I2S_DOUT = 25; ///< Daten-Leitung (SD)
constexpr uint8_t I2S_BCLK = 27; ///< Takt-Leitung (SCK)
constexpr uint8_t I2S_LRC = 26;  ///< Word-Select-Leitung (WS)
/// @}

// Create audio object
Audio audio;
constexpr uint8_t volume_max = 20;
constexpr uint8_t PRE = 25;
int volume = 0;

// volume smoothing variables
uint8_t oldvolume = 0;
uint8_t volcount = 0;
int voldisplaystart = 0;

constexpr u_int8_t volumeBlocks[5] = {0xD4, 0xD3, 0xD2, 0xD1, 0xD0};

void audio_info(const char *info)
{
  LOG_DEBUG(TAG, "Audio_Info: " << info);
}

/**
 * @brief Es wird sich mit dem aktuell ausgewählten Stream verbunden.
 *
 */
void connectCurrentStation()
{

  // Auslesen der aktuell ausgewählten Station und Ermittlung der zugehörigen url
  String url = getCurrentStation().url;

  // Die url ist in einem `String` abgelegt. `connecttohost()` braucht aber ein `char *`
  // Holen eines Speichers
  unsigned int urlLen = url.length();

  char urlCharArr[urlLen + 1]; // +1 wegen der Null am Ende eines Strings
  // Konvertierung
  url.toCharArray(urlCharArr, urlLen + 1);

  // Aufruf
  bool status = audio.connecttohost(urlCharArr);

  LOG_DEBUG(TAG, "connectCurrentStation-Status:" << (status ? "T" : "F"));
}

/**
 * @brief Initialisieren der Audio-Hardware
 *
 */
void setupAudio()
{
  analogReadResolution(10);

  // Connect MAX98357 I2S Amplifier Module
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  oldvolume = analogRead(VOL);
  oldvolume = map(oldvolume, 0, 1023, 0, volume_max);
  audio.setVolume(oldvolume);

  // Mute ausschalten!
  pinMode(MUTE, OUTPUT);
  digitalWrite(MUTE, LOW);
}

/**
 * @brief Gibt die aktuelle Lautstärke in Blöcken als String zurück.
 *
 * @param volume Lautstärke
 * @return String (7 Zeichen lang)
 */
String getBlocks(uint8_t volume)
{
  String blocks = "";

  // add full blocks
  for (uint8_t i = 0; i < volume / 5; i++)
  {
    blocks += char(volumeBlocks[4]);
  }

  // add smaller blocks
  if (volume % 5 != 0)
  {
    blocks += char(volumeBlocks[volume % 5 - 1]);
  }

  // maximum volume warning
  if (volume >= 20)
    blocks += "!";

  // fill with spaces
  while (blocks.length() < 7)
  {
    blocks += " ";
  }

  return blocks;
}

/**
 * @brief Regelmäßiges Aktualisieren der Audio-Einstellungen.
 *
 */
void loopAudioLautst()
{
  // check if normal display should be shown again (2 seconds after volume change)
  if (voldisplaystart + 2000 < millis())
  {
    streamingScreen.setText("iRadio  " + getTime() + "      ", 0);
  }

  audio.loop();
  volume = analogRead(VOL);
  volume = map(volume, 0, 1023, 0, volume_max);

  // The poti is noisy. Only if 25 values are indifferent from the initial value the volume is set.  
  if (volume != oldvolume)
  {
    // volume changed, increase counter
    if (volcount < 25)
      volcount++;
    else
    {
      // new volume level reached
      volcount = 0;
      voldisplaystart = millis();
      audio.setVolume(volume);
      streamingScreen.setText(String(getBlocks(volume)), 0);
      oldvolume = volume;
    }
  }
  else
  {
    // new volume level reached
    volcount = 0;
  }
}
/**
 * @brief Implementiert eine `weak` gebundene Methode der Audio-Klasse (Infos dazu gibt es [hier](https://en.wikipedia.org/wiki/Weak_symbol))
 * Diese Methode wird aufgerufen, wenn ein neues Stück gespielt wird. Sie gibt den Interpreten und den Titel des Stückes zurück.
 * @param theStreamTitle Der Titel, der grade gespielt wird.
 */
void audio_showstreamtitle(const char *theStreamTitle)
{
  String name = extraChar(String(theStreamTitle));
  uint8_t p = name.indexOf(" - "); // between artist & title
  if (p == 0)
  {
    p = name.indexOf(": ");
  }
  // Den gefundenen Musiker-Namen in die 3 Zeile (da 0 das erste Element in 2) des Stream-Screens schreiben
  streamingScreen.setText(name.substring(0, p), 2);
  // Den gefundenen Musik-Titel in die 4 Zeile (da 0 das erste Element in 3) des Stream-Screens schreiben

  streamingScreen.setText(name.substring(p + 3, name.length()), 3);
}

/**
 * @brief Implementiert eine `weak` gebundene Methode der Audio-Klasse (Infos dazu gibt es [hier](https://en.wikipedia.org/wiki/Weak_symbol))
 * Diese Methode wird aufgerufen, wenn ein neues Stück gespielt wird. Sie gibt die Station zurück.
 * @param theStation
 */
void audio_showstation(const char *theStation)
{
  // Den gefundenen Musiker-Namen in die 2 Zeile (da 0 das erste Element in 1) des Stream-Screens schreiben
  // Der Name des Senders wird auch in den Streaming-Daten übertragen. Leider ist das oft nicht gut gepflegt.
  // streamingScreen.setText(String(theStation), 1);
  // Deshalb nehmen wir den Namen, den der Benutzer für den Stream vergeben hat. Dann kann der sich auch frei entscheiden,
  // welchen er da sehen will.
  streamingScreen.setText(getCurrentStation().name, 1);
}