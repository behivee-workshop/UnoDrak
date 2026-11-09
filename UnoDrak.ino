// UnoDrak - MIDI clock slave, Uno R4 Minima.
// MIDI IN wired to D0 (RX1 / Serial1), USB stays free for Serial Monitor.
// Reports START/STOP/CONTINUE and BPM from an external MIDI clock master
// (e.g. an Arturia MicroFreak sequencer). Beat LED blinks in sync.
// For the full drum machine built on top of this, see UnoPops/UnoPops.ino.
#include <MIDI.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

const uint8_t CLOCKS_PER_BEAT = 24; // MIDI spec: 24 clocks per quarter note

uint32_t lastClockMicros = 0;
uint32_t intervalSum = 0;
uint8_t clockCount = 0;
uint16_t beatCount = 0;

void onBeat() {
  digitalWrite(LED_BUILTIN, beatCount % 2);
}

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
      onBeat();
    }
  }
  lastClockMicros = now;
}

void onStart() {
  Serial.println("START");
  clockCount = 0;
  intervalSum = 0;
  beatCount = 0;
}

void onContinue() {
  Serial.println("CONTINUE");
}

void onStop() {
  Serial.println("STOP"); // last printed BPM just stops updating
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleClock(onClock);
  MIDI.setHandleStart(onStart);
  MIDI.setHandleContinue(onContinue);
  MIDI.setHandleStop(onStop);
}

void loop() {
  MIDI.read();
}
