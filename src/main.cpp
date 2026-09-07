// TotalsyncAATC - Teensy 4.1 data acquisition and behavioural control
//
// Layout of this file:
//   1. Includes
//   2. Pin map and compile-time configuration
//   3. Packet / instruction types
//   4. Global state
//   5. Forward declarations
//   6. Arduino entry points (setup / loop)
//   7. Function definitions

// ---------------------------------------------------------------------------
// 1. Includes
// ---------------------------------------------------------------------------

#include <Encoder.h>
#include <FastCRC.h>
#include <PacketSerial.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "PulsePin.h"

// ---------------------------------------------------------------------------
// 2. Pin map and compile-time configuration
// ---------------------------------------------------------------------------

// Serial port used for human readable diagnostics
#define EXTSERIAL Serial1

// Bench debugging of the SLM path: simulates the scanner frame clock and
// mirrors the SLM state on scope pins. This one is still compile-time because
// it changes what pins 32 and 35 carry, so it must be defined before
// pins_slm.h. Whether the SLM experiment runs at all is decided at runtime by
// the slm_experiment flag below.
#define SLM_DEBUG 1

// Named pins. Included here rather than with the other headers because it
// reads SLM_DEBUG.
#include "pins_slm.h"

// Analog and digital channels scanned every gather tick
const int pinsAnalogIn[] = {16, 17, 18, 19, 20, 21, 22};
const int nAnalogIn = sizeof(pinsAnalogIn) / sizeof(pinsAnalogIn[0]);

const int pinsDigitalIn[] = {0, 1, 2,  3,  4,  5,  6,  7,
                             8, 9, 10, 11, 12, 13, 14, 15};
const int nDigitalIn = sizeof(pinsDigitalIn) / sizeof(pinsDigitalIn[0]);

const int pinsDigitalOut[] = {24, 25, 26, 27, 28, 29, 30, 31,
                              32, 33, 34, 35, 36, 37, 38, 39};
const int nDigitalOut = sizeof(pinsDigitalOut) / sizeof(pinsDigitalOut[0]);

