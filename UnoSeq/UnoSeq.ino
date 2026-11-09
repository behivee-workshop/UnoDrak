// UnoSeq - 4-track, 16-step drum sequencer slaved to external MIDI clock.
// MIDI IN:   D0 (RX1 / Serial1)      - start/stop/clock from the Arturia
// Audio:     A0, via Mozzi's built-in DAC output
// OLED:      128x64 I2C - SDA=A4, SCL=A5 (Wire), address 0x3C
// Buttons:   D2=left, D3=right (move step cursor), D4=up, D5=down (move
//            track cursor), D6=toggle step on/off. All INPUT_PULLUP to GND.
//
// Tracks top to bottom: Kick, Snare, Closed Hat, Open Hat.
// HelloDrum-arduino-Library turned out to contain no audio data at all (it's
// a piezo-trigger/MIDI-out library for e-drum pads) - these 4 voices are
// synthesized with Mozzi instead (oscillator + noise + decay envelopes),
// which is what Mozzi is actually for.
#include <MIDI.h>
#define MOZZI_CONTROL_RATE 256 // Hz - MIDI parsing, buttons, encoder, display
#include <Mozzi.h>
#include <mozzi_rand.h>
#include <Oscil.h>
#include <tables/sin256_int8.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- sequencer state ----------
enum Track { KICK = 0, SNARE, CH, OH, NUM_TRACKS };
const char *trackLabel[NUM_TRACKS] = { "K", "S", "C", "O" };
const uint8_t NUM_STEPS = 16;
uint16_t trackSteps[NUM_TRACKS] = { 0 }; // bit i = step i active

const uint8_t PIN_LEFT   = 2;
const uint8_t PIN_RIGHT  = 3;
const uint8_t PIN_UP     = 4;
const uint8_t PIN_DOWN   = 5;
const uint8_t PIN_TOGGLE = 6;
const uint8_t BUTTON_PINS[5] = { PIN_LEFT, PIN_RIGHT, PIN_UP, PIN_DOWN, PIN_TOGGLE };

uint8_t cursorStep = 0;
uint8_t cursorTrack = 0;
bool displayDirty = true;

// ---------- buttons ----------
int lastButtonState[5] = { HIGH, HIGH, HIGH, HIGH, HIGH };
unsigned long lastDebounceTime[5] = { 0 };
const unsigned long DEBOUNCE_MS = 30;

void pollButtons() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < 5; i++) {
    int s = digitalRead(BUTTON_PINS[i]);
    if (s != lastButtonState[i] && now - lastDebounceTime[i] > DEBOUNCE_MS) {
      lastDebounceTime[i] = now;
      lastButtonState[i] = s;
      if (s != LOW) continue; // only act on press

      switch (i) {
        case 0: cursorStep = (cursorStep + NUM_STEPS - 1) % NUM_STEPS; break;      // left
        case 1: cursorStep = (cursorStep + 1) % NUM_STEPS; break;                 // right
        case 2: cursorTrack = (cursorTrack + NUM_TRACKS - 1) % NUM_TRACKS; break; // up
        case 3: cursorTrack = (cursorTrack + 1) % NUM_TRACKS; break;              // down
        case 4: trackSteps[cursorTrack] ^= (1 << cursorStep); break;              // toggle
      }
      displayDirty = true;
    }
  }
}

// ---------- synthesized voices ----------
const uint32_t KICK_LEN  = (uint32_t)MOZZI_AUDIO_RATE * 200 / 1000;
const uint32_t SNARE_LEN = (uint32_t)MOZZI_AUDIO_RATE * 150 / 1000;
const uint32_t CH_LEN    = (uint32_t)MOZZI_AUDIO_RATE * 40  / 1000;
const uint32_t OH_LEN    = (uint32_t)MOZZI_AUDIO_RATE * 250 / 1000;
const uint32_t voiceLen[NUM_TRACKS] = { KICK_LEN, SNARE_LEN, CH_LEN, OH_LEN };

const int16_t voiceAmp[NUM_TRACKS] = { 45, 35, 20, 28 }; // halved again per feedback

const uint8_t LPF_SHIFT = 2;          // post-mix smoothing, see updateAudio()
const uint16_t MASTER_VOLUME = 256;   // 256 = 100%; e.g. 128 = 50%, 384 = 150%

volatile bool voiceActive[NUM_TRACKS] = { false };
volatile uint32_t voiceAge[NUM_TRACKS] = { 0 };

Oscil<SIN256_NUM_CELLS, MOZZI_AUDIO_RATE> kickOsc(SIN256_DATA);
const uint32_t KICK_SWEEP_LEN = (uint32_t)MOZZI_AUDIO_RATE * 50 / 1000; // pitch drop over 50ms

void triggerVoice(uint8_t t) {
  voiceAge[t] = 0;
  voiceActive[t] = true;
}

