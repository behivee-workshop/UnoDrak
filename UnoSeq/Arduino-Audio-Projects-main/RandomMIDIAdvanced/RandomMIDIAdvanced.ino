#include <Control_Surface.h>

USBMIDI_Interface midi;

const int cMajorNotes[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84};
const int chords[][6] = {
  {36, 60, 71, 62, 64, 67},
  {38, 62, 60, 64, 65, 69},
  {40, 64, 62, 65, 67, 71},
  {41, 65, 64, 67, 69, 60},
  {43, 67, 65, 69, 71, 62},
  {45, 69, 67, 71, 60, 64}
};
const int kickPattern[] = {36, 0, 0, 0, 38, 0, 0, 0, 36, 0, 0, 0, 38, 0, 0, 0};
const int hatPattern[] = {42, 0, 42, 0, 42, 0, 42, 42, 42, 0, 42, 0, 42, 0, 42, 46};

const unsigned long beatLength = 500;
const unsigned long sectionLength = beatLength * 16;//64

unsigned long previousMelodyTime = 0;
int melodyInterval = 0;
int currentMelodyNote = -1;
bool melodyNoteOn = false;

unsigned long previousMelody2Time = 0;
int melody2Interval = 0;
int currentMelody2Note = -1;
bool melody2NoteOn = false;

unsigned long previousBassTime = 0;
int currentBassNote = -1;
bool bassNoteOn = false;

int currentChord[3][6] = {{-1,-1,-1,-1,-1,-1}, {-1,-1,-1,-1,-1,-1}, {-1,-1,-1,-1,-1,-1}};
bool chordNoteOn[3] = {false, false, false};
bool padActive[3] = {true, false, false};

unsigned long previousDrumTime = 0;
unsigned long previousHatTime = 0;
unsigned long previousSectionTime = 0;
int currentSection = 0;
int drumMode = 0;
bool drumsActive = true;

unsigned long songStart = 0;

void turnOffPad(int pad) {
  if (chordNoteOn[pad]) {
    Channel ch = (pad == 0) ? CHANNEL_3 : (pad == 1) ? CHANNEL_6 : CHANNEL_7;
    for (int i = 0; i < 6; i++) {
      midi.sendNoteOff({currentChord[pad][i], ch}, 0);
    }
    chordNoteOn[pad] = false;
  }
}

void setup() {
  Control_Surface.begin();
  randomSeed(analogRead(A0));
  
  songStart = millis();
  melodyInterval = beatLength * 1;
  melody2Interval = beatLength * 2;
  
  int chordIndex = random(0, 6);
  currentBassNote = chords[chordIndex][0];
  midi.sendNoteOn({currentBassNote, CHANNEL_2}, 100);
  bassNoteOn = true;
  
  for (int i = 0; i < 6; i++) {
    currentChord[0][i] = chords[chordIndex][i];
    midi.sendNoteOn({currentChord[0][i], CHANNEL_3}, 60);
  }
  chordNoteOn[0] = true;
  
  previousBassTime = songStart;
  previousSectionTime = songStart;
  previousDrumTime = songStart;
  previousHatTime = songStart;
}

