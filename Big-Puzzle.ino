/*
  Big Puzzle - Curious Levity

  Puzzle101 for any number of Blinks.

  1. SETUP: put the Blinks together in any shape. Every pair of touching faces agrees on a
     color. Double-click any Blink to lock in the puzzle.
  2. PLAY: take the puzzle apart and put it back together. A face pulses when it touches the
     color it was locked in with. Double-click a Blink to toggle hints on that Blink (its
     faces turn green when it is back in its place). When every Blink is in place, the whole
     puzzle wins.
  3. WIN: a celebration, then the secret code, flashed in step on every Blink: each digit is
     a run of pulses in that digit's color, then a pause. The code repeats.
  BONUS: press any Blink during the reveal to start it over (on every Blink).

  Clicks   SETUP  double-click: lock in the puzzle
           PLAY   double-click: hints on/off (this Blink only)
                  4 clicks: win now        6 clicks: back to SETUP
           WIN    press: restart the reveal 6 clicks: back to SETUP

  Written for the standard Move38 blinklib.

  Designed with Paul Levy & Brandon Bozzi
  Code by: Jonathan Bobrow & Ivan Ivanov
  2025.11.12
*/

// =========================================================
// ===================== SECRET CODE =======================
// =========================================================
#define CODE_LENGTH 4                         // NUMBER OF DIGITS IN THE SECRET CODE
byte secretCode[CODE_LENGTH] = {1, 2, 3, 4};  // REPLACE THIS (each digit 0-9)
// =========================================================

// ---------------------------------------------------------
// Colors
// ---------------------------------------------------------
// Exactly what makeColorHSB(hue, 255, 255) gives, as the Blink's raw 16-bit color, so the
// tables below are filled in when compiling rather than by code at startup (saves flash).
// Use gameColor() / digitColor() to get a Color.
#define RAW_COLOR(r, g, b) ((word)(1 | ((r) << 1) | ((g) << 6) | ((word)(b) << 11)))
#define TANGERINE RAW_COLOR(31, 16,  0)  // hue  22
#define LEMON     RAW_COLOR(27, 31,  0)  // hue  49
#define MINT      RAW_COLOR( 0, 31, 10)  // hue  99
#define GRAPE     RAW_COLOR(22,  0, 31)  // hue 200
#define LIME      RAW_COLOR( 2, 31,  0)  // hue  82 (spare)
#define BLUEBERRY RAW_COLOR( 0,  7, 31)  // hue 160 (spare)

#define NUM_GAME_COLORS 4
const word gameColors[NUM_GAME_COLORS + 1] = {0, TANGERINE, LEMON, MINT, GRAPE};  // [0] = no color

// Color of each code digit, by position (cycles if the code is longer than 4 digits)
#define NUM_DIGIT_COLORS 4
const word digitColors[NUM_DIGIT_COLORS] = {TANGERINE, LEMON, MINT, GRAPE};

// ---------------------------------------------------------
// Timing
// ---------------------------------------------------------
#define PULSE_PERIOD_MS     2000  // breathing in SETUP and PLAY, kept in step across Blinks
#define PULSE_BUFFER_MS      200  // (Puzzle101's sync: see pulseSyncLoop)

#define CELEBRATE_STEPS        4  // the celebration, in steps (4 x 1000 = 4 s)
#define CELEBRATE_STEP_MS   1000
#define CELEBRATE_MS        (CELEBRATE_STEPS * CELEBRATE_STEP_MS)
#define CODE_PULSE_MS       1200  // one pulse of a code digit
#define CODE_SPACE_MS       1200  // the pause after each digit

// ---------------------------------------------------------
// Game state
// ---------------------------------------------------------
enum Mode   { SETUP, PLAY, WIN };
enum Signal { INERT, GO, RESOLVE };  // a mode change spreads as GO, then settles as RESOLVE

byte gameMode = SETUP;
byte signalState = INERT;

bool readyToSolve = false;   // PLAY: this Blink has been taken apart since the lock-in
bool amISolved = false;      // PLAY: every face touches the neighbor it was locked in with
bool showHints = false;

