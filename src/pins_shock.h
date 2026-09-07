// Pin map for the SLM / AATC setup on the Teensy 4.1.
//
// Every named pin used by main.cpp lives here, so switching to a different
// experiment means swapping this one header rather than editing main.cpp.
//
// SLM_DEBUG must be defined (or left undefined) *before* this header is
// included: pins 32 and 35 are shared between the debug outputs and
// PRESHOCK / TESTSHOCK.
//
// Whether the SLM stimulation code actually runs is a runtime decision, taken
// on the slm_experiment flag in main.cpp - the pin assignments below do not
// depend on it.

#ifndef PINS_SLM_H
#define PINS_SLM_H

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

#endif // PINS_SLM_H
