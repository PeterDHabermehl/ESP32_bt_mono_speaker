/*
==================================================================

Firmware for the
DL Acoustics DL-BB8 Buller-Boll 8"

(c) Drømpelbert Lärmverk AB

==================================================================
*/

#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>
#include "version.h"
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

using namespace audio_tools;

char buffer[25];

#define BT_NAME "DL-BB8"

// Pins
#define SD_CS_PIN 12
#define BTN_PIN 14
#define AC_MAIN_PIN 32



static float outVolume = 1.0f;
static float maxOutVolume = 1.0f;

static int short_tip_duration = 1000;
static int short_tip_pause = 1000;
static int long_press_threshold = 10000;
static bool muted = false;
static bool fadeIn = false;
static bool fadeOut = false;
static int debounce_time = 100;

static float fadeTargetVolume = 0.02f;
static float fadeMinTarget = 0.02f;
static float oldVolume = 1.0f;
static float paOldVol = 1.0f;
static float paVol = 0.03f;
static float wgVol = 0.18f;

static bool vcState = false;
static bool mp3Active = false;

bool acMainSwitch = true;
bool acMainSwitchOld = false;

static unsigned long ecoTimeout = 300000;  // timeout für main power off in ms, 300.000 --> 5min
static unsigned long lastSound = millis();

constexpr size_t SILENCE_FRAMES = 128;
uint8_t silenceBuffer[SILENCE_FRAMES * 2] = { 0 };

AudioInfo stereo_info(44100, 2, 16);
AudioInfo mono_info(44100, 1, 16);

// WAVDecoder wav_decoder;
StreamCopy mp3Copier;
File audioFile;

I2SStream i2s;
VolumeMeter i2sVUmeter;

FormatConverterStream mp3Conv;

MultiOutput target(i2s, i2sVUmeter);

FilteredStream<int16_t, float> bassNotch(target, 2);

VolumeStream out_volume_stream(bassNotch);

FilteredStream<int16_t, float> inFiltered_woof(out_volume_stream, 2);
FilteredStream<int16_t, float> inFiltered_noWoof(out_volume_stream, 2);

FormatConverterStream monoToStereoConverter(inFiltered_woof);
Equalizer3Bands eq(monoToStereoConverter);
FormatConverterStream stereoToMonoConverter(eq);

VolumeStream bt_volume_stream(stereoToMonoConverter);
BluetoothA2DPSink a2dp_sink(bt_volume_stream);

ConfigEqualizer3Bands cfg_eq;

EncodedAudioOutput mp3decoder(&mp3Conv, new MP3DecoderHelix());

VolumeMeter VUmeter;

static int currentPreset = 0;

// EQ Presets:
typedef struct {
  const char* name;
  const char* audiofile;
  float low;
  float mid;
  float high;
} EqPreset;

const EqPreset eq_presets[] = {
  { "Neutral", "/sys/EQN.mp3", 1.0f, 1.0f, 1.0f },
  { "Speech", "/sys/EQS.mp3", 0.9f, 1.5f, 1.2f },
  { "Classical", "/sys/EQC.mp3", 1.1f, 1.2f, 1.3f },
  { "Jazz", "/sys/EQJ.mp3", 1.3f, 1.2f, 1.0f },
  { "Pop", "/sys/EQP.mp3", 1.4f, 1.0f, 1.5f },
  { "Rock", "/sys/EQR.mp3", 1.3f, 1.5f, 1.2f },
  { "Loudness", "/sys/EQL.mp3", 1.6f, 0.9f, 1.4f },
  { "DeltaBlues", "/sys/EQD.mp3", 1.7f, 0.8f, 0.9f },
  { "Concert", "/sys/EQK.mp3", 1.1f, 1.4f, 1.4f },
  { "Night", "/sys/EQE.mp3", 0.7f, 0.9f, 1.3f }
};

const int EQ_PRESET_COUNT = sizeof(eq_presets) / sizeof(eq_presets[0]);

bool lp80 = false;

// Butterworth 2. Ordnung, Trennfrequenz 118 Hz
const float LP_b_coefficients[] = {
  0.0000698283f,
  0.0001396566,
  0.0000698283f
};
const float LP_a_coefficients[] = {
  1.0f,
  -1.9762961895f,
  0.9765755027f
};

const float HP_b_coefficients[] = {
  0.9872179231f,
  -1.9744358462f,
  0.9872179231f
};
const float HP_a_coefficients[] = {
  1.0f,
  -1.97629618950f,
  0.9765755027f
};

// Butterworth 2. Ordnung, Trennfrequenz 90 Hz
const float LP80_b_coefficients[] = {
  0.00004120f,
  0.00008240f,
  0.00004120f
};
const float LP80_a_coefficients[] = {
  1.0f,
  -1.98139f,
  0.98143f
};