#define MAX_SOLVE_DISTANCE 15
byte winDistance = 0;        // PLAY: 15 if I'm unsolved, else 1 less than the most a neighbor
                             // reports; 0 when no unsolved Blink is near (see updateWinDistance)

byte brightness;             // breathing level this frame (same on every Blink)

// breathing in step (pulseSyncLoop)
Timer pulseTimer;
byte pulseBeat = 0;
byte neighborBeat[FACE_COUNT];  // 2 = no neighbor

// the reveal (revealSyncLoop)
byte revealStep;   // steps since the reveal started (the same on every Blink)
byte revealDigit;  // 0 = celebrating, else the digit being shown (1..CODE_LENGTH)
byte revealPart;   // celebration step, or pulse number in the digit (== digit: the pause)
Timer revealTimer;

// ---------------------------------------------------------
// Face value: what every face broadcasts all the time (6 bits)
// ---------------------------------------------------------
//   bit 5     pulse beat: flips every PULSE_PERIOD_MS (keeps the breathing in step)
//   bits 0-4  mode and signal:
#define V_SETUP_INERT    0
#define V_SETUP_GO       1
#define V_SETUP_RESOLVE  2
#define V_PLAY_GO        3
#define V_PLAY_RESOLVE   4
#define V_PLAY_DIST      5   //  5..20  PLAY, settled: win distance 0..15
#define V_WIN_GO        21
#define V_WIN_RESOLVE   22
#define V_WIN_STEP      23   // 23..30  WIN, settled: reveal step (mod 8)

const byte SIGNAL_CODE[2][3] = {
  {V_SETUP_GO,      V_PLAY_GO,      V_WIN_GO},       // GO
  {V_SETUP_RESOLVE, V_PLAY_RESOLVE, V_WIN_RESOLVE}   // RESOLVE
};

// ---------------------------------------------------------
// Faces
// ---------------------------------------------------------
// The color of each edge is agreed with the neighbor in SETUP. A neighbor's "signature" is
// the colors of all six of its faces, which tells us WHICH Blink is touching us, whatever
// way it is turned. Locking in the puzzle saves each face's signature as its solution.
struct Face {
  // this edge
  byte myColor;          // agreed color (0 = none yet)
  byte proposedColor;    // our proposal (0 = none made yet)
  byte proposalNum;      // random number: the higher proposal wins
  byte negFlags;         // handshake for the color (CH_*)

  // the neighbor touching this face
  byte nbColor;          // its color on the touching face
  byte nbSig[FACE_COUNT];
  byte theirSigVersion;  // version of its signature we have (0 = none)
  byte sigAcked;         // version of OUR signature it says it has
  byte sigFlags;         // handshake for signatures (CH_*)

  // the solution, saved at lock-in
  byte solColor;
  byte solSig[FACE_COUNT];
};
Face faces[FACE_COUNT];

// (functions start here: all types are declared above, for the Arduino IDE's prototypes)

Color rawToColor(word raw) {
  Color c;
  c.as_uint16 = raw;
  return c;
}
Color gameColor(byte i)  { return rawToColor(gameColors[i]); }
Color digitColor(byte i) { return rawToColor(digitColors[i % NUM_DIGIT_COLORS]); }

byte modeOf(byte value) {
  value &= 31;
  if (value >= V_WIN_GO)  return WIN;
  if (value >= V_PLAY_GO) return PLAY;
  return SETUP;
}

byte signalOf(byte value) {
  value &= 31;
  if (value == V_SETUP_GO || value == V_PLAY_GO || value == V_WIN_GO) return GO;
  if (value == V_SETUP_RESOLVE || value == V_PLAY_RESOLVE || value == V_WIN_RESOLVE) return RESOLVE;
  return INERT;
}

// My signature changes in SETUP as edges get their colors. Each change bumps the version;
// a neighbor echoes back the version it has, so we know when it has the latest.
byte sigVersion = 1;

void signatureChanged() {
  if (++sigVersion == 0) sigVersion = 1;  // 0 means "none"
}

