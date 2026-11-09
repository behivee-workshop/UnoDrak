#include <Control_Surface.h>

USBMIDI_Interface midi;
const int cMajorNotes[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84};

void setup() {
  Control_Surface.begin();
  randomSeed(analogRead(A0));
}

void loop() {
  Control_Surface.loop();
  int randomNote = cMajorNotes[random(0, 15)];
  int randomDelay = random(200, 800);
  if (random(0, 100) < 70) {
    midi.sendNoteOn({randomNote, CHANNEL_1}, 127);
    delay(randomDelay);
    midi.sendNoteOff({randomNote, CHANNEL_1}, 0);
  } else {
    delay(randomDelay);
  }
  delay(100);
}