// All addressable digital pins (inputs followed by outputs), used by instUNITY
const int pinsDigital[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                           11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29,
                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
const int nDigital = sizeof(pinsDigital) / sizeof(pinsDigital[0]);

// Number of state variables shipped with every data packet
const int nStates = 8;

// On-board LED
const int ledPin = LED_BUILTIN;

// Pulse pins for the pupil camera
const int pinsPulsePins[] = {30, 31};
const int nPulsePins = sizeof(pinsPulsePins) / sizeof(pinsPulsePins[0]);

// Instruction strides, in bytes (target byte + payload)
const uint8_t strideInstLOW = 2;
const uint8_t strideInstHIGH = 2;
const uint8_t strideInstTOGGLE = 2;
const uint8_t strideInstUNITY = 2;
const uint8_t strideInstPULSE = 5;
const uint8_t strideInstSTATE = 5;

// Synchronization pattern for the pupil camera
const uint16_t syncCounterMax = 0x95FF;
const uint16_t syncCounterMin = 0x9500;

// ---------------------------------------------------------------------------
// 3. Packet / instruction types
// ---------------------------------------------------------------------------

enum packetType : uint8_t {
  ptSTATUS,
  ptINSTR,
  ptERROR,
  ptOK,
  ptACK
};

enum instructionType : uint8_t {
  instPIN_LOW = 0,
  instPIN_HIGH = 1,
  instPIN_TOGGLE = 2,
  instPIN_PULSE = 3,
  instSET_STATE = 4,
  instUNITY = 5,
  instRESET = 6,
  instHANDSHAKE = 149
};

union bytesToLong {
  byte bytes[4];
  unsigned long ulong;
  long slong;
};

// Running packet count, incremented by every packet constructor
static volatile unsigned long packetCount = 0;

struct dataPacket {
  uint8_t type;               // 1 B, packet type
  uint8_t length;             // 1 B, packet size
  uint16_t crc16;             // 2 B, CRC16
  unsigned long packetID;     // 4 B, running packet count

  unsigned long us_start;     // 4 B, gather start timestamp
  unsigned long us_end;       // 4 B, transmit timestamp
  uint16_t analog[nAnalogIn]; // 16 B, ADC values
  long variables[nStates];    // 32 B, variables (encoder, speed, etc)
  uint16_t digitalIn;         // 2 B, digital inputs
  uint16_t digitalOut;        // 2 B, digital outputs
  uint8_t padding[1];         // 1 B, align to 4B

  dataPacket()
      : type(ptSTATUS),
        length(sizeof(dataPacket)),
        crc16(0),
        packetID(packetCount++),
        digitalIn(0),
        digitalOut(0) {}
};

// Worst-case size of one data packet on the wire: the COBS-encoded payload
// (size + size/254 + 1, see COBS::getEncodedBufferSize) plus the delimiter
// byte, which PacketSerial::send() writes as a second, separate call. 72 B of
// payload -> 74 B on the wire.
const int packetWireSize =
    static_cast<int>(sizeof(dataPacket) + sizeof(dataPacket) / 254 + 2);

struct errorPacket {
  uint8_t type;            // 1 B, packet type
  uint8_t length;          // 1 B, packet size
  uint16_t crc16;          // 2 B, CRC16
  unsigned long packetID;  // 4 B, running packet count
  unsigned long us_start;  // 4 B, gather start timestamp

  char message[16];        // 16 B, error message

  errorPacket()
      : type(ptERROR),
        length(sizeof(errorPacket)),
        crc16(0),
        packetID(packetCount++) {}
};

// ---------------------------------------------------------------------------
// 4. Global state
// ---------------------------------------------------------------------------

// Globals below are static (internal linkage) unless something outside this
// translation unit needs them. Entries commented out are currently unused; they
// are kept rather than deleted so the original variable set stays visible.

// Data transport
static FastCRC16 CRC16;
static PacketSerial packetSerialA;
static PacketSerial packetSerialB;

// Timing. current_millis / current_micros are declared extern in PulsePin.h and
// read from PulsePin.cpp, so they must keep external linkage - do not make them
// static.
static IntervalTimer gatherTimer;
elapsedMicros current_micros;
elapsedMillis current_millis;

// Wheel encoder
static Encoder wheelEncoder(WHEEL_ENC_PINA, WHEEL_ENC_PINB);

// Timed output pins
static PulsePin* pulsePins[nPulsePins];

// Moving average for lick detection, superseded by the IR lick detector that
// gather() thresholds directly
// const int windowSize = 10;    // size of moving average window
// int lickReadings[windowSize]; // the readings from the lick input
// int windowIndex = 0;          // the index of the current reading
// int totalLickReadings = 0;    // the running total
// int averageLickReadings = 0;  // the average
static int lickThresh = 2600;
static bool binaryLick = false;

// Intermediate values
// long encoderPosition = 0;
static int last_packet_took = 0;
static int reward = 0;
static int lick = 0;
// int lickBaselineCorr = 0;
// int lickBaseline = 0;
// int ipacket = 0;
// int frame = 0;
static int camera_trigger = 0;
static int ephys = 1;
// int rad = 0;
static int exper = 0;
static int preshock = 0;
// int epacket = 0;
// int t = 0;
static int iPacket = 0;
static int AATC_trigger = 0;
static int press_test = 0;
static int n_sound1 = 0;
static int n_sound2 = 0;
static int Tone = 0;
static unsigned long triggertime = 1000;
static unsigned long tonelength = 0;
static unsigned long rewardtime = 0;

// Runtime switch for the SLM experiment, replacing the former SLM_EXPERIMENT
// compile-time flag so it can be toggled between runs without a reflash. Read
// from gather() (timer ISR) and meant to be written from loop() context, hence
// volatile.
static volatile bool slm_experiment = true;

#ifdef SLM_DEBUG
// Simulated scanner frame clock, so the SLM path can be exercised on the bench
// with nothing attached to SCANNER_FRAME_CLOCK. debug_frame_clock replaces the
// pin reading in gather() and is mirrored on DEBUG_FRAME_CLOCK_OUT for scoping.
//
// The low phase must stay above one gather() tick, or the falling edge can fall
// between two samples and be missed - the same Nyquist limit the real clock is
// subject to.
static bool debug_frame_clock = false;
static byte debug_frame_clock_high_millis = 26;
static byte debug_frame_clock_low_millis = 4;
static byte debug_frame_clock_tick = 0;  // ticks elapsed in the current phase
#endif
// SLM stimulation
static bool slm_stim_armed = false;
const int slm_stim_duration = 10;
const int slm_stim_waittime = 10;
static bool slm_stim_active = false;           // trigger currently held high
static unsigned long slm_stim_end_millis = 0;  // when to release the trigger
static int slm_frame_clock_prev = HIGH;        // frame clock level on the last tick
static byte slm_stim_selected = 0;

// One-wire transmission of slm_stim_selected on SLM_STIM_SELECT. The line idles
// LOW; a HIGH reset pulse of slmSelectResetTicks ms is followed by the 8 data
// bits, MSB first, one bit per ms, then a settling pause of slm_stim_waittime ms
// before the trigger may fire. All three durations are counted in gather()
// ticks, which is why gatherTimer must stay at a 1000 us interval.
enum slmSelectPhase : uint8_t {
  slmSelIdle,   // line LOW, waiting for slm_stim_armed
  slmSelReset,  // holding the reset pulse HIGH
  slmSelData,   // shifting out the 8 data bits
  slmSelWait,   // byte sent, holding the post-transmission pause
  slmSelDone    // pause elapsed, waiting for the frame clock edge to fire
};
const uint8_t slmSelectResetTicks = 10;  // reset pulse length, ms
const uint8_t slmSelectDataBits = 8;
static slmSelectPhase slm_select_phase = slmSelIdle;
static uint8_t slm_select_tick = 0;  // ticks elapsed in the current phase
static byte slm_select_byte = 0;     // slm_stim_selected, latched at tx start

// slm_select_tick counts the pause, so a wait longer than the counter can hold
// would wrap and never reach the comparison.
static_assert(slm_stim_waittime >= 0 && slm_stim_waittime <= 255,
              "slm_stim_waittime must fit in slm_select_tick (uint8_t)");

static volatile unsigned char counter = 0;
static volatile long bufferedStates[nStates];

// volatile bool gatherNow = false;
static volatile bool packetReady = false;

// Pupil camera synchronization counter
static volatile uint16_t syncCounter = syncCounterMax;
static volatile uint16_t syncCounterFrameInterval = 100; // count N frames as 'clock'
static volatile byte syncCounterIdx = 0;
static volatile byte syncCounterSubIdx = 0;
static volatile bool updateSyncCounter = true;

// Current state, overwritten on every gather
static dataPacket State;

// ---------------------------------------------------------------------------
// 5. Forward declarations
// ---------------------------------------------------------------------------

// Acquisition
static void gather();
static void updateSlmStim();
static void applyState(dataPacket* packet);
static void reset();

// Serial communication
static void onPacketReceived(const uint8_t* buffer, size_t size);
static void processInstruction(const uint8_t* buf, size_t buf_sz);
static void dumpBuffer(const uint8_t* buffer, size_t size);
static void debugPrint(const char* msg);

// Pulse pins and camera / ephys synchronization
static PulsePin* getPulsePinById(byte id);
static void syncBlink();

static void ephysrand();

// Experiment state machines
static void runExperiment();
static void runPreShock();
static void AATC();

// ---------------------------------------------------------------------------
// 6. Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  // pinMode(ledPin, OUTPUT);
  // pinMode(LED_1, OUTPUT); // your first LED
  // pinMode(LED_2, OUTPUT);

  // Turn them on
  // digitalWriteFast(LED_1, HIGH);
  // digitalWriteFast(LED_2, HIGH);

  // Analog input channels
  analogReadResolution(16); // change the resolution to 16 bits and read A0

  for (int i = 0; i < nAnalogIn; i++) {
    pinMode(pinsAnalogIn[i], INPUT);
  }
  // NOTE: no-op with the current pin table (nAnalogIn == 8)
  for (int i = 8; i < nAnalogIn; i++) {
    pinMode(pinsAnalogIn[i], OUTPUT);
  }

  // Digital input channels (the last two are left untouched on purpose)
  for (int i = 0; i < nDigitalIn - 2; i++) {
    pinMode(pinsDigitalIn[i], INPUT);
  }

  // Digital output channels
  for (int i = 0; i < nDigitalOut; i++) {
    pinMode(pinsDigitalOut[i], OUTPUT);
  }
  pinMode(VALVE, OUTPUT);
  pinMode(GATHER_INDICATOR, OUTPUT);
  pinMode(LOOP_INDICATOR, OUTPUT);

  // Timed output pins
  for (int i = 0; i < nPulsePins; i++) {
    pulsePins[i] = new PulsePin(i, pinsPulsePins[i], HIGH);
  }

  reset();

  // Start packet transport
  packetSerialA.begin(57600);
  packetSerialA.setPacketHandler(&onPacketReceived);
  packetSerialA.setStream(&SerialUSB1);
  packetSerialB.begin(57600);
  packetSerialB.setPacketHandler(&onPacketReceived);
  packetSerialB.setStream(&SerialUSB2);

  // Start data acquisition ticks, [us] interval.
  // Lowering priority is required to give the Encoder priority
  // and seems to massively reduce/prevent missed counts.
  gatherTimer.priority(200);
  gatherTimer.begin(gather, 1000);

  // Synchronization with the pupil camera
  attachInterrupt(digitalPinToInterrupt(PIN_CAMERA_FSTROBE), syncBlink, CHANGE);
}

