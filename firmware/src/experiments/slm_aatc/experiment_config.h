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

// Pin 32 is shared: with SLM_DEBUG on it reports slm_stim_armed for scoping.
#ifdef SLM_DEBUG
#define SLM_DEBUG_OUT 32
#endif

#define SLM_STIM_SELECT 33
#define SLM_STIM_TRIGGER 34

// Pin 35 is shared: with SLM_DEBUG on it carries the simulated frame clock, so
// TESTSHOCK is undefined and its writes in runExperiment() / runPreShock() are
// compiled out rather than allowed to fight the clock for the same pad.
#ifdef SLM_DEBUG
#define DEBUG_FRAME_CLOCK_OUT 35
#endif

#define EPHYS_TRIGGER 36
#define EPHYS_SYNC 37
#define PIN_SYNC_LED 38
#define TRIGGER_C 39

// Pins used to scope the communication / acquisition timing
#define LOOP_INDICATOR 40
#define GATHER_INDICATOR 41

Experiment *makeExperiment();

#endif // EXPERIMENT_CONFIG_H