// forget the neighbor touching this face
void clearNeighbor(Face &fc) {
  fc.nbColor = 0;
  FOREACH_FACE(i) fc.nbSig[i] = 0;
  fc.theirSigVersion = 0;
  fc.sigAcked = 0;
  fc.sigFlags = 0;
}

void resetFace(Face &fc) {
  clearNeighbor(fc);
  fc.myColor = 0;
  fc.proposedColor = 0;
  fc.proposalNum = 0;
  fc.negFlags = 0;
  fc.solColor = 0;
  FOREACH_FACE(i) fc.solSig[i] = 0;
}

void lockSolution(Face &fc) {
  fc.solColor = fc.nbColor;
  FOREACH_FACE(i) fc.solSig[i] = fc.nbSig[i];
}

// Is the neighbor on this face the one saved at lock-in (turned any way)?
// An empty face matches an empty solution.
bool faceSolved(Face &fc) {
  if (fc.nbColor != fc.solColor) return false;
  for (byte rot = 0; rot < FACE_COUNT; rot++) {
    byte i = 0;
    while (i < FACE_COUNT && fc.solSig[i] == fc.nbSig[(i + rot) % FACE_COUNT]) i++;
    if (i == FACE_COUNT) return true;
  }
  return false;
}

// ---------------------------------------------------------
// Neighbor presence, counted in ticks of PRESENCE_TICK_MS
// ---------------------------------------------------------
// GONE: blinklib calls a face expired after 200 ms of silence, but a busy Blink (six
// neighbors, a datagram going out) can take ~100 ms a loop, so its value can arrive just
// after that while it never left. A face only counts as empty after a longer silence;
// otherwise edges would keep renegotiating.
//
// FRESH: when a face (re)connects, getLastValueReceivedOnFace() still holds the last value
// heard there, maybe from a different Blink. A face's value is only trusted once it has
// been connected for a moment.
#define PRESENCE_TICK_MS       50
#define NEIGHBOR_GONE_TICKS    12   // 600 ms of silence
#define NEIGHBOR_FRESH_TICKS    5   // 250 ms connected
Timer presenceTimer;
byte silentTicks[FACE_COUNT];
byte connectedTicks[FACE_COUNT];

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

bool isNeighborGone(byte f) {
  return silentTicks[f] >= NEIGHBOR_GONE_TICKS;
}

bool isNeighborValueFresh(byte f) {
  return !isValueReceivedOnFaceExpired(f) && connectedTicks[f] >= NEIGHBOR_FRESH_TICKS;
}

// =========================================================
// ================  MAIN SETUP & LOOP  ====================
// =========================================================

void setup() {
  randomize();
  changeMode(SETUP);
}

void loop() {
  updateNeighborPresence();

  pulseSyncLoop();
  brightness = sin8_C(map(pulseTimer.getRemaining(), 0, PULSE_PERIOD_MS, 0, 255));

  receiveDatagrams();

  // spread mode changes across the puzzle
  switch (signalState) {
    case INERT:   inertLoop();   break;
    case GO:      goLoop();      break;
    case RESOLVE: resolveLoop(); break;
  }

  switch (gameMode) {
    case SETUP: setupLoop(); break;
    case PLAY:  playLoop();  break;
    case WIN:   winLoop();   break;
  }

  sendDatagrams();
  setValueSentOnAllFaces(faceValue());

  // clear any button events this mode didn't use
  buttonPressed();
  buttonSingleClicked();
  buttonDoubleClicked();
  buttonMultiClicked();
}

// =========================================================
// =======================  MODES  =========================
// =========================================================

void changeMode(byte mode) {
  gameMode = mode;
  signalState = GO;  // tell the neighbors

  if (mode == SETUP) {
    FOREACH_FACE(f) resetFace(faces[f]);
    signatureChanged();
    readyToSolve = false;
    amISolved = false;
    winDistance = 0;
  }
  else if (mode == PLAY) {
    // ready only once this Blink has been taken apart from the puzzle
    readyToSolve = false;
    FOREACH_FACE(f) lockSolution(faces[f]);
  }
  else {
    startReveal();
  }
}

