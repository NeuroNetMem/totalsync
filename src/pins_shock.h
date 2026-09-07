

#ifndef PINS_SHOCK_H
#define PINS_SHOCK_H
#include "Experiment.h"
// Named pins in use on the Teensy
#define WHEEL_ENC_PINA 2
#define WHEEL_ENC_PINB 3
#define WHEEL_ENC_SW 4
#define BLICK 5
#define SCANNER_FRAME_CLOCK 6
#define SPEAKER 7
// #define LED_1 8

#define PIN_CAMERA_FSTROBE 12
#define LICK 17
#define VALVE 24
#define REWARD 25
#define LICKDETECT 26
#define TONE1 27
#define TONE2 28
#define TRIGGER_AATC 29
#define EXPER 30
#define SHOCK 31
#define PRESHOCK 32
#define TESTSHOCK 35

#define EPHYS_TRIGGER 36
#define EPHYS_SYNC 37
#define PIN_SYNC_LED 38
#define TRIGGER_C 39

// Pins used to scope the communication / acquisition timing
#define LOOP_INDICATOR 40
#define GATHER_INDICATOR 41

class OFL_ShockExperiment : public Experiment {

private:
    int iPacket = 0;
    int exper = 0;
    int preshock = 0;
    void runExperiment();
    void runPreShock();

public:
    ~OFL_ShockExperiment() override = default;
    void setup() override; // gets called in setup()
    void loopMicro() override; // gets called in loop()
    void loopMilliPre() override; // gets called in gather() before serial port and pin updates
    void loopMilliPost() override; // gets called in gather() after serial port and pin updates
    void reset() override; // gets called in reset()

};

#endif // PINS_SHOCK_H
