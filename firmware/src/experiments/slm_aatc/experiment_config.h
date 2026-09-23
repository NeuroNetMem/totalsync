// Pin map for the SLM / AATC setup on the Teensy 4.1.
//
// Every named pin used by main.cpp lives here, so switching to a different
// experiment means swapping this one header rather than editing main.cpp.
//
//

#ifndef EXPERIMENT_CONFIG_H
#define EXPERIMENT_CONFIG_H

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

#define SLM_STIM_SELECT 33
#define SLM_STIM_TRIGGER 34

#define EPHYS_TRIGGER 36
#define EPHYS_SYNC 37
#define PIN_SYNC_LED 38
#define TRIGGER_C 39

// Pins used to scope the communication / acquisition timing
#define LOOP_INDICATOR 40
#define GATHER_INDICATOR 41

// State channels shipped in every data packet (packet.variables[]), written
// through state_variables[] (see Experiment.h). 0-3 and 7 are filled by gather()
// in main.cpp; 4-6 are free for the experiment - rename them when used.
#define STATE_WHEEL_POS 0        // raw encoder count
#define STATE_WHEEL_POS_SCALED 1 // count * EncoderConversion
#define STATE_BINARY_LICK 2
#define STATE_LICK 3
#define STATE_FREE_4 4
#define STATE_FREE_5 5
#define STATE_FREE_6 6
#define STATE_LAST_PACKET_TOOK 7 // duration of the previous gather(), us

Experiment *makeExperiment();

#endif // EXPERIMENT_CONFIG_H
