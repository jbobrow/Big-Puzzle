// Distributed Win & State Change Example
// 1. Shared Game Mode Across All Blinks
// 2. Distance Based Collective Win State
// 3. Time Synchronized Animation

// =========================================================
// ===================== SECRET CODE =======================
// =========================================================
#define CODE_LENGTH 4 // NUMBER OF DIGITS IN THE SECRET CODE
byte secretCode[CODE_LENGTH] = {4, 2, 4, 2}; // REPLACE THIS
// =========================================================
// =========================================================
// =========================================================
// =========================================================

#define NUM_GAME_COLORS 4
#define CODE_PULSE_DURATION         2000 // MILLISECONDS FOR BLINK PERIOD
#define CODE_DIGIT_SPACE_DURATION   2000 // MILLISECONDS FOR SPACE BETWEEN DIGITS
#define WIN_CELEBRATE_DURATION      4000 // MILLISECONDS FOR FIREWORKS
Timer winAnimationTimer;
byte winAnimationPlayhead = 0;

#define TANGERINE makeColorHSB(22,255,255)
#define LEMON     makeColorHSB(49,255,255)
#define MINT      makeColorHSB(99,255,255)
#define GRAPE     makeColorHSB(200,255,255) 
#define LIME      makeColorHSB(82,255,255)
#define BLUEBERRY makeColorHSB(160,255,255)

Color digitColors[CODE_LENGTH] = {TANGERINE, LEMON, MINT, GRAPE};  // ARRAY OF COLORS FOR EACH DIGIT POSITION
Color gameColors[NUM_GAME_COLORS+1] = {OFF, TANGERINE, LEMON, MINT, GRAPE};  // ARRAY OF COLORS FOR PUZZLE

#define COMM_INERT           0
#define COMM_SETUP_GO        1
#define COMM_SETUP_RESOLVE   2
#define COMM_PLAY_GO         3
#define COMM_PLAY_RESOLVE    4
#define COMM_PLAY_BASE       5  // PLAY mode uses values 5-20 (base + distance)
#define COMM_WIN_GO          21
#define COMM_WIN_RESOLVE     22

#define MAX_SOLVE_DISTANCE  15  // Maximum distance we can represent

#define PERIOD_DURATION 2000
#define BUFFER_DURATION 200

Timer syncTimer;
byte neighborSyncState[6];
byte syncVal = 0;

enum State {
  INERT,
  GO,
  RESOLVE
};

enum Mode {
  SETUP,
  PLAY,
  WIN
};

State signalState = INERT;

Mode gameMode = SETUP;//the default mode when the game begins

byte inverseDistanceFromUnsolved = 0;  // Distance propagation for win detection

byte brightness; // synchronized brightness

bool amISolved = false;

void setup() {

}

void loop() {

  // sync the animations
  syncLoop();
  brightness = sin8_C(map(syncTimer.getRemaining(), 0, PERIOD_DURATION, 0, 255));

  // The following listens for and updates game state across all Blinks
  switch (signalState) {
    case INERT:
      inertLoop();
      break;
    case GO:
      goLoop();
      break;
    case RESOLVE:
      resolveLoop();
      break;
  }

  // The following is loops for each of our game states
  switch (gameMode) {
    case SETUP:
      setupLoop();
      break;
    case PLAY:
      playLoop();
      break;
    case WIN:
      winLoop();
      break;
  }

  // communicate with neighbors
  // share both signalState (i.e. when to change) and the game mode
  byte sendData = getBroadcastValue();  
  setValueSentOnAllFaces(sendData);

  resetUserInputs();
}

/*
   Mode 1
*/
void setupLoop() {

  // press to start
  if (buttonPressed()) {
    changeMode(PLAY);  // change game mode on all Blinks
  }

  setColor(dim(RED,brightness));
}

/*
   Mode 2
*/
void playLoop() {

  // button press = individual solved
  // listen for collective solution

  // changeMode(WIN);

  // check and dump button pressed during gameplay
  if (buttonPressed()) {
    amISolved = !amISolved;
  }

  // check collective win condition
  updateWinDistance();

  if(amISolved && inverseDistanceFromUnsolved == 0) {
    changeMode(WIN);
  }

  if(amISolved) {
    // setColor(WHITE);
    Color distColor = makeColorHSB(16 * (MAX_SOLVE_DISTANCE - inverseDistanceFromUnsolved), 255, 255);
    setColor(dim(distColor,brightness));
  }
  else {
    setColor(OFF);
  }
}

