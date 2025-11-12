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

bool showHints = false;

#define NUM_GAME_COLORS 4
#define CODE_PULSE_DURATION         1200 // MILLISECONDS FOR BLINK PERIOD
#define CODE_DIGIT_SPACE_DURATION   1200 // MILLISECONDS FOR SPACE BETWEEN DIGITS
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
#define MAX_NEG_NUM 100

#define PERIOD_DURATION 2000
#define BUFFER_DURATION 200

#define PKT_BUFFER_DURATION 200

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

struct Neighbor {
  byte faceColor;           // Color on connected face
  byte signatureColors[FACE_COUNT]; // Full color signature of neighbor
  
  void clear() {
    faceColor = 0;
    FOREACH_FACE(f){
      signatureColors[f] = 0;
    }
  }
  
  bool isEmpty() const {
    return faceColor == 0;
  }
  
  bool hasSignature() const {
    FOREACH_FACE(f) {
      if (signatureColors[f] != 0) return true;
    }
    return false;
  }

  bool matches(const Neighbor& other) const {
    if (faceColor != other.faceColor) return false;
    
    // Check if other.signatureColors is a rotation of signatureColors
    for (int rotation = 0; rotation < FACE_COUNT; rotation++) {
      bool rotationMatches = true;
      for (int i = 0; i < FACE_COUNT; i++) {
        if (signatureColors[i] != other.signatureColors[(i + rotation) % FACE_COUNT]) {
          rotationMatches = false;
          break;
        }
      }
      if (rotationMatches) return true;
    }
    
    return false;
  }
};

struct FaceState {
  Neighbor currentNeighbor;         // Current neighbor data
  Neighbor solutionNeighbor;        // Locked-in solution
  byte negotiationNum;      // Random number for color negotiation
  byte proposedColor;       // Our proposed color
  byte myColor;

  enum NegState {
    NEG_IDLE,
    NEG_SENT,
    NEG_RECEIVED,
    NEG_COMPLETE
  } negotiationState;
  
  enum SigState {
    SIG_IDLE,
    SIG_SENT,
    SIG_RECEIVED
  } mySignatureState;

  void lockSolution() {
    solutionNeighbor.faceColor = currentNeighbor.faceColor;
    FOREACH_FACE(f){
      solutionNeighbor.signatureColors[f] = currentNeighbor.signatureColors[f];
    }
  }
  
  void reset() {
    currentNeighbor.clear();
    solutionNeighbor.clear();
    negotiationNum = 0;
    proposedColor = 0;
    myColor = 0;
    negotiationState = NEG_IDLE;
    mySignatureState = SIG_IDLE;
  }  
};

enum PacketType {
  PKT_NEGOTIATE_COLOR = 0,
  PKT_COLOR_SIGNATURE = 1,
  PKT_SIGNATURE_ACK = 2
};

State signalState = INERT;

Mode gameMode = SETUP;//the default mode when the game begins

byte inverseDistanceFromUnsolved = 0;  // Distance propagation for win detection

byte brightness; // synchronized brightness

bool amISolved = false;

FaceState faces[FACE_COUNT];

byte wasDataReceived[6] = {};

bool readyToSolve;

// =========================================================
// =============    MAIN SETUP & LOOP      =================
// =========================================================

void setup() {
  randomize();
  changeMode(SETUP);  // START IN SETUP
}

