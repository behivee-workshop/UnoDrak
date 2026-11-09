// UnoPops - Korg Mini Pops-style analog drum machine, slaved to external MIDI clock.
// MIDI IN:  D0 (RX1 / Serial1)   - start/stop/clock from the Arturia
// Audio:    A0 (8-bit DAC)       - mixed drum voices
// Buttons:  D2..D9, one per voice - INPUT_PULLUP, press toggles mute
// Pot:      A1 (10k)             - selects pattern (16 available)
//
// Drum samples and the 16 rhythm patterns are Jan Ostman's O2 Mini Pops
// rhythm box, (c) DSP Synthesizers 2016, free for non-commercial use
// (http://janostman.wordpress.com), ported here byte-for-byte from
// https://github.com/duddex/GRAINS-O2Minipops - see UnoPops_jan_samples.h.
// Everything else (MIDI clock slaving, step sequencer, buttons, pot) is ours.
// For MIDI-clock-only testing (no drum machine), see ../UnoDrak.ino.
#include <MIDI.h>
#include "FspTimer.h"
#include "UnoPops_jan_samples.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

// ---------- voices ----------
// Enum order matches the original pattern bit order exactly (bit0=QU
// .. bit7=GU), so JAN_PATTERNS bytes are usable as step masks unmodified.
enum Voice { QU = 0, CY, MA, CW, CL, BD, BG2, GU, NUM_VOICES };
const uint8_t buttonPins[NUM_VOICES] = { 2, 3, 4, 5, 6, 7, 8, 9 };
const char *voiceNames[NUM_VOICES]   = { "QU", "CY", "MA", "CW", "CL", "BD", "BG2", "GU" };

const uint8_t *const VOICE_TABLES[NUM_VOICES] = {
  QU_TABLE, CY_TABLE, MA_TABLE, CW_TABLE, CL_TABLE, BD_TABLE, BG2_TABLE, GU_TABLE
};
const uint16_t VOICE_LENGTHS[NUM_VOICES] = {
  QU_LEN, CY_LEN, MA_LEN, CW_LEN, CL_LEN, BD_LEN, BG2_LEN, GU_LEN
};

// Original firmware plays these back at 40kHz; matching that keeps pitch
// and duration as authored instead of pitching everything down.
const uint32_t SAMPLE_RATE_HZ = 40000;

volatile bool     voiceActive[NUM_VOICES]    = { false };
volatile uint16_t voicePlayIndex[NUM_VOICES] = { 0 };

FspTimer audioTimer;
bool audioTimerRunning = false;

void triggerVoice(uint8_t v) {
  noInterrupts();
  voicePlayIndex[v] = 0;
  voiceActive[v] = true;
  interrupts();
  if (!audioTimerRunning) {
    audioTimer.start();
    audioTimerRunning = true;
  }
}

// ---------- audio ISR ----------
const uint8_t OUTPUT_GAIN_SHIFT = 1; // 1 = 50% volume; raise to quiet further

void audioIsr(timer_callback_args_t *p_args) {
  int32_t mix = 0;

  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (!voiceActive[v]) continue;

    uint16_t idx = voicePlayIndex[v];
    uint8_t s = pgm_read_byte(&VOICE_TABLES[v][idx]);
    mix += (int16_t)s - 128;

    idx++;
    voicePlayIndex[v] = idx;
    if (idx >= VOICE_LENGTHS[v]) voiceActive[v] = false;
  }

  mix >>= OUTPUT_GAIN_SHIFT;
  if (mix > 127) mix = 127;
  if (mix < -128) mix = -128;
  analogWrite(A0, 128 + mix);
}

bool beginAudioTimer(float rateHz) {
  uint8_t timer_type = GPT_TIMER;
  int8_t tindex = FspTimer::get_available_timer(timer_type);
  if (tindex < 0) tindex = FspTimer::get_available_timer(timer_type, true);
  if (tindex < 0) return false;

  FspTimer::force_use_of_pwm_reserved_timer();
  if (!audioTimer.begin(TIMER_MODE_PERIODIC, timer_type, tindex, rateHz, 0.0f, audioIsr)) return false;
  if (!audioTimer.setup_overflow_irq()) return false;
  if (!audioTimer.open()) return false;
  if (!audioTimer.start()) return false;
  audioTimerRunning = true;
  return true;
}