// ---------------------------------------------------------
// SETUP: agree on edge colors
// ---------------------------------------------------------
void setupLoop() {
  if (buttonDoubleClicked()) {
    changeMode(PLAY);
    return;
  }

  FOREACH_FACE(f) {
    Face &fc = faces[f];
    if (!isValueReceivedOnFaceExpired(f)) {
      if (fc.proposedColor == 0) makeProposal(fc);  // a new neighbor
    }
    else if (isNeighborGone(f)) {
      if (fc.myColor != 0) signatureChanged();      // this edge lost its color
      resetFace(fc);
    }
    setColorOnFace(dim(gameColor(fc.myColor), 127 + brightness / 2), f);
  }

  if (isAlone()) setColor(dim(WHITE, brightness));
}

// ---------------------------------------------------------
// PLAY: is everyone back in place?
// ---------------------------------------------------------
void playLoop() {
  if (buttonDoubleClicked()) showHints = !showHints;

  if (buttonMultiClicked()) {
    byte clicks = buttonClickCount();
    if (clicks == 4) { changeMode(WIN);   return; }
    if (clicks == 6) { changeMode(SETUP); return; }
  }

  // forget neighbors that left (a new neighbor's signature arrives by datagram)
  bool alone = true;
  FOREACH_FACE(f) {
    if (isNeighborGone(f)) clearNeighbor(faces[f]);
    else alone = false;
  }

  // "Alone" by the same rule we use to forget neighbors (not blinklib's quicker isAlone()),
  // or a Blink just lifted away would still remember its old neighbors and look solved.
  if (alone) readyToSolve = true;

  // A Blink that was never taken apart must not trigger the win (the locked-in puzzle is
  // trivially solved), but it still reports honestly whether it is solved, so the others
  // can't win around it while it is out of place.
  amISolved = true;
  FOREACH_FACE(f) {
    if (!faceSolved(faces[f])) amISolved = false;
  }
  updateWinDistance();

  if (readyToSolve && amISolved && winDistance == 0) {
    changeMode(WIN);
    return;
  }

  FOREACH_FACE(f) {
    Face &fc = faces[f];
    Color c;
    if (!readyToSolve) {
      c = isNeighborGone(f) ? OFF : dim(WHITE, brightness);  // "take me apart"
    }
    else if (amISolved && showHints) {
      c = fc.myColor ? dim(GREEN, brightness) : OFF;
    }
    else if (fc.nbColor == fc.myColor) {
      c = dim(gameColor(fc.myColor), brightness);           // touching its color: pulse
    }
    else {
      c = gameColor(fc.myColor);
    }
    setColorOnFace(c, f);
  }
}

// Win detection without a leader: an unsolved Blink reports 15, and every solved Blink
// reports one less than the most any neighbor reports. A Blink sees 0 around it only when
// no unsolved Blink is within 15 steps, so the puzzle wins once every Blink is in place
// (for puzzles up to 15 Blinks across).
void updateWinDistance() {
  winDistance = amISolved ? 0 : MAX_SOLVE_DISTANCE;

  FOREACH_FACE(f) {
    if (isValueReceivedOnFaceExpired(f)) continue;

    byte d = MAX_SOLVE_DISTANCE;  // just connected: assume unsolved until we really hear it
    if (isNeighborValueFresh(f)) {
      byte v = getLastValueReceivedOnFace(f) & 31;
      d = (v >= V_PLAY_DIST && v <= V_PLAY_DIST + MAX_SOLVE_DISTANCE) ? v - V_PLAY_DIST : 0;
    }
    if (d > winDistance) winDistance = d - 1;
  }
}

// ---------------------------------------------------------
// WIN: celebrate, then reveal the code
// ---------------------------------------------------------
void winLoop() {
  if (buttonPressed()) changeMode(WIN);  // restart the reveal everywhere

  if (buttonMultiClicked() && buttonClickCount() == 6) {
    changeMode(SETUP);
    return;
  }

  revealSyncLoop();
  displayReveal();
}