AudioOutput updateAudio() {
  int16_t mix = 0;

  if (voiceActive[KICK]) {
    uint32_t age = voiceAge[KICK];
    uint16_t freq = (age < KICK_SWEEP_LEN)
      ? (150 - (uint32_t)100 * age / KICK_SWEEP_LEN)
      : 50;
    kickOsc.setFreq(freq);
    int32_t amp = voiceAmp[KICK] - (int32_t)voiceAmp[KICK] * age / KICK_LEN;
    if (amp < 0) amp = 0;
    mix += (int16_t)kickOsc.next() * amp / 128;
    if (++voiceAge[KICK] >= KICK_LEN) voiceActive[KICK] = false;
  }

  for (uint8_t t = SNARE; t <= OH; t++) {
    if (!voiceActive[t]) continue;
    uint32_t age = voiceAge[t];
    int32_t amp = voiceAmp[t] - (int32_t)voiceAmp[t] * age / voiceLen[t];
    if (amp < 0) amp = 0;
    int8_t n = rand(255) - 127;
    mix += (int16_t)n * amp / 128;
    if (++voiceAge[t] >= voiceLen[t]) voiceActive[t] = false;
  }

  // One-pole low-pass: our voices are raw square/noise, this rounds off the
  // harsh edges cheaply instead of sounding buzzy. Lower LPF_SHIFT = brighter.
  static int32_t lpfState = 0;
  lpfState += (mix - lpfState) >> LPF_SHIFT;

  // Master volume: single knob instead of retuning every voiceAmp by hand.
  int32_t out = (lpfState * MASTER_VOLUME) >> 8;

  return MonoOutput::fromAlmostNBit(9, out).clip();
}

// ---------- MIDI clock slave + step sequencer ----------
const uint8_t CLOCKS_PER_STEP = 6;  // 24 PPQN / 6 = 16th-note steps
const uint8_t CLOCKS_PER_BEAT = 24;

uint32_t totalClocks = 0;
uint8_t lastPlayStep = 255;
uint8_t playStep = 0;
bool running = false;
uint32_t lastClockMicros = 0;
uint32_t intervalSum = 0;
uint8_t clockCount = 0;
float currentBpm = 0;

void onClock() {
  uint32_t now = micros();
  if (lastClockMicros != 0) {
    intervalSum += now - lastClockMicros;
    clockCount++;
    if (clockCount == CLOCKS_PER_BEAT) {
      currentBpm = 60000000.0f / (float)intervalSum;
      intervalSum = 0;
      clockCount = 0;
    }
  }
  lastClockMicros = now;

  totalClocks++;
  uint8_t step = (totalClocks / CLOCKS_PER_STEP) % NUM_STEPS;
  if (step != lastPlayStep) {
    lastPlayStep = step;
    playStep = step;
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
      if (trackSteps[t] & (1 << step)) triggerVoice(t);
    }
    // no displayDirty here: nothing playhead-related is drawn on screen, so
    // redrawing every 16th note was a full 1024-byte I2C transfer for
    // nothing - a real contributor to the background whine.
  }
}

void onStart() {
  running = true;
  totalClocks = 0;
  lastPlayStep = 255;
  clockCount = 0;
  intervalSum = 0;
  displayDirty = true;
}

void onContinue() {
  running = true;
}

void onStop() {
  running = false;
  displayDirty = true;
}

// ---------- display ----------
// Plain text grid: 'X' = active step, '-' = inactive. The cursor cell is
// shown inverted (black-on-white) instead of drawing any shape over it.
void drawDisplay() {
  display.clearDisplay();

  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    display.setCursor(0, t * 16);
    display.setTextColor(SSD1306_WHITE);
    display.print(trackLabel[t]);
    display.print(':');

    for (uint8_t s = 0; s < NUM_STEPS; s++) {
      bool on = trackSteps[t] & (1 << s);
      bool isCursor = (t == cursorTrack && s == cursorStep);
      display.setTextColor(isCursor ? SSD1306_BLACK : SSD1306_WHITE,
                            isCursor ? SSD1306_WHITE : SSD1306_BLACK);
      display.print(on ? 'X' : '-');

      if ((s + 1) % 4 == 0 && s != NUM_STEPS - 1) {
        display.setTextColor(SSD1306_WHITE); // gap between groups, never inverted
        display.print(' ');
      }
    }
  }

  display.display();
}

// Diagnostic: set to false to stop all OLED/I2C traffic after boot, to test
// whether the I2C bus is contributing to the background whine. Confirmed:
// disabling it made the whine smaller but not gone, so it's a contributor,
// not the sole source. Re-enabled now that the redundant per-step redraw
// (see onClock()) is gone and I2C is running faster.
const bool DISPLAY_ENABLED = true;

// ---------- Mozzi control hook: everything non-audio lives here ----------
void updateControl() {
  MIDI.read();
  pollButtons();
  if (DISPLAY_ENABLED && displayDirty) {
    drawDisplay();
    displayDirty = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  for (uint8_t i = 0; i < 5; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000); // fast mode: shorter bus transfers per redraw

  Serial.println("Scanning I2C bus...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  found device at 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  no I2C devices found - check SDA/SCL wiring and OLED power.");
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 begin() failed at 0x3C - if the scan above found a "
                    "device at a different address (commonly 0x3D), update the "
                    "address in this call.");
  } else {
    Serial.println("OLED initialized OK.");
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("UnoSeq booting...");
    display.display();
  }

  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleClock(onClock);
  MIDI.setHandleStart(onStart);
  MIDI.setHandleContinue(onContinue);
  MIDI.setHandleStop(onStop);

  kickOsc.setFreq(150);

  startMozzi();
}

void loop() {
  audioHook(); // required: loop() must do nothing else but this
}