void loop() {
  digitalWriteFast(LOOP_INDICATOR, HIGH);

  // Check serial status for data and buffer health
  packetSerialA.update();
  packetSerialB.update();

  if (updateSyncCounter) {
    updateSyncCounter = false;
    if (++syncCounterSubIdx > syncCounterFrameInterval) {
      syncCounterSubIdx = 0;
      if (++syncCounterIdx > 15) {
        syncCounterIdx = 0;
        if (++syncCounter > syncCounterMax) {
          syncCounter = syncCounterMin;
        }
      }
    }
  }

  if (packetReady) {
    State.crc16 = CRC16.kermit(reinterpret_cast<uint8_t *>(&State), sizeof(State));

    // Only write to a port that provably has room for a whole packet.
    // usb_serialN_write() spins for up to TX_TIMEOUT_MSEC (120 ms) when the host
    // holds the port open but stops draining it, which would stall the other
    // port and the rest of loop() along with it. Packets are dropped rather
    // than queued: gather() overwrites State every millisecond regardless.
    //
    // availableForWrite() maps to usb_serialN_write_buffer_free(), which counts
    // idle TX transfer buffers. Transfers complete in order, so the buffer the
    // writer would block on is the first to free up: the count is zero exactly
    // when a write would spin. That makes this test sufficient on its own.
    //
    // Deliberately NOT gated on `if (SerialUSBn)`. That operator bool() is
    // usb_configuration && DTR asserted on this interface && 15 ms settled - and
    // DTR is set only when the host sends CDC_SET_CONTROL_LINE_STATE (usb.c:614).
    // Hosts that open the port without asserting DTR (notably .NET
    // System.IO.Ports.SerialPort, whose DtrEnable defaults to false) read and
    // write normally but leave that flag clear forever, so the guard would drop
    // every packet on a perfectly healthy port. Unplugged is handled anyway:
    // usb_serialN_write() returns immediately when !usb_configuration.
    if (SerialUSB1.availableForWrite() >= packetWireSize) {
      packetSerialA.send(reinterpret_cast<byte *>(&State), sizeof(State));
    }
    if (SerialUSB2.availableForWrite() >= packetWireSize) {
      packetSerialB.send(reinterpret_cast<byte *>(&State), sizeof(State));
    }

    // Apply current state vector
    applyState(&State);

    packetReady = false;
  }

  if (packetSerialA.overflow()) {
    debugPrint("S_A overflow!");
  }

  if (packetSerialB.overflow()) {
    debugPrint("S_B overflow!");
  }

  // Check if timed pins need updates
  for (int i = 0; i < nPulsePins; ++i) {
    pulsePins[i]->updateMicro();
  }

  digitalWriteFast(LOOP_INDICATOR, LOW);
}