const float HP80_b_coefficients[] = {
  0.99129f,
  -1.98258f,
  0.99129f
};
const float HP80_a_coefficients[] = {
  1.0f,
  -1.98139f,
  0.98143f
};

// ==================================================================

void mainSwitch(void) {

  if (acMainSwitch == acMainSwitchOld) { return; }

  acMainSwitchOld = acMainSwitch;
  Serial.print("AC main: ");
  Serial.println(acMainSwitch);
  digitalWrite(AC_MAIN_PIN, acMainSwitch);
}


void paVolume(float pavel = 0.2) {
  static bool paActive = false;

  if (paActive) {
    if (lp80) {
      out_volume_stream.setVolume(0.0f, 0);
      out_volume_stream.setVolume(paOldVol, 1);
    } else {
      out_volume_stream.setVolume(paOldVol);
    }
    paActive = false;
  } else {
    paOldVol = out_volume_stream.volume(1);
    if (lp80) {
      out_volume_stream.setVolume(0.0f, 0);
      out_volume_stream.setVolume(pavel, 1);
    } else {
      out_volume_stream.setVolume(pavel);
    }
    paActive = true;
  }
}

void playMP3(const char* filename, float targetVolume, bool blocking = false) {

  Serial.println("playMP3 reached");
  Serial.println(filename);

  if (mp3Active) {
    Serial.println("playMP3 already active");
    return;
  }

  audioFile = SD.open(filename, "r");
  if (!audioFile) {
    Serial.println("audiofile fail");

    return;
  }

  Serial.println("audiofile open");

  if (mp3Active) { endMP3(); }

  bt_volume_stream.setOutput(VUmeter);
  paVolume(targetVolume);

  mp3Copier.begin(mp3decoder, audioFile);
  Serial.println("copier passed");
  mp3Active = true;

  if (blocking) {
    while (mp3Copier.copy()) { delay(1); }
    endMP3();
  }
}

void endMP3() {
  // out_volume_stream.setVolume(0);
  audioFile.close();
  mp3Copier.end();
  bt_volume_stream.setOutput(stereoToMonoConverter);
  paVolume();  // restore out volume

  mp3Active = false;

  target.write((uint8_t*)silenceBuffer,
               sizeof(silenceBuffer));

  Serial.println("endMP3 reached");
}


void ecoHandler(void) {

  if (i2sVUmeter.volumeAvg() == 0) {
    if (((millis() - lastSound) > ecoTimeout) && acMainSwitch) {
      if (!muted) { mute(); }
      acMainSwitch = false;
    }
  } else {
    if (!acMainSwitch) {
      acMainSwitch = true;
      delay(20);
      if (muted) { mute(); }
    }
    lastSound = millis();
  }
}


void setNextEqPreset(void) {

  // Zum nächsten Preset wechseln (zyklisch)
  currentPreset++;
  if (currentPreset >= EQ_PRESET_COUNT) {
    currentPreset = 0;
  }

  // Preset anwenden
  cfg_eq.gain_low = eq_presets[currentPreset].low;
  cfg_eq.gain_medium = eq_presets[currentPreset].mid;
  cfg_eq.gain_high = eq_presets[currentPreset].high;

  // Debug-Ausgabe
  Serial.print("EQ Preset gesetzt: ");
  Serial.println(eq_presets[currentPreset].name);
  playMP3(eq_presets[currentPreset].audiofile, paVol, false);
}

void baseBandDamper(void) {
  static bool bbdActive = false;

  bbdActive = !bbdActive;

  cfg_eq.gain_low = eq_presets[currentPreset].low;
  cfg_eq.gain_medium = eq_presets[currentPreset].mid;
  cfg_eq.gain_high = eq_presets[currentPreset].high;

  if (bbdActive) {
    cfg_eq.gain_low = cfg_eq.gain_low * 0.5;
    Serial.println("Baseband Damper on");
    playMP3("/sys/BB0.mp3", paVol, false);
  } else {
    Serial.println("Baseband Damper off");
    playMP3("/sys/BB1.mp3", paVol, false);
  }
}
void fadeHandler() {
  static unsigned long lastFadeStep = 0;
  if (!(fadeIn || fadeOut)) { return; }

  if ((millis() - lastFadeStep) > 20) {
    float oldvol = bt_volume_stream.volume();
    lastFadeStep = millis();
    if (fadeIn) {
      float newvol = oldvol + 0.01;
      bt_volume_stream.setVolume(min(newvol, fadeTargetVolume));
      if (newvol >= fadeTargetVolume) {
        fadeIn = false;
        Serial.println("FadeIn complete");
      }
    }
    if (fadeOut) {
      float newvol = oldvol - 0.01;
      bt_volume_stream.setVolume(max(newvol, 0.0f));
      if (newvol <= fadeTargetVolume) {
        fadeOut = false;
        Serial.println("FadeOut complete");
      }
    }
  }
}
void mute(void) {
  if (muted) {
    fadeTargetVolume = max(oldVolume, fadeMinTarget);
    fadeIn = true;
    muted = !muted;
  } else {
    oldVolume = bt_volume_stream.volume(1);
    fadeTargetVolume = fadeMinTarget;
    fadeOut = true;
    muted = !muted;
  }
}

