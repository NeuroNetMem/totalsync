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
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PulsePin.h"

// ---------------------------------------------------------------------------
// 2. Pin map and compile-time configuration
// ---------------------------------------------------------------------------

// Serial port used for human readable diagnostics
#define EXTSERIAL Serial1

// Analog and digital channels scanned every gather tick
const int pinsAnalogIn[] = {16, 17, 18, 19, 20, 21, 22, 23};
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

// Named pins in use on the Teensy
#define WHEEL_ENC_PINA 2
#define WHEEL_ENC_PINB 3
#define WHEEL_ENC_SW 4
#define BLICK 5
#define Speaker 7
#define LED_1 8
#define PIN_CAMERA_FSTROBE 12
#define LICK 17
#define VALVE 24
#define REWARD 25
#define LICKDETECT 26
#define Tone1 27
#define Tone2 28
#define trigger_aatc 29
#define EXPER 30
#define SHOCK 31
#define PRESHOCK 32
#define LED_2 33
#define TESTSHOCK 35
#define ephys_trigger 36
#define ephys_sync 37
#define PIN_SYNC_LED 38
#define trigger_c 39

// Pins used to scope the communication / acquisition timing
#define LOOP_INDICATOR 40
#define GATHER_INDICATOR 41

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
volatile unsigned long packetCount = 0;

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

// Data transport
FastCRC16 CRC16;
PacketSerial packetSerialA;
PacketSerial packetSerialB;

// Timing. current_millis / current_micros are also used by PulsePin
IntervalTimer gatherTimer;
elapsedMicros current_micros;
elapsedMillis current_millis;

// Wheel encoder
Encoder wheelEncoder(WHEEL_ENC_PINA, WHEEL_ENC_PINB);

// Timed output pins
PulsePin* pulsePins[nPulsePins];

// Moving average for lick detection
const int windowSize = 10;    // size of moving average window
int lickReadings[windowSize]; // the readings from the lick input
int windowIndex = 0;          // the index of the current reading
int totalLickReadings = 0;    // the running total
int averageLickReadings = 0;  // the average
int lickThresh = 2600;
bool binaryLick = false;

// Intermediate values
long encoderPosition = 0;
int last_packet_took = 0;
int reward = 0;
int lick = 0;
int lickBaselineCorr = 0;
int lickBaseline = 0;
int ipacket = 0;
int frame = 0;
int camera_trigger = 0;
int ephys = 1;
int rad = 0;
int exper = 0;
int preshock = 0;
int epacket = 0;
int t = 0;
int iPacket = 0;
int AATC_trigger = 0;
int press_test = 0;
int n_sound1 = 0;
int n_sound2 = 0;
int Tone = 0;
unsigned long triggertime = 1000;
unsigned long tonelength = 0;
unsigned long rewardtime = 0;

volatile unsigned char counter = 0;
volatile long bufferedStates[nStates];

volatile bool gatherNow = false;
volatile bool packetReady = false;

// Pupil camera synchronization counter
volatile uint16_t syncCounter = syncCounterMax;
volatile uint16_t syncCounterFrameInterval = 100; // count N frames as 'clock'
volatile byte syncCounterIdx = 0;
volatile byte syncCounterSubIdx = 0;
volatile bool updateSyncCounter = true;

// Current state, overwritten on every gather
dataPacket State;

// ---------------------------------------------------------------------------
// 5. Forward declarations
// ---------------------------------------------------------------------------

// Acquisition
void gather();
void applyState(dataPacket* packet);
void reset();

// Serial communication
void onPacketReceived(const uint8_t* buffer, size_t size);
void processInstruction(const uint8_t* buf, size_t buf_sz);
void dumpBuffer(const uint8_t* buffer, size_t size);
void debugPrint(const char* msg);

// Pulse pins and camera / ephys synchronization
PulsePin* getPulsePinById(byte id);
void syncBlink();
void ephysrand();

// Experiment state machines
void runExperiment();
void runPreShock();
void AATC();

// ---------------------------------------------------------------------------
// 6. Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(LED_1, OUTPUT); // your first LED
  pinMode(LED_2, OUTPUT);

  // Turn them on
  digitalWriteFast(LED_1, HIGH);
  digitalWriteFast(LED_2, HIGH);

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
    State.crc16 = CRC16.kermit((uint8_t*)&State, sizeof(State));

    // Only write to a port that is open (configured + DTR asserted) and has
    // room for a whole packet. usb_serialN_write() spins for up to
    // TX_TIMEOUT_MSEC (120 ms) when the host holds the port open but stops
    // draining it, which would stall the other port and the rest of loop()
    // along with it. Packets are dropped rather than queued: gather()
    // overwrites State every millisecond regardless.
    if (SerialUSB1 && SerialUSB1.availableForWrite() >= packetWireSize) {
      packetSerialA.send((byte*)&State, sizeof(State));
    }
    if (SerialUSB2 && SerialUSB2.availableForWrite() >= packetWireSize) {
      packetSerialB.send((byte*)&State, sizeof(State));
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
void gather() {
  digitalWriteFast(GATHER_INDICATOR, HIGH); // toggle pin to indicate gather start
  dataPacket packet;

  if (ephys == 1) {
    ephysrand();
  } else {
    digitalWriteFast(ephys_sync, LOW);
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

  camera_trigger = digitalReadFast(trigger_c);
  reward = digitalReadFast(REWARD);
  // ephys = digitalReadFast(ephys_trigger);
  exper = digitalReadFast(EXPER);
  preshock = digitalReadFast(PRESHOCK);
  AATC_trigger = digitalReadFast(trigger_aatc);

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
void syncBlink() {
  if (!digitalReadFast(PIN_CAMERA_FSTROBE)) {
    digitalWriteFast(PIN_SYNC_LED, (syncCounter >> syncCounterIdx) & 0x1);
    updateSyncCounter = true;
  }
}

// Same counter pattern, mirrored on the ephys sync line
void ephysrand() {
  if (ephys == 1) {
    digitalWriteFast(ephys_sync, (syncCounter >> syncCounterIdx) & 0x1);
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
      digitalWriteFast(TESTSHOCK, HIGH);
      digitalWriteFast(LED_BUILTIN, HIGH);
    }

    // On period: 2 seconds HIGH
    if (iPacket == offInterval + 2000) {
      digitalWriteFast(SHOCK, LOW);
      digitalWriteFast(TESTSHOCK, LOW);
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
      digitalWriteFast(TESTSHOCK, HIGH);
      digitalWriteFast(LED_BUILTIN, HIGH);
    }

    if (iPacket == 12000) {
      digitalWriteFast(SHOCK, LOW);
      digitalWriteFast(LED_BUILTIN, LOW);
      digitalWriteFast(TESTSHOCK, LOW);
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
  if ((current_millis == triggertime) && (Tone == 1) && (n_sound1 < 3)) {
    // Tone CS+
    analogWriteFrequency(Speaker, 9000);
    analogWrite(Speaker, 127);

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
    tone(Speaker, 3000, 2000);

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
