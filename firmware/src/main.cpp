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
// #include <cmath>
#include <cstdio>
#include <cstring>

#include "PulsePin.h"

// ---------------------------------------------------------------------------
// 2. Pin map and compile-time configuration
// ---------------------------------------------------------------------------

// Serial port used for human readable diagnostics
#define EXTSERIAL Serial1


// Named pins. Included here rather than with the other headers because it
// reads SLM_DEBUG.
#include <experiment_config.h>

// Analog and digital channels scanned every gather tick
const int pinsAnalogIn[] = {16, 17, 18, 19, 20, 21, 22};
constexpr int nAnalogIn = std::size(pinsAnalogIn);

const int pinsDigitalIn[] = {0, 1, 2,  3,  4,  5,  6,  7,
                             8, 9, 10, 11, 12, 13, 14, 15};
constexpr int nDigitalIn = std::size(pinsDigitalIn);

const int pinsDigitalOut[] = {24, 25, 26, 27, 28, 29, 30, 31,
                              32, 33, 34, 35, 36, 37, 38, 39};
constexpr int nDigitalOut = std::size(pinsDigitalOut);

// All addressable digital pins (inputs followed by outputs), used by instUNITY
const int pinsDigital[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                           11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29,
                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
constexpr int nDigital = std::size(pinsDigital);

// Number of state variables shipped with every data packet
constexpr int nStates = 8;

// // On-board LED
// const int ledPin = LED_BUILTIN;

// Pulse pins for the pupil camera
constexpr int pinsPulsePins[] = {30, 31};
constexpr int nPulsePins = std::size(pinsPulsePins);

// Instruction strides, in bytes (target byte + payload)
constexpr uint8_t strideInstLOW = 2;
constexpr uint8_t strideInstHIGH = 2;
constexpr uint8_t strideInstTOGGLE = 2;
constexpr uint8_t strideInstUNITY = 2;
constexpr uint8_t strideInstPULSE = 5;
constexpr uint8_t strideInstSTATE = 5;

// Synchronization pattern for the pupil camera
constexpr uint16_t syncCounterMax = 0x95FF;
constexpr uint16_t syncCounterMin = 0x9500;

constexpr float EncoderConversion = 0.077; // was 0.0159
// ---------------------------------------------------------------------------
// 3. Packet / instruction types
// ---------------------------------------------------------------------------

namespace {
  enum packetType : uint8_t {
    ptSTATUS,
    ptINSTR,
    ptERROR,
    ptOK [[maybe_unused]],
    ptACK [[maybe_unused]]
  };
}

namespace {
  enum instructionType : uint8_t {
    instPIN_LOW = 0,
    instPIN_HIGH = 1,
    instPIN_TOGGLE = 2,
    instPIN_PULSE = 3,
    instSET_STATE = 4,
    instUNITY = 5,
    instRESET = 6,
    instHANDSHAKE [[maybe_unused]] = 149
  };
}

namespace {
  union bytesToLong {
    byte bytes[4];
    unsigned long ulong;
    long slong;
  };
}

// Running packet count, incremented by every packet constructor
static volatile unsigned long packetCount = 0;

namespace {
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
    [[maybe_unused]] uint8_t padding[1];         // 1 B, align to 4B

    dataPacket()
      : type(ptSTATUS),
        length(sizeof(dataPacket)),
        crc16(0),
        packetID(packetCount++), us_start(0), us_end(0), analog{}, variables{},
        digitalIn(0),
        digitalOut(0), padding{} {
    }
  };
}

// Worst-case size of one data packet on the wire: the COBS-encoded payload
// (size + size/254 + 1, see COBS::getEncodedBufferSize) plus the delimiter
// byte, which PacketSerial::send() writes as a second, separate call. 72 B of
// payload -> 74 B on the wire.
constexpr int packetWireSize =
    static_cast<int>(sizeof(dataPacket) + sizeof(dataPacket) / 254 + 2);

namespace {
  struct errorPacket {
    uint8_t type;            // 1 B, packet type
    uint8_t length;          // 1 B, packet size
    uint16_t crc16;          // 2 B, CRC16
    unsigned long packetID;  // 4 B, running packet count
    [[maybe_unused]] unsigned long us_start;  // 4 B, gather start timestamp

    [[maybe_unused]] char message[16];        // 16 B, error message

    errorPacket()
      : type(ptERROR),
        length(sizeof(errorPacket)),
        crc16(0),
        packetID(packetCount++), us_start(0), message{} {
    }
  };
}

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
// static int camera_trigger = 0;
static int ephys = 1;
// int rad = 0;

// int epacket = 0;
// int t = 0;


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
static void applyState(dataPacket* packet);
static void reset();

// Serial communication
static void onPacketReceived(const uint8_t* buffer, size_t size);
static void processInstruction(const uint8_t* buf, size_t buf_sz);
static void dumpBuffer(const uint8_t* buffer, size_t size);
static void debugPrint(const char* msg);

// Pulse pins and camera / ephys synchronization
//static PulsePin* getPulsePinById(byte id);
static void syncBlink();

static void ephysrand();

static Experiment *experiment = makeExperiment();
//
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

  for (int i : pinsAnalogIn) {
    pinMode(i, INPUT);
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
  for (int i : pinsDigitalOut) {
    pinMode(i, OUTPUT);
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
  experiment->setup();
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
  for (auto & pulsePin : pulsePins) {
    pulsePin->updateMicro();
  }

  experiment->loopMicro();
  digitalWriteFast(LOOP_INDICATOR, LOW);
}

// ---------------------------------------------------------------------------
// 7. Function definitions
// ---------------------------------------------------------------------------

// Acquisition tick, driven by gatherTimer
static void gather() {
  digitalWriteFast(GATHER_INDICATOR, HIGH); // toggle pin to indicate gather start
  dataPacket packet;

  experiment->loopMilliPre();

  if (ephys == 1) {
    ephysrand();
  } else {
    digitalWriteFast(EPHYS_SYNC, LOW);
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

  // camera_trigger = digitalReadFast(TRIGGER_C);
  reward = digitalReadFast(REWARD);
  // ephys = digitalReadFast(ephys_trigger);

  experiment->loopMilliPost();
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
  const long position_feel = static_cast<long>(new_pos * EncoderConversion);
  interrupts();

  for (long & variable : packet.variables) {
    variable = 0L;
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
// ReSharper disable once CppParameterMayBeConstPtrOrRef
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

  // noinspection
  for (int i = 0; i < nStates; i++) {
    bufferedStates[i] = 0;
  }

  for (int i : pinsDigitalOut) {
    digitalWriteFast(i, LOW);
  }
  for (int i : pinsDigitalIn) {
    digitalWriteFast(i, LOW);
  }

  for (auto & pulsePin : pulsePins) {
    pulsePin->restart();
  }

  experiment->reset();

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
        // ReSharper disable once CppObjectMemberMightNotBeInitialized
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
        // ReSharper disable once CppObjectMemberMightNotBeInitialized
        snprintf(msg, sizeof(msg), "%u:%ld", target, bul.slong);
        debugPrint(msg);
        if (target >= nStates) {
          continue;
        }
        // ReSharper disable once CppObjectMemberMightNotBeInitialized
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
  constexpr size_t bytesPerLine = 16;
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



// PulsePin lookup
// PulsePin* getPulsePinById(byte id) {
//   for (int i = 0; i < nPulsePins; ++i) {
//     if (pulsePins[i]->getId() == id) {
//       return pulsePins[i];
//     }
//   }
//   return nullptr;
// }