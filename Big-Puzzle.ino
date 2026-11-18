/*
  Big Puzzle - Curious Levity

  The following code adapts the game Puzzle101, to a larger format

  1. Puzzle101 style puzzle for any number of Blinks (user locks in puzzle)
  2. Solution state celebration (recognize global solution and celebrate)
  3. Communicate secret code through flashing
  4. BONUS: Press any Blink in the win condition, it's toy time.
  
  Designed with Paul Levy & Brandon Bozzi
  Code by: Jonathan Bobrow & Ivan Ivanov
  2025.11.12
*/

// =========================================================
// ===================== SECRET CODE =======================
// =========================================================
#define CODE_LENGTH 4 // NUMBER OF DIGITS IN THE SECRET CODE
byte secretCode[CODE_LENGTH] = {1, 2, 3, 4}; // REPLACE THIS
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

// Exactly what makeColorHSB(hue,255,255) gives, written as constants so the color tables
// below are filled in at compile time instead of by code that runs at startup (saves flash)
#define TANGERINE MAKECOLOR_5BIT_RGB(31,16, 0)  // makeColorHSB( 22,255,255)
#define LEMON     MAKECOLOR_5BIT_RGB(27,31, 0)  // makeColorHSB( 49,255,255)
#define MINT      MAKECOLOR_5BIT_RGB( 0,31,10)  // makeColorHSB( 99,255,255)
#define GRAPE     MAKECOLOR_5BIT_RGB(22, 0,31)  // makeColorHSB(200,255,255)
#define LIME      MAKECOLOR_5BIT_RGB( 2,31, 0)  // makeColorHSB( 82,255,255)
#define BLUEBERRY MAKECOLOR_5BIT_RGB( 0, 7,31)  // makeColorHSB(160,255,255)

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

// DATAGRAMS
// blinklib keeps ONE outgoing datagram per face, and a pending datagram is sent INSTEAD of
// the face value. So: never queue two datagrams on a face in the same loop (the second
// overwrites the first), and don't flood (the face value - mode, GO/RESOLVE, win
// distance, sync - would never get through). One datagram goes out per interval, taking
// turns between faces, only while a face still has something to deliver.
#define DATAGRAM_INTERVAL 100
Timer datagramTimer;

// NEIGHBOR PRESENCE, counted in ticks of PRESENCE_TICK_MS
//
// GONE: a face is only treated as empty after it has heard nothing for a while. blinklib
// expires a face after 200ms of silence, but a busy Blink (six neighbors, a datagram going
// out) can take ~100ms per loop, so a face value can arrive just after 200ms even though
// the neighbor never left. Dropping the edge then would restart its negotiation.
//
// SETTLE: when a face (re)connects, getLastValueReceivedOnFace() still holds the LAST value
// heard on that face - possibly from a different Blink that was there before. A datagram
// can even arrive (and mark the face connected) before the new neighbor's first face value.
// So a face value is only trusted once the face has been connected for a moment.
#define PRESENCE_TICK_MS      50
#define NEIGHBOR_GONE_TICKS   12   // 600ms of silence
#define NEIGHBOR_SETTLE_TICKS  5   // 250ms connected
Timer presenceTimer;
byte silentTicks[FACE_COUNT];      // ticks since we last heard this face (saturates)
byte connectedTicks[FACE_COUNT];   // ticks this face has been connected without a gap (saturates)

bool isNeighborGone(byte f) {
  return silentTicks[f] >= NEIGHBOR_GONE_TICKS;
}

// connected, and connected long enough that its face value is really the neighbor's
bool isNeighborValueFresh(byte f) {
  return !isValueReceivedOnFaceExpired(f) && connectedTicks[f] >= NEIGHBOR_SETTLE_TICKS;
}

void updateNeighborPresence() {
  if (!presenceTimer.isExpired()) return;
  presenceTimer.set(PRESENCE_TICK_MS);

  FOREACH_FACE(f) {
    if (isValueReceivedOnFaceExpired(f)) {
      connectedTicks[f] = 0;
      if (silentTicks[f] < 255) silentTicks[f]++;
    }
    else {
      silentTicks[f] = 0;
      if (connectedTicks[f] < 255) connectedTicks[f]++;
    }
  }
}

// Per-face handshake flags (negotiation uses all three, signatures use GOT and REPLY)
#define CH_GOT        1   // we have the neighbor's data
#define CH_DELIVERED  2   // the neighbor told us it has our data
#define CH_REPLY      4   // the neighbor asked us to confirm we have theirs

// Flags carried in packets
#define PF_HAVE_YOURS    1
#define PF_NEED_CONFIRM  2