void buttonHandler() {
  static bool lastState = HIGH;
  static unsigned long lastChange = 0;

  static int tipCount = 0;
  static unsigned long time_at_last_tip = 0;
  static unsigned long time_at_last_press = 0;
  static unsigned long time_at_last_release = 0;
  static unsigned long volume_debounce = 0;

  bool state = digitalRead(BTN_PIN);

  if (state != lastState && millis() - lastChange > debounce_time) {
    if (!acMainSwitch) {
      playMP3("/sys/UNM.mp3", paVol, false);  //acMainSwitch = true;
      if (muted) {mute();}
      Serial.println("Wakeup");
      return;
    }
    lastChange = millis();
    lastState = state;

    if (state == LOW) {
      Serial.println("Button pressed (debounced)");
      time_at_last_press = millis();
    } else {
      Serial.println("Button released");
      if ((millis() - time_at_last_press) > short_tip_pause) { tipCount = 0; }
      time_at_last_release = millis();

      if ((time_at_last_release - time_at_last_press) < short_tip_duration) {
        tipCount += 1;
        Serial.print("tipCount inc: ");
        Serial.println(tipCount);
      }
    }
  }

  if (state == LOW) {

    if ((tipCount == 0) && ((millis() - time_at_last_press) > long_press_threshold)) {
      Serial.println("Enter Setup");
    }

    if (((millis() - time_at_last_press) > 2000) && (!muted)) {
      if (tipCount == 1) {
        if (vcState == false) {
          vcState = true;
          playMP3("/sys/VUP.mp3", paVol, true);
        }
        if ((millis() - volume_debounce) > 100) {
          volume_debounce = millis();
          float newVol = out_volume_stream.volume(1) + 0.01;
          Serial.println(newVol);
          if (lp80) {
            out_volume_stream.setVolume(0.0f, 0);
            out_volume_stream.setVolume(min(newVol, maxOutVolume), 1);
          } else {
            out_volume_stream.setVolume(min(newVol, maxOutVolume));
          }
        }
      }
      if (tipCount == 2) {
        if (vcState == false) {
          vcState = true;
          playMP3("/sys/VDN.mp3", paVol, true);
        }
        if ((millis() - volume_debounce) > 100) {
          volume_debounce = millis();
          float newVol = out_volume_stream.volume(1) - 0.01;
          Serial.println(newVol);
          if (lp80) {
            out_volume_stream.setVolume(0.0f, 0);
            out_volume_stream.setVolume(max(newVol, 0.0f), 1);
          } else {
            out_volume_stream.setVolume(max(newVol, 0.0f));
          }
        }
      }
    }
  } else {
    vcState = false;
  }

  if (((millis() - time_at_last_release) > short_tip_pause) && (state != LOW)) {
    switch (tipCount) {
      case 1:
        Serial.println("Short tip 1");

        if (muted) {
          mute();
          playMP3("/sys/UNM.mp3", paVol, false);
        } else {
          mute();
          playMP3("/sys/MUT.mp3", paVol, false);
        }

        break;
      case 2:
        Serial.println("Short tip 2");
        if (muted) {
          playMP3("/sys/WGG.mp3", wgVol, false);
        } else {
          setNextEqPreset();
        }
        break;
      case 3:
        Serial.println("Short tip 3");

        if (lp80) {
          monoToStereoConverter.setOutput(inFiltered_woof);
          Serial.println("base band transducer engaged");
          out_volume_stream.setVolume(out_volume_stream.volume(1), 0);
          playMP3("/sys/ENG.mp3", paVol, false);
          lp80 = false;
        } else {
          monoToStereoConverter.setOutput(inFiltered_noWoof);
          Serial.println("base band transducer decoupled");
          out_volume_stream.setVolume(0.0f, 0);
          playMP3("/sys/DET.mp3", paVol, false);
          lp80 = true;
        }

        break;
      case 4:
        Serial.println("Short tip 4");
        baseBandDamper();
        break;
      case 5:
        Serial.println("Short tip 5");
        playMP3("/sys/WLC.mp3", paVol, false);
        break;
      case 6:
        Serial.println("Short tip 6");
        acMainSwitch = !acMainSwitch;
        break;
    }
    if (tipCount != 0) {
      Serial.print("Short tip high: ");
      Serial.println(tipCount);
      tipCount = 0;
    }
  }
}