void loop() {

  // sync the animations
  syncLoop();
  brightness = sin8_C(map(syncTimer.getRemaining(), 0, PERIOD_DURATION, 0, 255));
  
  // listen for packets
  processIncomingPackages();

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

  // double click to start
  if (buttonDoubleClicked()) {
    changeMode(PLAY);  // change game mode on all Blinks
  }

  bool newNeighborhood = false;

  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
      if (faces[f].currentNeighbor.isEmpty()) {  // a new neighbor!
        if(faces[f].negotiationState == FaceState::NEG_IDLE) {
          faces[f].negotiationNum = random(MAX_NEG_NUM);
          faces[f].proposedColor = 1 + random(NUM_GAME_COLORS - 1);
          sendNegotiationPacket(f, faces[f].proposedColor, faces[f].negotiationNum);
          faces[f].negotiationState = FaceState::NEG_SENT;
        }
      }
      if(faces[f].mySignatureState == FaceState::SIG_IDLE && faces[f].negotiationState == FaceState::NEG_COMPLETE) {
//        sendSignaturePacket(f);
//        faces[f].mySignatureState == FaceState::SIG_SENT;
        newNeighborhood = true;
      }
    }
    else { // no neighor
      if(!faces[f].currentNeighbor.isEmpty()) {
        newNeighborhood = true;
      }
      faces[f].reset();
    }

    // display color on face
    setColorOnFace(dim(gameColors[faces[f].myColor], 127 + brightness/2),f);

//     // let's see what state the signature exchange is in
//     if(faces[f].mySignatureState == FaceState::SIG_IDLE) {
//       setColorOnFace(YELLOW, f);
//     }else if(faces[f].mySignatureState == FaceState::SIG_SENT) {
//       setColorOnFace(BLUE, f);
//     }else if(faces[f].mySignatureState == FaceState::SIG_RECEIVED) {
//       setColorOnFace(GREEN, f);
//     }
//
//    if(faces[f].currentNeighbor.hasSignature()) {
//      setColorOnFace(GREEN, f);
//    }
//
//    // DEBUG: PACKET DATA VIS
//    if(wasDataReceived[f]){
//      setColorOnFace(RED, f);
//      wasDataReceived[f] = false;
//    }

  }

  // share the updated signature
  if(newNeighborhood) {
    // send my signature to all neighbors
    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
        sendSignaturePacket(f);
        faces[f].mySignatureState == FaceState::SIG_SENT;
      }  
    }
  }
}

/*
   Mode 2
*/
void playLoop() {

  // Manual trigger win condition w/ 4 clicks
  // Return to setup w/ 6 clicks
  if(buttonMultiClicked()) {
    if(buttonClickCount() == 4) {
      changeMode(WIN);
    }
    else if(buttonClickCount() == 6) {
      changeMode(SETUP);
    }
  }

  if(isAlone()) {
    readyToSolve = true;
  }

  // display state
  if(!readyToSolve) {
    FOREACH_FACE(f) {
      if(!isValueReceivedOnFaceExpired(f)) {
        setColorOnFace(dim(WHITE,brightness), f);
      }
    }
  }
  else {

    bool newNeighborhood = false;

    FOREACH_FACE(f) {
      if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
        if(faces[f].mySignatureState != FaceState::SIG_RECEIVED) {  // PENDING CHANGE (GUESS!!!! THIS DID IT)
          newNeighborhood = true;
        }
      }
      else { // no neighor
        if(!faces[f].currentNeighbor.isEmpty()) {
          newNeighborhood = true;
        }
        faces[f].currentNeighbor.clear();
        faces[f].mySignatureState = FaceState::SIG_IDLE;
      }
    }

    if(newNeighborhood) {
      // send my signature to all neighbors
      FOREACH_FACE(f) {
        if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
          sendSignaturePacket(f);
          faces[f].mySignatureState == FaceState::SIG_SENT;
        }  
      }
    }

    amISolved = isAllFacesSolved();

    // check collective win condition
    updateWinDistance();
    
    if(readyToSolve && amISolved && inverseDistanceFromUnsolved == 0) {
        changeMode(WIN);
    }

    FOREACH_FACE(f) {
      // display color on face
      if(amISolved && showHints) { //AND HINTING
        if(faces[f].myColor != 0) {
          setColorOnFace(dim(GREEN, brightness), f);
        }
        else {
          setColorOnFace(OFF, f);
        }
      }
      else if(faces[f].currentNeighbor.faceColor == faces[f].myColor){
        setColorOnFace(dim(gameColors[faces[f].myColor], brightness),f);
      }
      else {
        setColorOnFace(gameColors[faces[f].myColor],f);
      }

//      if(faces[f].solutionNeighbor.matches(faces[f].currentNeighbor)) {
//        setColorOnFace(GREEN, f);
//      }
//      else {
//        setColorOnFace(RED, f);
//      }
//
//      // DEBUG: PACKET DATA VIS
//      if(wasDataReceived[f]){
//        setColorOnFace(BLUE, f);
//        wasDataReceived[f] = false;
//      }
    }
  }
}