// My signature (the colors on my six faces) changes during SETUP as faces are negotiated.
// Each change bumps the version; a neighbor echoes back the version it holds, so we know
// when it has our latest signature.
byte sigVersion = 1;

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
  byte proposedColor;       // Our proposed color (0 = no proposal made yet)
  byte myColor;
  byte negFlags;            // CH_* handshake flags for the color negotiation
  byte sigFlags;            // CH_* handshake flags for the signature exchange
  byte theirSigVersion;     // version of the neighbor's signature we hold (0 = none)
  byte sigAcked;            // version of OUR signature the neighbor says it holds

  void lockSolution() {
    solutionNeighbor.faceColor = currentNeighbor.faceColor;
    FOREACH_FACE(f){
      solutionNeighbor.signatureColors[f] = currentNeighbor.signatureColors[f];
    }
  }

  // forget everything we learned from the neighbor on this face
  void clearNeighbor() {
    currentNeighbor.clear();
    sigFlags = 0;
    theirSigVersion = 0;
    sigAcked = 0;
  }

  void reset() {
    clearNeighbor();
    solutionNeighbor.clear();
    negotiationNum = 0;
    proposedColor = 0;
    myColor = 0;
    negFlags = 0;
  }
};

enum PacketType {
  PKT_NEGOTIATE_COLOR = 0,  // {type, proposedColor, negotiationNum, flags}
  PKT_COLOR_SIGNATURE = 1   // {type | needConfirm<<4, faceColor, sig0<<4|sig1, sig2<<4|sig3, sig4<<4|sig5, myVersion, yourVersionIHave}
};
#define NEG_PKT_LEN 4
#define SIG_PKT_LEN 7   // kept short: a Blink can't do anything else while it sends IR

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
  
  // keep track of which faces still have a neighbor
  updateNeighborPresence();

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

  // send any datagrams still waiting to be delivered (rate limited)
  sendPendingDatagrams();

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

  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) { // a neighbor!
      if (faces[f].proposedColor == 0) {  // a new neighbor! propose a color for this edge
        makeProposal(f);
      }
    }
    else if (isNeighborGone(f)) { // no neighbor (for a while)
      if (faces[f].myColor != 0) {
        signatureChanged();  // this face lost its color, so my signature changed
      }
      faces[f].reset();
    }

    // display color on face
    setColorOnFace(dim(gameColors[faces[f].myColor], 127 + brightness/2),f);

//     // let's see what state the signature exchange is in
//     if(signatureNeedsSend(f)) {
//       setColorOnFace(BLUE, f);        // neighbor doesn't have our latest signature yet
//     }else if(faces[f].sigFlags & CH_GOT) {
//       setColorOnFace(GREEN, f);       // both sides have each other's signature
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

  if(isAlone()) {
    setColor(dim(WHITE, brightness));
  }
}