// =========================================================
// ====================  THE REVEAL  =======================
// =========================================================
//
// The reveal is a sequence of short steps: CELEBRATE_STEPS steps of celebration, then for
// each digit one step per pulse and one for the pause, then the digits again, forever.
//
// To keep every Blink in step (each Blink's clock runs a little fast or slow), every Blink
// broadcasts its step number (mod 8). A Blink that sees a neighbor 1 to 3 steps ahead jumps
// ahead to match. This is Puzzle101's "pass go" sync, applied at every step: the whole
// puzzle follows whichever Blink runs fastest, and no Blink can drift by more than a step.

void startReveal() {
  revealStep = 0;
  revealDigit = 0;
  revealPart = 0;
  revealTimer.set(CELEBRATE_STEP_MS);
}

void revealAdvance() {
  revealStep++;
  revealPart++;

  byte parts = revealDigit ? secretCode[revealDigit - 1] + 1 : CELEBRATE_STEPS;
  if (revealPart >= parts) {
    revealPart = 0;
    revealDigit = revealDigit % CODE_LENGTH + 1;  // after the celebration, digits 1..N, repeating
  }

  word ms = CELEBRATE_STEP_MS;
  if (revealDigit) ms = (revealPart < secretCode[revealDigit - 1]) ? CODE_PULSE_MS : CODE_SPACE_MS;
  revealTimer.set(ms);
}

void revealSyncLoop() {
  if (revealTimer.isExpired()) revealAdvance();

  // While the restart (or the win itself) is still spreading, neighbors may show old steps
  if (signalState != INERT) return;

  FOREACH_FACE(f) {
    if (!isNeighborValueFresh(f)) continue;
    byte v = getLastValueReceivedOnFace(f) & 31;
    if (v < V_WIN_STEP || v > V_WIN_STEP + 7) continue;

    byte ahead = (v - V_WIN_STEP - revealStep) & 7;
    if (ahead <= 3) {
      while (ahead--) revealAdvance();
    }
  }
}

// The color wheel at full saturation and brightness: 6 sectors of 32 steps (step 0..191),
// in the Blink's own 5-bit color. Looks the same as makeColorHSB(hue, 255, 255) and takes
// far less flash.
#define RAINBOW_STEPS 192
Color rainbow(byte step) {
  byte up = step & 31;
  byte down = 31 - up;
  byte r = 0, g = 0, b = 0;
  switch (step >> 5) {
    case 0:  r = 31;   g = up;   break;  // red -> yellow
    case 1:  r = down; g = 31;   break;  // yellow -> green
    case 2:  g = 31;   b = up;   break;  // green -> cyan
    case 3:  g = down; b = 31;   break;  // cyan -> blue
    case 4:  r = up;   b = 31;   break;  // blue -> magenta
    default: r = 31;   b = down; break;  // magenta -> red
  }
  return MAKECOLOR_5BIT_RGB(r, g, b);
}

void displayReveal() {
  word remaining = revealTimer.getRemaining();

  if (revealDigit == 0) {
    // celebration: a rainbow for the first half, then fading sparkles
    word left = (CELEBRATE_STEPS - 1 - revealPart) * CELEBRATE_STEP_MS + remaining;
    Color c = rainbow(map(left % (CELEBRATE_MS / 2), 0, CELEBRATE_MS / 2, 0, RAINBOW_STEPS));
    if (left > CELEBRATE_MS / 2) {
      setColor(c);
    }
    else {
      byte bri = map(left, 0, CELEBRATE_MS / 2, 0, 255);
      FOREACH_FACE(f) setColorOnFace(random(3) == 0 ? dim(c, bri) : OFF, f);
    }
  }
  else if (revealPart < secretCode[revealDigit - 1]) {
    // one pulse: dark, up to full, back to dark
    byte phase = 192 + (byte)((255UL * remaining) / CODE_PULSE_MS);
    setColor(dim(digitColor(revealDigit - 1), sin8_C(phase)));
  }
  else {
    setColor(OFF);  // the pause between digits
  }
}

// =========================================================
// ==============  MODE CHANGES (GO/RESOLVE)  ==============
// =========================================================

// Settled: follow a neighbor that is announcing a new mode
void inertLoop() {
  FOREACH_FACE(f) {
    if (!isNeighborValueFresh(f)) continue;  // (an old value could be a previous neighbor's)
    byte v = getLastValueReceivedOnFace(f);
    if (signalOf(v) == GO) changeMode(modeOf(v));
  }
}