/*
   Mode 3
*/
void winLoop() {

  if (buttonPressed()) {
    changeMode(WIN);
  }

  if (buttonDoubleClicked()) {
    changeMode(SETUP);
  }

  if(winAnimationTimer.isExpired()){
    // winAnimationPlayhead++;
    winAnimationPlayhead = 1 + (winAnimationPlayhead % CODE_LENGTH); // ITERATES 1 - CODE_LENGTH
    setWinAnimationTimer();
  }

  // switch(winAnimationPlayhead) {
  //   case 0: setColor(ORANGE); break;
  //   case 1: setColor(YELLOW); break;
  //   case 2: setColor(GREEN); break;
  //   case 3: setColor(CYAN); break;
  //   case 4: setColor(BLUE); break;
  //   default: setColor(MAGENTA);
  // }
  displayWinAnimation();

  // setColor(dim(BLUE,brightness));
}

uint8_t easeInHelper(uint8_t A) {
    float t = A / 255.0f;
    float eased = sqrt(t);
    return (uint8_t)(eased * 255.0f + 0.5f); // round to nearest
}

void displayWinAnimation() {
  switch(winAnimationPlayhead) {
    case 0:
      { 
        byte hue = map(winAnimationTimer.getRemaining() % (WIN_CELEBRATE_DURATION / 2), 0, WIN_CELEBRATE_DURATION / 2, 0, 255);
        Color rainbowCol = makeColorHSB(hue,255,255);
        if(winAnimationTimer.getRemaining() > WIN_CELEBRATE_DURATION / 2) {
          setColor(rainbowCol);
        }
        else {
          byte bri = map(winAnimationTimer.getRemaining(), 0, WIN_CELEBRATE_DURATION/2, 0, 255);
          bri = easeInHelper(bri);
          FOREACH_FACE(f){
            setColorOnFace(dim(random(3)==0 ? rainbowCol : OFF, bri), f);
          }
        }
      }
      break;
    case 1:
    case 2:
    case 3:
    case 4:
      {
        if(winAnimationTimer.getRemaining() <= CODE_DIGIT_SPACE_DURATION) {
          setColor(OFF);
        }
        else {
         byte bri = sin8_C(192 + 255*winAnimationTimer.getRemaining()/CODE_PULSE_DURATION);
         setColor(dim(digitColors[winAnimationPlayhead-1], bri)); 
        }
      }
      break;
    default:
      setColor(MAGENTA);
  }
}

void setWinAnimationTimer() {
  uint32_t duration;
  switch(winAnimationPlayhead) {
    case 0:
      winAnimationTimer.set(WIN_CELEBRATE_DURATION);
      break;
    case 1:
      duration = secretCode[0]*(CODE_PULSE_DURATION)+CODE_DIGIT_SPACE_DURATION;
      winAnimationTimer.set(duration);
      break;
    case 2:
      duration = secretCode[1]*(CODE_PULSE_DURATION)+CODE_DIGIT_SPACE_DURATION;
      winAnimationTimer.set(duration);
      break;
    case 3:
      duration = secretCode[2]*(CODE_PULSE_DURATION)+CODE_DIGIT_SPACE_DURATION;
      winAnimationTimer.set(duration);
      break;
    case 4:
      duration = secretCode[3]*(CODE_PULSE_DURATION)+CODE_DIGIT_SPACE_DURATION;
      winAnimationTimer.set(duration);
      break;
    default:
      winAnimationTimer.never();
  }
}


/*
   pass this a game mode to switch to
*/
void changeMode( byte mode ) {
  gameMode = mode;  // change my own mode
  signalState = GO; // signal my neighbors

  // handle any items that a game should do once when it changes
  if (gameMode == SETUP) {
    // reset puzzle
    amISolved = false;
  }
  else if (gameMode == PLAY) {
    // initiate signature swap
  }
  else if (gameMode == WIN) {
    // reset win animation
    winAnimationPlayhead = 0;
    setWinAnimationTimer();
  }
}


/*
   This loop looks for a GO signalState
   Also gets the new gameMode
*/
void inertLoop() {

  //listen for neighbors in GO
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {//a neighbor!
      if (getSignalState(getLastValueReceivedOnFace(f)) == GO) {//a neighbor saying GO!
        byte neighborGameMode = getGameMode(getLastValueReceivedOnFace(f));
        changeMode(neighborGameMode);
      }
    }
  }
}

/*
   If all of my neighbors are in GO or RESOLVE, then I can RESOLVE
*/
void goLoop() {
  signalState = RESOLVE;//I default to this at the start of the loop. Only if I see a problem does this not happen

  //look for neighbors who have not heard the GO news
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {//a neighbor!
      if (getSignalState(getLastValueReceivedOnFace(f)) == INERT) {//This neighbor doesn't know it's GO time. Stay in GO
        signalState = GO;
      }
    }
  }
}

/*
   This loop returns me to inert once everyone around me has RESOLVED
   Now receive the game mode
*/
void resolveLoop() {
  signalState = INERT;//I default to this at the start of the loop. Only if I see a problem does this not happen

  //look for neighbors who have not moved to RESOLVE
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {//a neighbor!
      if (getSignalState(getLastValueReceivedOnFace(f)) == GO) {//This neighbor isn't in RESOLVE. Stay in RESOLVE
        signalState = RESOLVE;
      }
    }
  }
}