/*
   Mode 3
*/
void winLoop() {

  if (buttonPressed()) {
    changeMode(WIN);
  }

  // Return to setup with 6 clicks
  if(buttonMultiClicked()) {
    if(buttonClickCount() == 6) {
      changeMode(SETUP);
    }
  }

  if(winAnimationTimer.isExpired()){
    // winAnimationPlayhead++;
    winAnimationPlayhead = 1 + (winAnimationPlayhead % CODE_LENGTH); // ITERATES 1 - CODE_LENGTH
    setWinAnimationTimer();
  }

  displayWinAnimation();
}

bool isAllFacesSolved() {
  FOREACH_FACE(f) {
    if(!faces[f].solutionNeighbor.matches(faces[f].currentNeighbor)){
      return false; 
    }
  }
  return true;
}

void displayWinAnimation() {
   if(winAnimationPlayhead == 0) {
     byte hue = map(winAnimationTimer.getRemaining() % (WIN_CELEBRATE_DURATION / 2), 0, WIN_CELEBRATE_DURATION / 2, 0, 255);
     Color rainbowCol = makeColorHSB(hue,255,255);
     if(winAnimationTimer.getRemaining() > WIN_CELEBRATE_DURATION / 2) {
       setColor(rainbowCol);
     }
     else {
       byte bri = map(winAnimationTimer.getRemaining(), 0, WIN_CELEBRATE_DURATION/2, 0, 255);
       FOREACH_FACE(f){
         setColorOnFace(dim(random(3)==0 ? rainbowCol : OFF, bri), f);
       }
     }
   }
   else {
     if(winAnimationTimer.getRemaining() <= CODE_DIGIT_SPACE_DURATION) {
       setColor(OFF);
     }
     else {
       byte bri = sin8_C(192 + 255*winAnimationTimer.getRemaining()/CODE_PULSE_DURATION);
       setColor(dim(digitColors[winAnimationPlayhead-1], bri)); 
     }
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

void sendNegotiationPacket(byte face, byte colorIndex, byte randomNum) {
  byte packet[3] = {PKT_NEGOTIATE_COLOR, colorIndex, randomNum};
  sendDatagramOnFace(packet, 3, face);
}

void sendSignaturePacket(byte face) {
  byte packet[8] = {
    PKT_COLOR_SIGNATURE,
    faces[face].myColor,
    faces[0].myColor,
    faces[1].myColor,
    faces[2].myColor,
    faces[3].myColor,
    faces[4].myColor,
    faces[5].myColor
  };
  sendDatagramOnFace(packet, 8, face);
}

void sendSignatureAck(byte face) {
  byte packet[1] = {PKT_SIGNATURE_ACK};
  sendDatagramOnFace(packet, 1, face);
}

// ===== PACKAGE PROCESSING =====
void processIncomingPackages() {
  FOREACH_FACE(f) {
    if (isDatagramReadyOnFace(f)) {
      wasDataReceived[f] = true;
      const byte* pkg = getDatagramOnFace(f);
      byte pkgType = pkg[0];
      
      switch(pkgType) {

        case PKT_NEGOTIATE_COLOR:
          {
            // Only process color negotiation when in SETUP mode
            if (gameMode != SETUP) {
              break;  // Ignore negotiation packets when not in SETUP
            }
            
            // Also check that neighbor is in SETUP mode
            byte neighborData = getLastValueReceivedOnFace(f);
            byte neighborMode = getGameMode(neighborData);
            if (neighborMode != SETUP) {
              break;  // Ignore if neighbor isn't in SETUP either
            }
            
            // Process negotiation packet if we're in NEG_INERT or NEG_SENT state
            if(faces[f].negotiationState == FaceState::NEG_IDLE || faces[f].negotiationState == FaceState::NEG_SENT) {
              byte neighborColor = pkg[1];
              byte neighborNumber = pkg[2];
              
              // If we haven't sent our proposal yet, send it now
              if(faces[f].negotiationState == FaceState::NEG_IDLE) {
                faces[f].negotiationNum = random(MAX_NEG_NUM);
                faces[f].proposedColor = 1 + random(NUM_GAME_COLORS - 1);
                sendNegotiationPacket(f, faces[f].proposedColor, faces[f].negotiationNum);
              }
              
              // Decide on final color based on who has higher number
              if(neighborNumber > faces[f].negotiationNum) {
                faces[f].myColor = neighborColor;
              } else if(neighborNumber < faces[f].negotiationNum) {
                faces[f].myColor = faces[f].proposedColor;
              } else {
                // Tie-breaker: use lower color index for consistency
                faces[f].myColor = (faces[f].proposedColor < neighborColor) ? faces[f].proposedColor : neighborColor;
              }
              
              // Store agreed color
              faces[f].currentNeighbor.faceColor = faces[f].myColor;
              faces[f].negotiationState = FaceState::NEG_RECEIVED;
            }
            // If we already received their packet, mark negotiation complete
            else if(faces[f].negotiationState == FaceState::NEG_RECEIVED) {
              faces[f].negotiationState = FaceState::NEG_COMPLETE;
            }
          }
          break;

        case PKT_COLOR_SIGNATURE:
          {
            // store the signature we received
            faces[f].currentNeighbor.faceColor = pkg[1];
            faces[f].currentNeighbor.signatureColors[0] = pkg[2];
            faces[f].currentNeighbor.signatureColors[1] = pkg[3];
            faces[f].currentNeighbor.signatureColors[2] = pkg[4];
            faces[f].currentNeighbor.signatureColors[3] = pkg[5];
            faces[f].currentNeighbor.signatureColors[4] = pkg[6];
            faces[f].currentNeighbor.signatureColors[5] = pkg[7];
            
            // if we haven't sent our signature yet, let's do that
            if(faces[f].mySignatureState == FaceState::SIG_IDLE) {
              faces[f].mySignatureState = FaceState::SIG_SENT;
            }

            // let our neighbor know we received it so they can stop sending
            sendSignatureAck(f);
          }
          break;
        
        case PKT_SIGNATURE_ACK:
          {
            // our neighbor let us know they recieved our signature, no need to send more
            faces[f].mySignatureState = FaceState::SIG_RECEIVED;
          }
          break;
      }
      
      markDatagramReadOnFace(f);
    }
  }
  
  // Send a second packet to confirm negotiation complete
  FOREACH_FACE(f) {
    if(faces[f].negotiationState == FaceState::NEG_RECEIVED) {
      sendNegotiationPacket(f, faces[f].proposedColor, faces[f].negotiationNum);
      faces[f].negotiationState = FaceState::NEG_COMPLETE;
    }

    if(faces[f].mySignatureState == FaceState::SIG_SENT) {
      sendSignaturePacket(f);
    }
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
    FOREACH_FACE(f){
      faces[f].reset();
    }
    amISolved = false;
    readyToSolve = false;
  }
  else if (gameMode == PLAY) {
    // save puzzle solution
    FOREACH_FACE(f) {
      faces[f].lockSolution();
      faces[f].myColor = faces[f].solutionNeighbor.faceColor;
      faces[f].mySignatureState = FaceState::SIG_IDLE;
    }
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