// Announcing: once no neighbor is still settled in the old way, start resolving
void goLoop() {
  signalState = RESOLVE;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f) && signalOf(getLastValueReceivedOnFace(f)) == INERT) {
      signalState = GO;
    }
  }
}

// Resolving: once no neighbor is still announcing, settle
void resolveLoop() {
  signalState = INERT;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f) && signalOf(getLastValueReceivedOnFace(f)) == GO) {
      signalState = RESOLVE;
    }
  }
}

byte faceValue() {
  byte v = V_SETUP_INERT;
  if (signalState != INERT)  v = SIGNAL_CODE[signalState - 1][gameMode];
  else if (gameMode == PLAY) v = V_PLAY_DIST + winDistance;
  else if (gameMode == WIN)  v = V_WIN_STEP + (revealStep & 7);
  return (pulseBeat << 5) | v;
}

// =========================================================
// =================  BREATHING IN STEP  ===================
// =========================================================
// Puzzle101's sync: the beat bit flips every PULSE_PERIOD_MS. When a neighbor's beat flips
// ("passes go") and we haven't just passed go ourselves, pass go now. Everyone ends up
// following the fastest Blink.

void pulseSyncLoop() {
  bool neighborPassedGo = false;

  FOREACH_FACE(f) {
    if (isValueReceivedOnFaceExpired(f)) {
      neighborBeat[f] = 2;
    }
    else {
      byte beat = (getLastValueReceivedOnFace(f) >> 5) & 1;
      if (neighborBeat[f] != 2 && beat != neighborBeat[f]) neighborPassedGo = true;
      neighborBeat[f] = beat;
    }
  }

  if ((neighborPassedGo && pulseTimer.getRemaining() < PULSE_PERIOD_MS - PULSE_BUFFER_MS)
      || pulseTimer.isExpired()) {
    pulseTimer.set(PULSE_PERIOD_MS);
    pulseBeat = !pulseBeat;
  }
}

// =========================================================
// ==================  NEIGHBOR DATAGRAMS  =================
// =========================================================
//
// Two exchanges with each neighbor:
//  - SETUP: the edge color. Both sides propose {color, random number}; the higher number
//    wins (tie: the lower color). Both compute the same answer from the two proposals.
//  - SETUP and PLAY: signatures (the colors of all six of a Blink's faces).
//
// blinklib holds ONE outgoing datagram per face and sends it INSTEAD of the face value. So
// there are no separate ACK packets (they would overwrite data queued on the same face):
// each packet says whether we have the other side's data and whether we still need ours
// confirmed. Only one datagram goes out per DATAGRAM_INTERVAL_MS, taking turns between
// faces, and only while something is undelivered: sending IR stalls a Blink, and too much
// of it starves the face values and makes neighbors think we left.

#define DATAGRAM_INTERVAL_MS 100
#define MAX_NEG_NUM          100

enum PacketType {
  PKT_COLOR     = 0,  // {type, proposedColor, proposalNum, flags}
  PKT_SIGNATURE = 1   // {type | needConfirm << 4, edge color, sig0<<4|sig1, sig2<<4|sig3,
                      //  sig4<<4|sig5, my version, version of yours I have}
};
#define COLOR_PKT_LEN 4
#define SIG_PKT_LEN   7

// handshake flags, per face
#define CH_GOT        1   // we have the neighbor's data
#define CH_DELIVERED  2   // the neighbor says it has ours
#define CH_REPLY      4   // the neighbor asked us to confirm we have theirs
// flags in color packets
#define PF_HAVE_YOURS    1
#define PF_NEED_CONFIRM  2

Timer datagramTimer;
byte nextDatagramFace = 0;

void makeProposal(Face &fc) {
  fc.proposalNum = random(MAX_NEG_NUM);                 // 0..MAX_NEG_NUM (random() is inclusive)
  fc.proposedColor = 1 + random(NUM_GAME_COLORS - 1);   // 1..NUM_GAME_COLORS
  fc.negFlags = 0;
}