// ---------------------------------------------------------------------------
// 7. Function definitions
// ---------------------------------------------------------------------------

// Acquisition tick, driven by gatherTimer
static void gather() {
  digitalWriteFast(GATHER_INDICATOR, HIGH); // toggle pin to indicate gather start
  dataPacket packet;

  // Runs before the experiment state machines that arm it, see updateSlmStim()
  updateSlmStim();

  if (ephys == 1) {
    ephysrand();
  } else {
    digitalWriteFast(EPHYS_SYNC, LOW);
  }

  if (exper == 1) {
    runExperiment();
  }

  if (preshock == 1) {
    runPreShock();
  }

  if (AATC_trigger == 1) {
    AATC();
  } else {
    press_test = 0;
    triggertime = 1000;
  }

  packet.us_start = current_micros;

  for (int i = 0; i < nAnalogIn; i++) {
    packet.analog[i] = analogRead(pinsAnalogIn[i]);
  }

  for (int i = 0; i < nDigitalOut; i++) {
    packet.digitalOut |= digitalReadFast(pinsDigitalOut[i]) << i;
  }

  for (int i = 0; i < nDigitalIn; i++) {
    packet.digitalIn |= digitalReadFast(pinsDigitalIn[i]) << i;
  }

  camera_trigger = digitalReadFast(TRIGGER_C);
  reward = digitalReadFast(REWARD);
  // ephys = digitalReadFast(ephys_trigger);
  exper = digitalReadFast(EXPER);
#ifdef SLM_DEBUG
  preshock = false;
#else
  preshock = digitalReadFast(PRESHOCK);
#endif
#ifdef SLM_DEBUG
  AATC_trigger = true;
#else
  AATC_trigger = digitalReadFast(TRIGGER_AATC);
#endif
  // With the IR lick detector we don't need a moving average anymore
  lick = analogRead(LICK);

  if (lick >= lickThresh) {
    digitalWriteFast(LICKDETECT, HIGH); // redundant?
    binaryLick = true;
  } else {
    digitalWriteFast(LICKDETECT, LOW);
    binaryLick = false;
  }

  digitalWriteFast(VALVE, reward);

  noInterrupts();
  const long new_pos = wheelEncoder.read(); // negative or positive?
  const long position_feel = (long)(new_pos * 0.077); // 0.0159
  interrupts();

  for (int p = 0; p < nStates; p++) {
    packet.variables[p] = 0L;
  }

  packet.variables[0] = new_pos;
  packet.variables[1] = position_feel;
  packet.variables[2] = binaryLick;
  packet.variables[3] = lick;
  packet.variables[4] = 0;
  packet.variables[5] = 0;
  packet.variables[6] = 0;
  packet.variables[7] = last_packet_took;
  packet.us_end = current_micros;

  last_packet_took = current_micros - packet.us_start;
  State = packet;
  packetReady = true;
  digitalWriteFast(GATHER_INDICATOR, LOW); // toggle pin to indicate gather end
}