/*
   Mode 2
*/
void playLoop() {

  // turn hints on or off with doubleclick (only on this Blink)
  if(buttonDoubleClicked()) {
    showHints = !showHints;
  }

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

  // forget neighbors that left; a new neighbor's signature arrives by datagram
  bool allGone = true;
  FOREACH_FACE(f) {
    if (isNeighborGone(f)) {
      faces[f].clearNeighbor();
    }
    else {
      allGone = false;
    }
  }

  // A Blink becomes ready once it has been taken apart from the puzzle. Until then it must
  // not trigger the win (the locked-in puzzle is trivially solved), but it still has to
  // report honestly whether it is solved, or its neighbors could win while it is wrong.
  // "Alone" uses the same rule we use to forget neighbors (not blinklib's quicker isAlone()),
  // otherwise a Blink just lifted away would still remember its old neighbors and look solved.
  if(allGone) {
    readyToSolve = true;
  }

  amISolved = isAllFacesSolved();

  // check collective win condition
  updateWinDistance();

  if(readyToSolve && amISolved && inverseDistanceFromUnsolved == 0) {
    changeMode(WIN);
    return;
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

  if(winAnimationPlayhead == 0) {
    duration = WIN_CELEBRATE_DURATION;
  }
  else {
    duration = secretCode[winAnimationPlayhead-1]*(CODE_PULSE_DURATION)+CODE_DIGIT_SPACE_DURATION;
  }

  winAnimationTimer.set(duration);
}

// =========================================================
// ================  NEIGHBOR DATAGRAMS  ===================
// =========================================================
//
// Two exchanges happen with each neighbor:
//  - SETUP: color negotiation. Both sides propose {color, random number}; the higher
//    number wins (tie: lower color). Both compute the same answer from the two proposals.
//  - SETUP & PLAY: signatures. Each side sends the colors of all six of its faces, so a
//    neighbor can recognize WHICH Blink is touching it.
//
// Packets are not acknowledged with separate ACK packets (blinklib only holds one outgoing
// datagram per face, so an ACK would overwrite the data packet queued after it, or vice
// versa). Instead each packet says whether we have the other side's data and whether we
// still need them to confirm ours. A face resends every DATAGRAM_INTERVAL until both sides
// are confirmed, then goes quiet.

void makeProposal(byte f) {
  faces[f].negotiationNum = random(MAX_NEG_NUM);                // 0..MAX_NEG_NUM (blinklib's random() is inclusive)
  faces[f].proposedColor = 1 + random(NUM_GAME_COLORS - 1);     // 1..NUM_GAME_COLORS
  faces[f].negFlags = 0;
}

void signatureChanged() {
  sigVersion++;
  if (sigVersion == 0) sigVersion = 1;  // 0 means "none"
}

bool negotiationNeedsSend(byte f) {
  return faces[f].proposedColor != 0 &&
         (!(faces[f].negFlags & CH_DELIVERED) || (faces[f].negFlags & CH_REPLY));
}

bool signatureNeedsSend(byte f) {
  return faces[f].sigAcked != sigVersion || (faces[f].sigFlags & CH_REPLY);
}

// One datagram per DATAGRAM_INTERVAL, taking turns between faces. Sending IR blocks the
// Blink face by face (a signature takes ~40ms), so datagrams on all six faces at once would
// stall it longer than the 200ms after which neighbors consider a face empty: they would
// drop and renegotiate the edge, change their signature, and never settle.
// On each face, negotiation goes first: the signature only means something once this edge
// has its color.
byte nextDatagramFace = 0;

void sendPendingDatagrams() {
  if (!datagramTimer.isExpired() || gameMode == WIN) return;

  for (byte i = 0; i < FACE_COUNT; i++) {
    byte f = nextDatagramFace;
    nextDatagramFace = (f == FACE_COUNT - 1) ? 0 : f + 1;

    if (isValueReceivedOnFaceExpired(f)) continue;

    FaceState &fs = faces[f];
    byte packet[SIG_PKT_LEN];
    byte len;

    if (gameMode == SETUP && negotiationNeedsSend(f)) {
      // {type, proposedColor, negotiationNum, flags}
      packet[0] = PKT_NEGOTIATE_COLOR;
      packet[1] = fs.proposedColor;
      packet[2] = fs.negotiationNum;
      packet[3] = ((fs.negFlags & CH_GOT) ? PF_HAVE_YOURS : 0) | ((fs.negFlags & CH_DELIVERED) ? 0 : PF_NEED_CONFIRM);
      len = NEG_PKT_LEN;
      fs.negFlags &= ~CH_REPLY;
    }
    else if ((gameMode != SETUP || fs.myColor != 0) && signatureNeedsSend(f)) {
      // {type | needConfirm<<4, faceColor, sig0<<4|sig1, sig2<<4|sig3, sig4<<4|sig5, myVersion, yourVersionIHave}
      packet[0] = PKT_COLOR_SIGNATURE | ((fs.sigAcked != sigVersion) ? 0x10 : 0);
      packet[1] = fs.myColor;
      for (byte k = 0; k < 3; k++) {
        packet[2 + k] = (faces[2 * k].myColor << 4) | faces[2 * k + 1].myColor;
      }
      packet[5] = sigVersion;
      packet[6] = fs.theirSigVersion;
      len = SIG_PKT_LEN;
      fs.sigFlags &= ~CH_REPLY;
    }
    else {
      continue;   // nothing to say on this face
    }

    sendDatagramOnFace(packet, len, f);
    datagramTimer.set(DATAGRAM_INTERVAL);
    return;
  }
}

// ===== PACKET PROCESSING =====
void processIncomingPackages() {
  FOREACH_FACE(f) {
    if (isDatagramReadyOnFace(f)) {
      wasDataReceived[f] = true;
      const byte* pkg = getDatagramOnFace(f);
      byte len = getDatagramLengthOnFace(f);
      byte pkgType = pkg[0] & 0x0F;
      FaceState &fs = faces[f];

      if (pkgType == PKT_NEGOTIATE_COLOR && len == NEG_PKT_LEN && gameMode == SETUP) {
        // the neighbor may have noticed us before we noticed it
        if (fs.proposedColor == 0) {
          makeProposal(f);
        }

        // Decide on final color: the higher number wins; on a tie, the lower color index.
        // Both sides compute this from the same two proposals, so they agree.
        byte neighborColor = pkg[1];
        byte neighborNumber = pkg[2];
        byte agreed = fs.proposedColor;
        if (neighborNumber > fs.negotiationNum ||
            (neighborNumber == fs.negotiationNum && neighborColor < fs.proposedColor)) {
          agreed = neighborColor;
        }
        if (fs.myColor != agreed) {
          fs.myColor = agreed;
          signatureChanged();
        }

        fs.negFlags |= CH_GOT;
        if (pkg[3] & PF_HAVE_YOURS)   fs.negFlags |= CH_DELIVERED;
        if (pkg[3] & PF_NEED_CONFIRM) fs.negFlags |= CH_REPLY;
      }
      else if (pkgType == PKT_COLOR_SIGNATURE && len == SIG_PKT_LEN && gameMode != WIN) {
        // store the signature we received (two colors per byte)
        fs.currentNeighbor.faceColor = pkg[1];
        for (byte k = 0; k < 3; k++) {
          fs.currentNeighbor.signatureColors[2 * k]     = pkg[2 + k] >> 4;
          fs.currentNeighbor.signatureColors[2 * k + 1] = pkg[2 + k] & 0x0F;
        }
        fs.theirSigVersion = pkg[5];
        fs.sigAcked = pkg[6];
        fs.sigFlags |= CH_GOT;
        if (pkg[0] >> 4) fs.sigFlags |= CH_REPLY;   // they need to hear that we have it
      }

      markDatagramReadOnFace(f);
    }
  }
}

/*
   pass this a game mode to switch to
*/
void changeMode( byte mode ) {
  gameMode = (Mode)mode;  // change my own mode
  signalState = GO; // signal my neighbors

  // handle any items that a game should do once when it changes
  if (gameMode == SETUP) {
    // reset puzzle
    FOREACH_FACE(f){
      faces[f].reset();
    }
    signatureChanged();
    amISolved = false;
    readyToSolve = false;
    inverseDistanceFromUnsolved = 0;
  }
  else if (gameMode == PLAY) {
    // save puzzle solution (myColor keeps the negotiated colors)
    // readyToSolve stays false until this Blink has been taken apart from the puzzle
    readyToSolve = false;
    FOREACH_FACE(f) {
      faces[f].lockSolution();
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
    if (isNeighborValueFresh(f)) {//a neighbor! (whose value is really its own, not left over)
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

// Face value, low 5 bits (unchanged on the wire):
//   0 INERT (SETUP or WIN), 1/2 SETUP GO/RESOLVE, 3/4 PLAY GO/RESOLVE,
//   5..20 PLAY INERT with win distance 0..15, 21/22 WIN GO/RESOLVE
// GO codes are odd, RESOLVE codes even.
const byte COMM_CODE[2][3] = {
  {COMM_SETUP_GO,      COMM_PLAY_GO,      COMM_WIN_GO},       // GO
  {COMM_SETUP_RESOLVE, COMM_PLAY_RESOLVE, COMM_WIN_RESOLVE}   // RESOLVE
};

byte getSignalState(byte data) {
  byte val = data & 31;
  if (val == COMM_INERT || (val >= COMM_PLAY_BASE && val < COMM_WIN_GO) || val > COMM_WIN_RESOLVE) {
    return INERT;
  }
  return (val & 1) ? GO : RESOLVE;
}

byte getGameMode(byte data) {
  byte val = data & 31;
  if (val > COMM_WIN_RESOLVE) return SETUP;
  if (val >= COMM_WIN_GO)     return WIN;
  if (val >= COMM_PLAY_GO)    return PLAY;   // PLAY GO/RESOLVE and the distance values
  return SETUP;
}

byte getBroadcastValue() {
  byte val = COMM_INERT;
  if (signalState != INERT) {
    val = COMM_CODE[signalState - 1][gameMode];
  }
  else if (gameMode == PLAY) {
    val = COMM_PLAY_BASE + inverseDistanceFromUnsolved;
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
      byte neighborDistance = 0;

      if (!isNeighborValueFresh(f)) {
        // just connected: its value may be left over from a previous neighbor.
        // Until we really hear from it, assume it is unsolved.
        neighborDistance = MAX_SOLVE_DISTANCE;
      }
      else {
        byte neighborBroadcast = getLastValueReceivedOnFace(f) & 31; // Extract lower 5 bits

        // Only process if neighbor is sending valid distance data
        if (neighborBroadcast >= COMM_PLAY_BASE && neighborBroadcast < COMM_WIN_GO) {
          neighborDistance = neighborBroadcast - COMM_PLAY_BASE;
        }
      }

      if (neighborDistance > inverseDistanceFromUnsolved && neighborDistance > 0) {
        inverseDistanceFromUnsolved = neighborDistance - 1;
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