void loop() {
  Control_Surface.loop();
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - songStart;
  unsigned long currentBeat = elapsed / beatLength;
  
  if (currentTime - previousSectionTime >= sectionLength) {
    previousSectionTime = currentTime;
    currentSection = random(0, 8);
    drumMode = random(0, 4);
    drumsActive = random(0, 100) < 85;
    
    int padConfig = random(0, 6);
    padActive[0] = (padConfig == 0 || padConfig == 1 || padConfig == 3);
    padActive[1] = (padConfig == 1 || padConfig == 2 || padConfig == 4);
    padActive[2] = (padConfig == 3 || padConfig == 4 || padConfig == 5);
    
    for (int p = 0; p < 3; p++) {
      if (!padActive[p]) turnOffPad(p);
    }
  }
  
  if (currentTime - previousMelodyTime >= melodyInterval) {
    previousMelodyTime = songStart + (currentBeat * beatLength);
    
    if (melodyNoteOn) {
      midi.sendNoteOff({currentMelodyNote, CHANNEL_1}, 0);
      melodyNoteOn = false;
    }
    
    if (random(0, 100) < 70) {
      currentMelodyNote = cMajorNotes[random(0, 15)];
      midi.sendNoteOn({currentMelodyNote, CHANNEL_1}, currentSection == 3 ? 90 : 127);
      melodyNoteOn = true;
    }
    
    int randomBeats[] = {1, 2, 3, 4};
    melodyInterval = beatLength * randomBeats[random(0, 4)];
  }
  
  if (currentTime - previousMelody2Time >= melody2Interval) {
    previousMelody2Time = songStart + (currentBeat * beatLength);
    
    if (melody2NoteOn) {
      midi.sendNoteOff({currentMelody2Note, CHANNEL_5}, 0);
      melody2NoteOn = false;
    }
    
    if (random(0, 100) < 70) {
      currentMelody2Note = cMajorNotes[random(0, 15)];
      midi.sendNoteOn({currentMelody2Note, CHANNEL_5}, currentSection == 3 ? 90 : 127);
      melody2NoteOn = true;
    }
    
    int randomBeats[] = {1, 2, 3, 4};
    melody2Interval = beatLength * randomBeats[random(0, 4)];
  }
  
  if (currentTime - previousBassTime >= beatLength * 16) {
    previousBassTime = songStart + (currentBeat * beatLength);
    
    if (bassNoteOn) {
      midi.sendNoteOff({currentBassNote, CHANNEL_2}, 0);
      bassNoteOn = false;
    }
    
    for (int p = 0; p < 3; p++) turnOffPad(p);
    
    if (currentSection != 3) {
      int chordIndex = random(0, 6);
      
      currentBassNote = chords[chordIndex][0];
      midi.sendNoteOn({currentBassNote, CHANNEL_2}, 100);
      bassNoteOn = true;
      
      for (int p = 0; p < 3; p++) {
        if (padActive[p]) {
          Channel ch = (p == 0) ? CHANNEL_3 : (p == 1) ? CHANNEL_6 : CHANNEL_7;
          for (int i = 0; i < 6; i++) {
            currentChord[p][i] = chords[chordIndex][i];
            midi.sendNoteOn({currentChord[p][i], ch}, 60);
          }
          chordNoteOn[p] = true;
        }
      }
    }
  }
  
  if (currentTime - previousDrumTime >= beatLength && drumsActive && currentSection != 3 && currentSection != 5) {
    previousDrumTime = songStart + (currentBeat * beatLength);
    
    int drumStep = currentBeat % 16;
    if (kickPattern[drumStep] != 0) {
      midi.sendNoteOn({kickPattern[drumStep], CHANNEL_4}, 100);
      midi.sendNoteOff({kickPattern[drumStep], CHANNEL_4}, 0);
      midi.sendNoteOn({kickPattern[drumStep], CHANNEL_8}, 100);
      midi.sendNoteOff({kickPattern[drumStep], CHANNEL_8}, 0);
    }
  }
  
  if (currentTime - previousHatTime >= beatLength / 2 && drumsActive && currentSection != 5 && drumMode != 3) {
    unsigned long halfBeat = elapsed / (beatLength / 2);
    previousHatTime = songStart + (halfBeat * (beatLength / 2));
    
    if (drumMode == 0 || drumMode == 1) {
      int hatStep = halfBeat % 16;
      if (hatPattern[hatStep] != 0) {
        midi.sendNoteOn({hatPattern[hatStep], CHANNEL_4}, 80);
        midi.sendNoteOff({hatPattern[hatStep], CHANNEL_4}, 0);
        midi.sendNoteOn({hatPattern[hatStep], CHANNEL_8}, 80);
        midi.sendNoteOff({hatPattern[hatStep], CHANNEL_8}, 0);
      }
    } else if (drumMode == 2) {
      if (currentBeat % 2 == 0) {
        midi.sendNoteOn({42, CHANNEL_4}, 60);
        midi.sendNoteOff({42, CHANNEL_4}, 0);
        midi.sendNoteOn({42, CHANNEL_8}, 60);
        midi.sendNoteOff({42, CHANNEL_8}, 0);
      }
    }
  }
}