byte getSignalState(byte data) {
    byte val = data & 31;
    switch(val) {
      case COMM_INERT:          return INERT;
      case COMM_SETUP_GO:       return GO;
      case COMM_SETUP_RESOLVE:  return RESOLVE;
      case COMM_PLAY_GO:        return GO;
      case COMM_PLAY_RESOLVE:   return RESOLVE;
      case COMM_WIN_GO:         return GO;
      case COMM_WIN_RESOLVE:    return RESOLVE;
      default: {
        // PLAY mode distance values (COMM_PLAY_BASE through COMM_PLAY_BASE + MAX_SOLVE_DISTANCE)
        if (val >= COMM_PLAY_BASE && val < COMM_WIN_GO) {
          return INERT;  // During stable PLAY with distance encoding, we're in INERT state
        }
        return INERT;
      }
  }
}

byte getGameMode(byte data) {
    byte val = data & 31;
    switch(val) {
      case COMM_INERT:          return SETUP;
      case COMM_SETUP_GO:       return SETUP;
      case COMM_SETUP_RESOLVE:  return SETUP;
      case COMM_PLAY_GO:        return PLAY;
      case COMM_PLAY_RESOLVE:   return PLAY;
      case COMM_WIN_GO:         return WIN;
      case COMM_WIN_RESOLVE:    return WIN;
      default: {
        // PLAY mode distance values (COMM_PLAY_BASE through COMM_PLAY_BASE + MAX_SOLVE_DISTANCE)
        if (val >= COMM_PLAY_BASE && val < COMM_WIN_GO) {
          return PLAY;
        }
        return SETUP;
      }
  }
}

byte getBroadcastValue() {
  byte val;
  if(signalState == INERT) {
      switch(gameMode) {
        case SETUP: val = COMM_INERT; break;
        case PLAY: val = COMM_PLAY_BASE + inverseDistanceFromUnsolved; break;
        case WIN: val = COMM_INERT; break;
      }
  }
  else if (signalState == GO) {
    switch(gameMode) {
      case SETUP: val = COMM_SETUP_GO; break;
      case PLAY: val = COMM_PLAY_GO; break;
      case WIN: val = COMM_WIN_GO; break;
    }
  }
  else if (signalState == RESOLVE) {
    switch(gameMode) {
      case SETUP: val = COMM_SETUP_RESOLVE; break;
      case PLAY: val = COMM_PLAY_RESOLVE; break;
      case WIN: val = COMM_WIN_RESOLVE; break;
    } 
  }
  return (syncVal << 5) + val;
}

// === WIN DETECTION using distance propagation ===
void updateWinDistance() {

  // Propagate inverse distance from unsolved Blink
  inverseDistanceFromUnsolved = 0;

  if (!amISolved) {
    // I am unsolved, so distance is MAX
    inverseDistanceFromUnsolved = MAX_SOLVE_DISTANCE;
  }

  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      byte neighborData = getLastValueReceivedOnFace(f);
      
      byte neighborBroadcast = neighborData & 31; // Extract lower 5 bits
      
      // CRITICAL: Only process if neighbor is sending valid distance data
      if (neighborBroadcast >= COMM_PLAY_BASE && neighborBroadcast < COMM_WIN_GO) {
        byte neighborDistance = neighborBroadcast - COMM_PLAY_BASE;
        if (neighborDistance > inverseDistanceFromUnsolved && neighborDistance > 0) {
          inverseDistanceFromUnsolved = neighborDistance - 1;
        }
      }
    }
  } 
}

void syncLoop() {

  bool didNeighborChange = false;

  // look at our neighbors to determine if one of them passed go (changed value)
  // note: absent neighbors changing to not absent don't count
  FOREACH_FACE(f) {
    if (isValueReceivedOnFaceExpired(f)) {
      neighborSyncState[f] = 2; // this is an absent neighbor
    }
    else {
      byte data = getLastValueReceivedOnFace(f);
      if (neighborSyncState[f] != 2) {  // wasn't absent
        if (getSyncVal(data) != neighborSyncState[f]) { // passed go (changed value)
          didNeighborChange = true;
        }
      }

      neighborSyncState[f] = getSyncVal(data);  // update our record of state now that we've check it
    }
  }

  // if our neighbor passed go and we haven't done so within the buffer period, catch up and pass go as well
  // if we are due to pass go, i.e. timer expired, do so
  if ( (didNeighborChange && syncTimer.getRemaining() < PERIOD_DURATION - BUFFER_DURATION)
       || syncTimer.isExpired()
     ) {

    syncTimer.set(PERIOD_DURATION); // aim to pass go in the defined duration
    syncVal = !syncVal; // change our value everytime we pass go
  }
}

byte getSyncVal(byte data) {
  return (data >> 5) & 1;
}

void resetUserInputs() {
  buttonPressed();
  buttonSingleClicked();
  buttonDoubleClicked();
  buttonMultiClicked();
}