// One tick of the SLM stimulation state machine, called from gather() every
// millisecond and gated on the slm_experiment flag.
//
// Once armed, the selected stimulus index is clocked out on SLM_STIM_SELECT
// (reset pulse + 8 bits, MSB first, one bit per tick). Once that byte is fully
// on the wire and the line is back LOW, a settling pause of slm_stim_waittime
// ms runs, giving the SLM time to load the pattern; only then does the next
// falling edge of the scanner frame clock fire SLM_STIM_TRIGGER, which is held
// for slm_stim_duration ms before disarming.
//
// The frame clock level is sampled every tick whether armed or not, so the
// comparison is always against the immediately preceding tick rather than a
// stale level from whenever arming last happened. gather() calls this before
// the experiment state machines that arm it, so the edge that fires a stimulus
// is always one sampled strictly after the arming tick.
//
// The phases are exclusive per tick: a tick that advances the transmission or
// the pause never also fires the trigger, so the falling edge acted on is
// always at least slm_stim_waittime ms - in practice one tick more - after
// SLM_STIM_SELECT returned LOW.
static void updateSlmStim() {
  if (!slm_experiment) {
    // Toggled off, possibly mid-stimulus: release both lines and drop anything
    // pending or in flight, so nothing is left asserted. The guard is false on
    // every following tick, so the pins are not driven while idle. Seeding the
    // edge detector LOW means the first tick after a re-enable cannot see a
    // phantom falling edge.
    if (slm_stim_armed || slm_stim_active || slm_select_phase != slmSelIdle) {
      digitalWriteFast(SLM_STIM_SELECT, LOW);
      digitalWriteFast(SLM_STIM_TRIGGER, LOW);
      slm_stim_armed = false;
      slm_stim_active = false;
      slm_select_phase = slmSelIdle;
      slm_select_tick = 0;
    }
    slm_frame_clock_prev = LOW;
    return;
  }

#ifdef SLM_DEBUG
  // Drive the simulated frame clock in place of the pin. The phase durations are
  // counted in gather() ticks, so they are milliseconds only while gatherTimer
  // stays at a 1000 us interval - the same assumption the select transmission
  // below makes. The level is toggled before it is sampled, so the tick that
  // flips the clock is also the tick that sees the edge.
  if (++debug_frame_clock_tick >= (debug_frame_clock
                                       ? debug_frame_clock_high_millis
                                       : debug_frame_clock_low_millis)) {
    debug_frame_clock_tick = 0;
    debug_frame_clock = !debug_frame_clock;
  }
  digitalWriteFast(DEBUG_FRAME_CLOCK_OUT, debug_frame_clock);
  const int slm_frame_clock = debug_frame_clock ? HIGH : LOW;
  // write the status of slm_stim_armed to SLM_DEBUG_OUT
  if (slm_stim_armed)
    digitalWriteFast(SLM_DEBUG_OUT, HIGH);
  else
    digitalWriteFast(SLM_DEBUG_OUT, LOW);

#else
  const int slm_frame_clock = digitalReadFast(SCANNER_FRAME_CLOCK);
#endif
  switch (slm_select_phase) {
    case slmSelIdle:
      if (slm_stim_armed) {
        // Latch the index so a later write to slm_stim_selected cannot corrupt
        // the byte mid-transmission.
        slm_select_byte = slm_stim_selected;
        slm_select_tick = 0;
        digitalWriteFast(SLM_STIM_SELECT, HIGH);
        slm_select_phase = slmSelReset;
      }
      break;

    case slmSelReset:
      if (++slm_select_tick >= slmSelectResetTicks) {
        slm_select_tick = 0;
        digitalWriteFast(SLM_STIM_SELECT,
                         (slm_select_byte >> (slmSelectDataBits - 1)) & 0x1);
        slm_select_phase = slmSelData;
      }
      break;

    case slmSelData:
      if (++slm_select_tick >= slmSelectDataBits) {
        // Last bit has been held for its full tick: return the line to idle.
        digitalWriteFast(SLM_STIM_SELECT, LOW);
        slm_select_tick = 0;
        slm_select_phase = slmSelWait;
      } else {
        digitalWriteFast(
            SLM_STIM_SELECT,
            (slm_select_byte >> (slmSelectDataBits - 1 - slm_select_tick)) & 0x1);
      }
      break;

    case slmSelWait:
      if (!slm_stim_armed) {
        // Arming was withdrawn (e.g. by reset()) during the pause.
        slm_select_phase = slmSelIdle;
      } else if (++slm_select_tick >= slm_stim_waittime) {
        slm_select_phase = slmSelDone;
      }
      break;

    case slmSelDone:
      if (!slm_stim_armed) {
        // Arming was withdrawn (e.g. by reset()) while the byte was going out.
        slm_select_phase = slmSelIdle;
      } else if (slm_frame_clock_prev == HIGH && slm_frame_clock == LOW) {
        digitalWriteFast(SLM_STIM_TRIGGER, HIGH);
        slm_stim_end_millis = current_millis + slm_stim_duration;
        slm_stim_active = true;
        slm_stim_armed = false;
        slm_select_phase = slmSelIdle;
      }
      break;
  }
  slm_frame_clock_prev = slm_frame_clock;

  if (slm_stim_active && current_millis >= slm_stim_end_millis) {
    digitalWriteFast(SLM_STIM_TRIGGER, LOW);
    slm_stim_active = false;
  }
}

