// ============================================================
//  Simon Says — Arduino (non-blocking state machine)
//  4 LEDs and 4 buttons (INPUT_PULLUP: pressed = LOW)
//  Colours are numbered 0-3: 0 = red, 1 = green, 2 = yellow, 3 = blue
//
//  loop() never waits. Each pass it reads the buttons, then the
//  current state checks the clock / the buttons and either does
//  nothing or moves to the next state.
// ============================================================

// ---------- Settings ----------
const int MAX_LEVEL = 15;                   // longest sequence; finishing it wins the game
const unsigned long SHOW_ON_MS     = 1000;  // each colour lit during playback
const unsigned long SHOW_GAP_MS    = 500;   // gap between colours during playback
const unsigned long START_PAUSE_MS = 1000;  // pause before a new game's first colour
const unsigned long PASS_ON_MS     = 500;   // pass flash: all LEDs on
const unsigned long PASS_OFF_MS    = 1000;  // pause after the pass flash
const unsigned long FAIL_STEP_MS   = 200;   // fail flashes: each on/off step
const unsigned long WIN_STEP_MS    = 100;   // win chase: each LED
const unsigned long END_PAUSE_MS   = 1000;  // pause after a fail or win animation
const unsigned long DEBOUNCE_MS    = 30;    // a reading must hold this long to count
const bool DEBUG_SHOW_SEQUENCE = false;     // true = print the sequence to Serial (cheat mode)

// ---------- Pins ----------
// The array index IS the colour number.
const int NUM_COLORS = 4;
const int ledPins[NUM_COLORS]    = {10, 9, 8, 7};   // red, green, yellow, blue
const int buttonPins[NUM_COLORS] = { 5, 4, 3, 2};   // red, green, yellow, blue
const char COLOR_LETTERS[NUM_COLORS + 1] = "RGYB";  // for Serial messages

// ---------- States ----------
enum GameState { START, SHOW_ON, SHOW_GAP, WAIT_INPUT, CORRECT, INCORRECT, WIN };
GameState state;
unsigned long stateStart;   // millis() when the current state (or animation step) began
int animStep;               // step counter for the multi-step animations

// ---------- Game data ----------
int colorList[MAX_LEVEL];   // the sequence (values 0-3)
int counter = 0;            // how many colours are in the sequence = current level
int showIndex = 0;          // which colour playback is on
int inputIndex = 0;         // which colour the player must press next
int heldColor = -1;         // button currently held during WAIT_INPUT (-1 = none)

// ---------- Button debouncing (one entry per colour) ----------
int stableState[NUM_COLORS];              // last accepted state (HIGH = up, LOW = down)
int lastReading[NUM_COLORS];              // last raw reading
unsigned long lastChange[NUM_COLORS];     // millis() when the raw reading last changed
int pressedNow = -1;    // colour pressed on THIS pass through loop(), or -1
int releasedNow = -1;   // colour released on THIS pass through loop(), or -1

// ============================================================
void setup() {
  Serial.begin(9600);

  for (int c = 0; c < NUM_COLORS; c = c + 1) {
    pinMode(buttonPins[c], INPUT_PULLUP);
    pinMode(ledPins[c], OUTPUT);
    stableState[c] = HIGH;   // assume all buttons start released
    lastReading[c] = HIGH;
    lastChange[c] = 0;
  }

  // Seed the random generator ONCE, from the noise on an unconnected pin
  randomSeed(analogRead(A0));

  newGame();
}

void loop() {
  updateButtons();   // sets pressedNow / releasedNow for this pass

  switch (state) {

    case START:
      // Short pause before the first colour of a new game
      if (timeUp(START_PAUSE_MS)) {
        startRound();
      }
      break;

    case SHOW_ON:
      // The current colour is lit; turn it off when its time is up
      if (timeUp(SHOW_ON_MS)) {
        digitalWrite(ledPins[colorList[showIndex]], LOW);
        goTo(SHOW_GAP);
      }
      break;

    case SHOW_GAP:
      // All off between colours; then the next colour, or the player's turn
      if (timeUp(SHOW_GAP_MS)) {
        showIndex = showIndex + 1;
        if (showIndex < counter) {
          digitalWrite(ledPins[colorList[showIndex]], HIGH);
          goTo(SHOW_ON);
        }
        else {
          heldColor = -1;   // ignore any button that was already held during playback
          goTo(WAIT_INPUT);
        }
      }
      break;

    case WAIT_INPUT:
      // A press lights that colour's LED (only one button counts at a time)
      if (pressedNow >= 0 && heldColor < 0) {
        heldColor = pressedNow;
        digitalWrite(ledPins[heldColor], HIGH);
      }
      // The release is what counts as the player's answer
      if (releasedNow >= 0 && releasedNow == heldColor) {
        digitalWrite(ledPins[heldColor], LOW);
        checkAnswer(heldColor);
        heldColor = -1;
      }
      break;

    case CORRECT:
      // Step 0: all on (set on entry). Step 1: all off, pause. Then next round or win.
      if (animStep == 0 && timeUp(PASS_ON_MS)) {
        setAll(LOW);
        nextStep();
      }
      else if (animStep == 1 && timeUp(PASS_OFF_MS)) {
        if (counter == MAX_LEVEL) {
          Serial.println("WIN");
          digitalWrite(ledPins[0], HIGH);   // first LED of the chase
          goTo(WIN);
        }
        else {
          addNewColor();
          startRound();
        }
      }
      break;

    case INCORRECT:
      // Steps 0-5 alternate on/off (3 flashes), then a pause, then a new game
      if (animStep < 5) {
        if (timeUp(FAIL_STEP_MS)) {
          nextStep();
          setAll(animStep % 2 == 0 ? HIGH : LOW);   // even steps on, odd steps off
        }
      }
      else if (timeUp(FAIL_STEP_MS + END_PAUSE_MS)) {
        newGame();
      }
      break;

    case WIN:
      // Steps 0-11 light one LED each, chasing around 3 times; then a pause
      if (animStep < 12) {
        if (timeUp(WIN_STEP_MS)) {
          digitalWrite(ledPins[animStep % NUM_COLORS], LOW);
          nextStep();
          if (animStep < 12) {
            digitalWrite(ledPins[animStep % NUM_COLORS], HIGH);
          }
        }
      }
      else if (timeUp(END_PAUSE_MS)) {
        newGame();
      }
      break;
  }
}

