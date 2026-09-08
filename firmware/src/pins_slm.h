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

#include "PulsePin.h"
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


class SLM_AATCExperiment : public Experiment {
private:

    // Runtime switch for the SLM experiment, replacing the former SLM_EXPERIMENT
    // compile-time flag so it can be toggled between runs without a reflash. Read
    // from gather() (timer ISR) and meant to be written from loop() context, hence
    // volatile.
    volatile bool slm_experiment = true;
#ifdef SLM_DEBUG
    // Simulated scanner frame clock, so the SLM path can be exercised on the bench
    // with nothing attached to SCANNER_FRAME_CLOCK. debug_frame_clock replaces the
    // pin reading in gather() and is mirrored on DEBUG_FRAME_CLOCK_OUT for scoping.
    //
    // The low phase must stay above one gather() tick, or the falling edge can fall
    // between two samples and be missed - the same Nyquist limit the real clock is
    // subject to.
    bool debug_frame_clock = false;
    byte debug_frame_clock_high_millis = 26;
    byte debug_frame_clock_low_millis = 4;
    byte debug_frame_clock_tick = 0; // ticks elapsed in the current phase
#endif
    // SLM stimulation
    bool slm_stim_armed = false;
    static constexpr int slm_stim_n_triggers = 3;
    static constexpr int slm_stim_duration = 10;
    static constexpr int slm_stim_waittime = 10;
    bool slm_stim_active = false; // trigger currently held high
    unsigned long slm_stim_end_millis = 0; // when to release the trigger
    int slm_frame_clock_prev = HIGH; // frame clock level on the last tick
    byte slm_stim_selected = 0;
    uint8_t slm_stim_pulses_left = 0; // pulses still owed in this train

    // One-wire transmission of slm_stim_selected on SLM_STIM_SELECT. The line idles
    // LOW; a HIGH reset pulse of slmSelectResetTicks ms is followed by the 8 data
    // bits, MSB first, one bit per ms, then a settling pause of slm_stim_waittime ms
    // before the trigger may fire. All three durations are counted in gather()
    // ticks, which is why gatherTimer must stay at a 1000 us interval.
    enum slmSelectPhase : uint8_t {
        slmSelIdle, // line LOW, waiting for slm_stim_armed
        slmSelReset, // holding the reset pulse HIGH
        slmSelData, // shifting out the 8 data bits
        slmSelWait, // byte sent, holding the post-transmission pause
        slmSelDone // pause elapsed, firing one pulse per frame clock edge
    };

    static constexpr uint8_t slmSelectResetTicks = 10; // reset pulse length, ms
    static constexpr uint8_t slmSelectDataBits = 8;
    slmSelectPhase slm_select_phase = slmSelIdle;
    uint8_t slm_select_tick = 0; // ticks elapsed in the current phase
    byte slm_select_byte = 0; // slm_stim_selected, latched at tx start

    int AATC_trigger = 0;
    int press_test = 0;
    int n_sound1 = 0;
    int n_sound2 = 0;
    int Tone = 0;
    unsigned long triggertime = 1000;
    unsigned long tonelength = 0;
    unsigned long rewardtime = 0;
    // slm_select_tick counts the pause, so a wait longer than the counter can hold
    // would wrap and never reach the comparison.
    static_assert(slm_stim_waittime >= 0 && slm_stim_waittime <= 255,
                  "slm_stim_waittime must fit in slm_select_tick (uint8_t)");

    // slm_stim_pulses_left is pre-decremented on every pulse, so a zero-length
    // train would wrap and run for 256 frames instead of none.
    static_assert(slm_stim_n_triggers >= 1 && slm_stim_n_triggers <= 255,
                  "slm_stim_n_triggers must fit in slm_stim_pulses_left (uint8_t)");

    // Each pulse must be released before the frame clock edge that starts the next
    // one, otherwise the train degenerates into a single long pulse.
    static_assert(slm_stim_duration >= 1,
                  "slm_stim_duration must be at least one gather() tick");

    void AATC();
public:
    ~SLM_AATCExperiment() override = default;
    void setup() override; // gets called in setup()
    void loopMicro() override; // gets called in loop()
    void loopMilliPre() override; // gets called in gather() before serial port and pin updates
    void loopMilliPost() override; // gets called in gather() after serial port and pin updates
    void reset() override; // gets called in reset()
};
#endif // PINS_SLM_H