bool colorNeedsSend(Face &fc) {
  return fc.proposedColor != 0 && (!(fc.negFlags & CH_DELIVERED) || (fc.negFlags & CH_REPLY));
}

bool signatureNeedsSend(Face &fc) {
  return fc.sigAcked != sigVersion || (fc.sigFlags & CH_REPLY);
}

void sendDatagrams() {
  if (!datagramTimer.isExpired() || gameMode == WIN) return;

  for (byte i = 0; i < FACE_COUNT; i++) {
    byte f = nextDatagramFace;
    nextDatagramFace = (f == FACE_COUNT - 1) ? 0 : f + 1;
    if (isValueReceivedOnFaceExpired(f)) continue;

    Face &fc = faces[f];
    byte packet[SIG_PKT_LEN];
    byte len;

    if (gameMode == SETUP && colorNeedsSend(fc)) {
      packet[0] = PKT_COLOR;
      packet[1] = fc.proposedColor;
      packet[2] = fc.proposalNum;
      packet[3] = ((fc.negFlags & CH_GOT) ? PF_HAVE_YOURS : 0) |
                  ((fc.negFlags & CH_DELIVERED) ? 0 : PF_NEED_CONFIRM);
      len = COLOR_PKT_LEN;
      fc.negFlags &= ~CH_REPLY;
    }
    else if ((gameMode == PLAY || fc.myColor != 0) && signatureNeedsSend(fc)) {
      // (in SETUP, only once this edge has its color)
      packet[0] = PKT_SIGNATURE | ((fc.sigAcked != sigVersion) ? 0x10 : 0);
      packet[1] = fc.myColor;
      for (byte k = 0; k < 3; k++) {
        packet[2 + k] = (faces[2 * k].myColor << 4) | faces[2 * k + 1].myColor;
      }
      packet[5] = sigVersion;
      packet[6] = fc.theirSigVersion;
      len = SIG_PKT_LEN;
      fc.sigFlags &= ~CH_REPLY;
    }
    else {
      continue;  // nothing to say on this face
    }

    sendDatagramOnFace(packet, len, f);
    datagramTimer.set(DATAGRAM_INTERVAL_MS);
    return;
  }
}

void receiveDatagrams() {
  FOREACH_FACE(f) {
    if (!isDatagramReadyOnFace(f)) continue;

    const byte *pkt = getDatagramOnFace(f);
    byte len = getDatagramLengthOnFace(f);
    byte type = pkt[0] & 0x0F;
    Face &fc = faces[f];

    if (type == PKT_COLOR && len == COLOR_PKT_LEN && gameMode == SETUP) {
      if (fc.proposedColor == 0) makeProposal(fc);  // it noticed us before we noticed it

      // the higher number wins; on a tie, the lower color
      byte theirColor = pkt[1];
      byte theirNum = pkt[2];
      byte agreed = fc.proposedColor;
      if (theirNum > fc.proposalNum || (theirNum == fc.proposalNum && theirColor < agreed)) {
        agreed = theirColor;
      }
      if (fc.myColor != agreed) {
        fc.myColor = agreed;
        signatureChanged();
      }

      fc.negFlags |= CH_GOT;
      if (pkt[3] & PF_HAVE_YOURS)   fc.negFlags |= CH_DELIVERED;
      if (pkt[3] & PF_NEED_CONFIRM) fc.negFlags |= CH_REPLY;
    }
    else if (type == PKT_SIGNATURE && len == SIG_PKT_LEN && gameMode != WIN) {
      fc.nbColor = pkt[1];
      for (byte k = 0; k < 3; k++) {
        fc.nbSig[2 * k]     = pkt[2 + k] >> 4;
        fc.nbSig[2 * k + 1] = pkt[2 + k] & 0x0F;
      }
      fc.theirSigVersion = pkt[5];
      fc.sigAcked = pkt[6];
      fc.sigFlags |= CH_GOT;
      if (pkt[0] >> 4) fc.sigFlags |= CH_REPLY;  // it needs to hear that we have it
    }

    markDatagramReadOnFace(f);
  }
}