// State machine
void applyState(dataPacket* packet) {
  (void)packet; // apply finite state machine updates here
  counter++;
}

// Option to reset all the values
void reset() {
  noInterrupts();
  wheelEncoder.write(0);
  current_millis = 0;
  current_micros = 0;

  for (int i = 0; i < nStates; i++) {
    bufferedStates[i] = 0;
  }

  for (int i = 0; i < nDigitalOut; i++) {
    digitalWriteFast(pinsDigitalOut[i], LOW);
  }
  for (int i = 0; i < nDigitalIn; i++) {
    digitalWriteFast(pinsDigitalIn[i], LOW);
  }

  for (int i = 0; i < nPulsePins; i++) {
    pulsePins[i]->restart();
  }

  // Drop any pending or in-flight SLM stimulus, whatever slm_experiment says -
  // there is nothing to preserve either way. Required because current_millis
  // is zeroed above: a live slm_stim_end_millis would otherwise sit ~49 days in
  // the future and hold the trigger high. Seed the edge detector from the pin so
  // the first tick after a reset cannot see a phantom falling edge. Abandoning a
  // partially sent select byte is safe: SLM_STIM_SELECT was driven LOW by the
  // loop over pinsDigitalOut above, so the receiver sees a truncated packet and
  // no trigger follows it.
  slm_stim_armed = false;
  slm_stim_active = false;
  slm_stim_end_millis = 0;
  slm_select_phase = slmSelIdle;
  slm_select_tick = 0;
#ifdef SLM_DEBUG
  // Restart the simulated clock from its low phase. The loop over pinsDigitalOut
  // above has already driven DEBUG_FRAME_CLOCK_OUT low, so this keeps the
  // variable and the pin in agreement instead of leaving the next tick to undo a
  // forced level.
  debug_frame_clock = false;
  debug_frame_clock_tick = 0;
  slm_frame_clock_prev = LOW;
#else
  slm_frame_clock_prev = digitalReadFast(SCANNER_FRAME_CLOCK);
#endif

  interrupts();
}

// If info received
void onPacketReceived(const uint8_t* buffer, size_t size) {
  // if we receive a command, do what it tells us to do...
  if (buffer[0] == ptINSTR) {
    processInstruction(buffer, size);
  } else {
    dumpBuffer(buffer, size);
  }
}

void processInstruction(const uint8_t* buf, size_t buf_sz) {
  const uint8_t instruction = buf[4];

  uint8_t stride;
  uint8_t target;
  uint8_t pin;
  bytesToLong bul;

  switch (instruction) {
    case instPIN_LOW:
      stride = strideInstLOW;
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        if (target >= nDigitalOut) {
          continue;
        }
        pin = pinsDigitalOut[target];
        digitalWriteFast(pin, LOW);
      }
      break;

    case instPIN_HIGH:
      stride = strideInstHIGH;
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        if (target >= nDigitalOut) {
          continue;
        }
        pin = pinsDigitalOut[target];
        digitalWriteFast(pin, HIGH);
      }
      break;

    case instPIN_TOGGLE:
      stride = strideInstTOGGLE;
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        if (target >= nDigitalOut) {
          continue;
        }
        pin = pinsDigitalOut[target];
        digitalWriteFast(pin, !digitalReadFast(pin));
      }
      break;

    case instUNITY:
      stride = strideInstUNITY;
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        if (target >= nDigital) {
          continue;
        }
        pin = pinsDigital[target];
        digitalWrite(pin, !digitalRead(pin));
      }
      break;

    case instPIN_PULSE:
      stride = strideInstPULSE;
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        for (size_t b = 0; b < sizeof(bul); b++) {
          bul.bytes[b] = buf[pIdx + 1 + b];
        }
        if (target >= nPulsePins) {
          continue;
        }
        pulsePins[target]->pulseMicro(bul.ulong * 1000);
      }
      break;

    case instSET_STATE:
      stride = strideInstSTATE;
      debugPrint("instSet_State");
      for (size_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
        target = buf[pIdx];
        for (size_t b = 0; b < sizeof(bul); b++) {
          bul.bytes[b] = buf[pIdx + 1 + b];
        }
        char msg[24];
        snprintf(msg, sizeof(msg), "%u:%ld", target, bul.slong);
        debugPrint(msg);
        if (target >= nStates) {
          continue;
        }
        bufferedStates[target] = bul.slong;
      }
      break;

    case instRESET:
      debugPrint("Resetting everything!");
      reset();
      break;

    default:
      debugPrint("Unknown command");
      break;
  }
}