// ============================================================
//  State helpers
// ============================================================

// Every state change goes through here, so the timer always restarts.
void goTo(GameState next) {
  state = next;
  stateStart = millis();
  animStep = 0;
}

// True once `ms` milliseconds have passed in the current state or step.
bool timeUp(unsigned long ms) {
  return millis() - stateStart >= ms;
}

// Moves an animation to its next step and restarts the timer.
void nextStep() {
  animStep = animStep + 1;
  stateStart = millis();
}

// Clears everything and waits in START before the first colour.
void newGame() {
  setAll(LOW);
  for (int i = 0; i < MAX_LEVEL; i = i + 1) {
    colorList[i] = 0;
  }
  counter = 0;
  addNewColor();
  Serial.println("NEW GAME");
  goTo(START);
}

// Starts playback of the current sequence from the first colour.
// Called from START (new game) and CORRECT (next round).
void startRound() {
  showIndex = 0;
  inputIndex = 0;

  if (DEBUG_SHOW_SEQUENCE) {
    Serial.print("Sequence: ");
    for (int i = 0; i < counter; i = i + 1) {
      Serial.print(COLOR_LETTERS[colorList[i]]);
    }
    Serial.println();
  }

  digitalWrite(ledPins[colorList[0]], HIGH);   // entry action: light the first colour
  goTo(SHOW_ON);
}

// Judges one completed press during WAIT_INPUT.
void checkAnswer(int pressed) {
  Serial.print(COLOR_LETTERS[pressed]);

  if (pressed != colorList[inputIndex]) {
    Serial.println("N");   // e.g. "GN" = pressed green, wrong
    Serial.println("N");   // round failed
    setAll(HIGH);          // entry action: first fail flash
    goTo(INCORRECT);
    return;
  }

  Serial.println("Y");     // e.g. "GY" = pressed green, right
  inputIndex = inputIndex + 1;
  if (inputIndex == counter) {
    Serial.println("Y");   // whole round correct
    setAll(HIGH);          // entry action: pass flash
    goTo(CORRECT);
  }
  // otherwise stay in WAIT_INPUT for the next colour
}

// Adds one random colour to the end of the sequence.
void addNewColor() {
  if (counter >= MAX_LEVEL) {   // guard: never write past the end of the array
    return;
  }
  colorList[counter] = random(0, NUM_COLORS);
  counter = counter + 1;
}

// Turns all LEDs on (HIGH) or off (LOW).
void setAll(int level) {
  for (int c = 0; c < NUM_COLORS; c = c + 1) {
    digitalWrite(ledPins[c], level);
  }
}

// ============================================================
//  Input: non-blocking debounce + edge detection
// ============================================================

// Reads every button once. A new reading only counts after it has held
// steady for DEBOUNCE_MS. Sets pressedNow / releasedNow when a button's
// accepted state changes on this pass.
void updateButtons() {
  pressedNow = -1;
  releasedNow = -1;

  for (int c = 0; c < NUM_COLORS; c = c + 1) {
    int reading = digitalRead(buttonPins[c]);

    // Raw reading changed (real change or bounce): restart its timer
    if (reading != lastReading[c]) {
      lastReading[c] = reading;
      lastChange[c] = millis();
    }

    // Held steady long enough and different from what we accepted: it's real
    if (millis() - lastChange[c] >= DEBOUNCE_MS && reading != stableState[c]) {
      stableState[c] = reading;
      if (reading == LOW) {
        pressedNow = c;     // edge: up → down
      }
      else {
        releasedNow = c;    // edge: down → up
      }
    }
  }
}