void mp3Handler() {
  if (mp3Active) {
    if (mp3Copier.copy() == 0) { endMP3(); }
  }
}


/*
============================================================================
*/

void setup() {
  Serial.begin(115200);

  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  printVersion();
  serial_read(buffer);
  Serial.println("Serial number:");
  Serial.println(buffer);
  Serial.println("== Startup Message End ==");


  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD mount failed");
  } else {
    Serial.println("SD card mounted");
  }

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(AC_MAIN_PIN, OUTPUT);

  mainSwitch();

  //Linkwitz–Riley 4. Ordnung (LR4)
  inFiltered_woof.setFilter(0, new FilterChain<float, 2>({ new BiQuadDF1<float>(LP_b_coefficients, LP_a_coefficients), new BiQuadDF1<float>(LP_b_coefficients, LP_a_coefficients) }));
  inFiltered_woof.setFilter(1, new FilterChain<float, 2>({ new BiQuadDF1<float>(HP_b_coefficients, HP_a_coefficients), new BiQuadDF1<float>(HP_b_coefficients, HP_a_coefficients) }));

  inFiltered_noWoof.setFilter(0, new FilterChain<float, 2>({ new BiQuadDF1<float>(LP80_b_coefficients, LP80_a_coefficients), new BiQuadDF1<float>(LP80_b_coefficients, LP80_a_coefficients) }));
  inFiltered_noWoof.setFilter(1, new FilterChain<float, 2>({ new BiQuadDF1<float>(HP80_b_coefficients, HP80_a_coefficients), new BiQuadDF1<float>(HP80_b_coefficients, HP80_a_coefficients) }));

  // Bass Notch
  bassNotch.setFilter(0, new FilterChain<float, 3>({ new NotchFilter<float>(126.0f, 44100.0f, 10.0f), new NotchFilter<float>(80.0f, 44100.0f, 10.0f), new HighPassFilter<float>(50.0f, 44100.0f, 0.7071f) }));
  bassNotch.setFilter(1, new FilterChain<float, 3>({ new NotchFilter<float>(126.0f, 44100.0f, 10.0f), new NotchFilter<float>(80.0f, 44100.0f, 10.0f), new HighPassFilter<float>(50.0f, 44100.0f, 0.7071f) }));

  // I2S Konfiguration
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  cfg.pin_bck = 26;
  cfg.pin_ws = 25;
  cfg.pin_data = 22;
  cfg.pin_mck = -1;
  cfg.buffer_count = 8;
  cfg.buffer_size = 256;
  i2s.begin(cfg);

  Serial.println("I2S launched...");

  VUmeter.begin();

  mp3Conv.setOutput(monoToStereoConverter);
  mp3Conv.begin(stereo_info, mono_info);

  mp3decoder.begin();

  bt_volume_stream.setVolume(0.8);
  bt_volume_stream.begin();

  stereoToMonoConverter.begin(stereo_info, mono_info);  // downmix stereo to mono
  monoToStereoConverter.begin(mono_info, stereo_info);

  // setup equilizer
  cfg_eq = eq.defaultConfig();

  cfg_eq.setAudioInfo(mono_info);

  cfg_eq.gain_low = 1.0;
  cfg_eq.gain_medium = 1.0;
  cfg_eq.gain_high = 1.0;

  eq.begin(cfg_eq);

  inFiltered_woof.begin(stereo_info);
  inFiltered_noWoof.begin(stereo_info);

  bassNotch.begin(stereo_info);

  auto vcfg = out_volume_stream.defaultConfig();
  vcfg.allow_boost = true;  // activate amplification using linear control

  out_volume_stream.setVolume(0);
  out_volume_stream.begin(vcfg);

  target.begin();
  i2sVUmeter.begin();

  delay(150);

  out_volume_stream.setVolume(outVolume);

  playMP3("/sys/UNP.mp3", paVol, false);

  a2dp_sink.set_auto_reconnect(true);
  a2dp_sink.start(BT_NAME);
}


// ==================================================================


// ==================================================================
void loop() {

/*
  // test code for channel volume
  static long m = millis();

  if (millis() - m > 1000) {
    m = millis();
    Serial.println(i2sVUmeter.volumeAvg());
  }
*/

  // put your main code here, to run repeatedly:
  ecoHandler();
  mainSwitch();
  mp3Handler();
  buttonHandler();
  fadeHandler();
}