// Best-effort diagnostics on EXTSERIAL (Serial1).
//
// Serial1 is deliberately never begin()'d: that would re-mux pins 0/1 to the
// UART, and those two pads are the first entries of pinsDigitalIn[], reported
// as bits 0 and 1 of packet.digitalIn. Enabling the UART would kill those two
// acquisition channels silently.
//
// Without begin() nothing drains the 64 B TX ring, and
// HardwareSerialIMXRT::write9bit() spins forever with no timeout once the ring
// is full (HardwareSerial.cpp:585) - so an unguarded println() would hang the
// firmware permanently. Writing only when the ring provably has room makes
// that impossible: messages are dropped instead. Add EXTSERIAL.begin(115200)
// in setup() (accepting the loss of digitalIn bits 0/1) and every message
// below starts working with no other change.
void debugPrint(const char* msg) {
  const int len = static_cast<int>(strlen(msg));
  if (EXTSERIAL.availableForWrite() >= len + 2) { // + "\r\n"
    EXTSERIAL.println(msg);
  }
}

void dumpBuffer(const uint8_t* buffer, size_t size) {
  char msg[16];
  snprintf(msg, sizeof(msg), "%u", static_cast<unsigned>(size));
  debugPrint(msg);

  // 16 bytes per line, so each write stays well inside the 64 B TX ring
  const size_t bytesPerLine = 16;
  char line[bytesPerLine * 3 + 1];
  size_t pos = 0;

  for (size_t i = 0; i < size; i++) {
    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buffer[i]);
    if ((i + 1) % bytesPerLine == 0 || i + 1 == size) {
      debugPrint(line);
      pos = 0;
    }
  }
}

// PulsePin lookup
PulsePin* getPulsePinById(byte id) {
  for (int i = 0; i < nPulsePins; ++i) {
    if (pulsePins[i]->getId() == id) {
      return pulsePins[i];
    }
  }
  return nullptr;
}

// Synchronization pattern linking the camera to the teensy timing by
// sending a pulsed pattern. The pattern is a counter clocked by the FSTROBE
// signal from the camera.
static void syncBlink() {
  if (!digitalReadFast(PIN_CAMERA_FSTROBE)) {
    digitalWriteFast(PIN_SYNC_LED, (syncCounter >> syncCounterIdx) & 0x1);
    updateSyncCounter = true;
  }
}

// Same counter pattern, mirrored on the ephys sync line
void ephysrand() {
  if (ephys == 1) {
    digitalWriteFast(EPHYS_SYNC, (syncCounter >> syncCounterIdx) & 0x1);
    updateSyncCounter = true;
  }
}

void runExperiment() {
  iPacket++; // Increment iPacket
  // Initial random off interval in milliseconds (12 to 40 seconds)
  static int offInterval = random(12000, 40001);
  static int trialCount = 0; // To track the number of trials

  // Run 10 repetitions
  if (trialCount < 10) {
    // Off period: keep pin LOW for a random time between 12 and 40 seconds
    if (iPacket == offInterval) {
      digitalWriteFast(SHOCK, HIGH);
#ifndef SLM_DEBUG
      digitalWriteFast(TESTSHOCK, HIGH);
#endif
      digitalWriteFast(LED_BUILTIN, HIGH);
    }

    // On period: 2 seconds HIGH
    if (iPacket == offInterval + 2000) {
      digitalWriteFast(SHOCK, LOW);
#ifndef SLM_DEBUG
      digitalWriteFast(TESTSHOCK, LOW);
#endif
      digitalWriteFast(LED_BUILTIN, LOW);
      iPacket = 0;      // Reset iPacket
      trialCount++;     // Increment the trial count
      // Generate a new random off interval for the next trial
      offInterval = random(12000, 40001);
    }
  }
}