// Stops the audio interrupt entirely once every voice has finished decaying,
// instead of leaving it ticking on a silent, flat 128 forever. Restarted
// instantly by triggerVoice(). Try this first if you still hear a background
// whine only while nothing is playing - it isolates whether the ISR itself
// is the noise source versus the analog output stage.
unsigned long lastActiveMillis = 0;

void updateAudioTimerIdle() {
  bool anyActive = false;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (voiceActive[v]) { anyActive = true; break; }
  }
  if (anyActive) {
    lastActiveMillis = millis();
  } else if (audioTimerRunning && millis() - lastActiveMillis > 50) {
    audioTimer.stop();
    audioTimerRunning = false;
  }
}

// ---------- patterns ----------
const uint8_t CLOCKS_PER_STEP = 6;   // 24 PPQN / 6 = 16th-note steps
const uint8_t CLOCKS_PER_BEAT = 24;  // for BPM calc + beat LED

bool muted[NUM_VOICES] = { false };
uint8_t currentPattern = 0;
uint32_t totalClocks = 0;
uint8_t lastStep = 255; // sentinel so step 0 always fires on start

void triggerStepHits(uint8_t step) {
  uint8_t mask = JAN_PATTERNS[currentPattern][step];
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if ((mask & (1 << v)) && !muted[v]) triggerVoice(v);
  }
}

// ---------- MIDI clock slave ----------
uint32_t lastClockMicros = 0;
uint32_t intervalSum = 0;
uint8_t clockCount = 0;
uint16_t beatCount = 0;

void onClock() {
  uint32_t now = micros();
  if (lastClockMicros != 0) {
    intervalSum += now - lastClockMicros;
    clockCount++;
    if (clockCount == CLOCKS_PER_BEAT) {
      float bpm = 60000000.0f / (float)intervalSum;
      Serial.print("BPM: ");
      Serial.println(bpm, 1);
      intervalSum = 0;
      clockCount = 0;
      beatCount++;
      digitalWrite(LED_BUILTIN, beatCount % 2);
    }
  }
  lastClockMicros = now;

  totalClocks++;
  uint8_t steps = JAN_PATTERN_STEPS[currentPattern];
  uint8_t step = (totalClocks / CLOCKS_PER_STEP) % steps;
  if (step != lastStep) {
    lastStep = step;
    triggerStepHits(step);
  }
}

void onStart() {
  Serial.println("START");
  clockCount = 0;
  intervalSum = 0;
  beatCount = 0;
  totalClocks = 0;
  lastStep = 255;
}

void onContinue() {
  Serial.println("CONTINUE"); // resumes from current step, no reset
}

void onStop() {
  Serial.println("STOP"); // step stays frozen; any decaying voice rings out
}

// ---------- buttons + pot ----------
int lastButtonState[NUM_VOICES];
unsigned long lastDebounceTime[NUM_VOICES] = { 0 };
const unsigned long DEBOUNCE_MS = 30;

void pollButtons() {
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    int state = digitalRead(buttonPins[v]);
    if (state != lastButtonState[v] && millis() - lastDebounceTime[v] > DEBOUNCE_MS) {
      lastDebounceTime[v] = millis();
      lastButtonState[v] = state;
      if (state == LOW) { // pressed, active low
        muted[v] = !muted[v];
        Serial.print(voiceNames[v]);
        Serial.println(muted[v] ? " muted" : " unmuted");
      }
    }
  }
}

void pollPot() {
  int potVal = analogRead(A1); // 0-1023
  uint8_t newPattern = (uint32_t)potVal * JAN_NUM_PATTERNS / 1024;
  if (newPattern >= JAN_NUM_PATTERNS) newPattern = JAN_NUM_PATTERNS - 1;
  if (newPattern != currentPattern) {
    currentPattern = newPattern;
    lastStep = 255; // re-sync step 0 on the new pattern's own length
    Serial.print("Pattern: ");
    Serial.println(JAN_PATTERN_NAMES[currentPattern]);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    pinMode(buttonPins[v], INPUT_PULLUP);
    lastButtonState[v] = HIGH;
  }

  analogWrite(A0, 128); // idle at mid-rail, default 8-bit DAC resolution matches the sample tables

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleClock(onClock);
  MIDI.setHandleStart(onStart);
  MIDI.setHandleContinue(onContinue);
  MIDI.setHandleStop(onStop);

  if (!beginAudioTimer(SAMPLE_RATE_HZ)) {
    Serial.println("Audio timer failed to start!");
  }
}

void loop() {
  MIDI.read();
  pollButtons();
  pollPot();
  updateAudioTimerIdle();
}