void runPreShock() {
  iPacket++;
  // Run 3 repetitions of 2 seconds HIGH, 30 seconds LOW
  for (int i = 0; i < 3; i++) {
    // Set the shock pins and the built-in LED to LOW for 30 seconds
    if (iPacket == 10000) {
      digitalWriteFast(SHOCK, HIGH);
#ifndef SLM_DEBUG
      digitalWriteFast(TESTSHOCK, HIGH);
#endif
      digitalWriteFast(LED_BUILTIN, HIGH);
    }

    if (iPacket == 12000) {
      digitalWriteFast(SHOCK, LOW);
      digitalWriteFast(LED_BUILTIN, LOW);
#ifndef SLM_DEBUG
      digitalWriteFast(TESTSHOCK, LOW);
#endif
      iPacket = 0;
    }
  }
}

// UNCOMMENT THIS FOR CS+ 3KHZ CS- 9KHZ, INVERTED SOUND CODE IS BELOW
/*
void AATC() {
  press_test++;

  if (press_test == 1) {
    triggertime = triggertime + current_millis;
    n_sound1 = 0;
    n_sound2 = 0;
  }
  if ((current_millis == triggertime) && (Tone == 1) && (n_sound1 < 3)) {
    // Tone CS+
    tone(Speaker, 3000, 2000);

    digitalWriteFast(Tone1, HIGH);
    rewardtime = triggertime + 3000;
    tonelength = triggertime + 2000;
    triggertime = triggertime + random(29000, 45000);
    n_sound1 += 1;
    n_sound2 = 0;
    Tone = random(2);
  }
  if (current_millis == tonelength) {
    digitalWriteFast(Tone1, LOW);
    digitalWriteFast(Tone2, LOW);
    analogWrite(Speaker, 0);
  }
  if (current_millis == rewardtime) {
    digitalWriteFast(REWARD, HIGH);
  }
  if (current_millis == rewardtime + 2000) {
    digitalWriteFast(REWARD, LOW);
  }

  if ((current_millis == triggertime) && (Tone == 0) && (n_sound2 < 3)) {
    // Tone CS-
    analogWriteFrequency(Speaker, 9000);
    analogWrite(Speaker, 127);

    digitalWriteFast(Tone2, HIGH);
    tonelength = triggertime + 2000;
    triggertime = triggertime + random(29000, 45000);
    Tone = random(2);
    n_sound2 += 1;
    n_sound1 = 0;
  }
  if (n_sound1 >= 3) {
    Tone = 0;
  }
  if (n_sound2 >= 3) {
    Tone = 1;
  }
}
*/

// INVERTED SOUNDS, CS+ 9KHZ CS- 3KHZ
void AATC() {
  press_test++;

  if (press_test == 1) {
    triggertime = triggertime + current_millis;
    n_sound1 = 0;
    n_sound2 = 0;
  }
  if ((current_millis >= triggertime) && (Tone == 1) && (n_sound1 < 3)) {
    // Tone CS+
    if (slm_experiment) {
      slm_stim_armed = true;
#ifdef SLM_DEBUG
      slm_stim_selected++;
#endif
    } else {
      analogWriteFrequency(SPEAKER, 9000);
      analogWrite(SPEAKER, 127);
      digitalWriteFast(TONE1, HIGH);
    }
    rewardtime = triggertime + 3000;
    tonelength = triggertime + 2000;

#ifdef SLM_DEBUG
    triggertime = triggertime + 6000;
#else
    triggertime = triggertime + random(29000, 45000);
#endif
    n_sound1 += 1;
    n_sound2 = 0;
    Tone = random(2);




    if (!slm_experiment && current_millis == tonelength) {
      digitalWriteFast(TONE1, LOW);
      digitalWriteFast(TONE2, LOW);
      analogWrite(SPEAKER, 0);
    }
    if (current_millis == rewardtime) {
      digitalWriteFast(REWARD, HIGH);
    }
    if (current_millis == rewardtime + 2000) {
      digitalWriteFast(REWARD, LOW);
    }
  }
  if ((current_millis >= triggertime) && (Tone == 0) && (n_sound2 < 3)) {
    // Tone CS-
    if (slm_experiment) {
      slm_stim_armed = true;
#ifdef SLM_DEBUG
      slm_stim_selected++;
#endif
    } else {
      tone(SPEAKER, 3000, 2000);
      digitalWriteFast(TONE2, HIGH);
    }
    tonelength = triggertime + 2000;

#ifdef SLM_DEBUG
    triggertime = triggertime + 6000;
#else
    triggertime = triggertime + random(29000, 45000);
#endif
    Tone = random(2);
    n_sound2 += 1;
    n_sound1 = 0;

  }
  if (n_sound1 >= 3) {
    Tone = 0;
  }
  if (n_sound2 >= 3) {
    Tone = 1;
  }

  if (slm_experiment) {
#ifdef SLM_DEBUG
    if (slm_stim_selected >= 128) slm_stim_selected = 0;
#else
    slm_stim_selected = Tone;
#endif
  }
}
