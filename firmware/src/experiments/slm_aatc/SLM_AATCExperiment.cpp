//
// Created by Francesco Battaglia on 07/09/2026.
//



#include "experiment_config.h"
#include "Experiment.h"
#include "PulsePin.h"
namespace {
  class SLM_AATCExperiment : public Experiment {
  private:

    // Runtime switch for the SLM experiment, replacing the former SLM_EXPERIMENT
    // compile-time flag so it can be toggled between runs without a reflash. Read
    // from gather() (timer ISR) and meant to be written from loop() context, hence
    // volatile.
    volatile bool slm_experiment = true;
    // SLM stimulation. Two independent state machines, both driven from
    // loopMilliPre(): slm_select_stim() sends the stimulus index when
    // slm_stim_armed is set, and slm_trigger_stim() fires a trigger train when
    // slm_stim_fire is set. AATC() sets the flags; the state machines clear them.
    bool slm_stim_armed = false; // request: send slm_stim_selected to the SLM
    bool slm_stim_fire = false; // request: fire a train on the selected stimulus
    // Set once the selection and its settling pause are complete; cleared when
    // the next selection starts. A fire request is dropped while this is false.
    bool slm_stim_ready = false;
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
    // before slm_stim_ready is set. All three durations are counted in gather()
    // ticks, which is why gatherTimer must stay at a 1000 us interval.
    enum slmSelectPhase : uint8_t {
      slmSelIdle, // line LOW, waiting for slm_stim_armed
      slmSelReset, // holding the reset pulse HIGH
      slmSelData, // shifting out the 8 data bits
      slmSelWait // byte sent, holding the post-transmission pause
    };

    static constexpr uint8_t slmSelectResetTicks = 10; // reset pulse length, ms
    static constexpr uint8_t slmSelectDataBits = 8;
    slmSelectPhase slm_select_phase = slmSelIdle;
    // Shared phase counter; uint16_t permits settling pauses up to 65,535 ms.
    uint16_t slm_select_tick = 0; // ticks elapsed in the current phase
    byte slm_select_byte = 0; // slm_stim_selected, latched at tx start

    int AATC_trigger = 0;
    int press_test = 0;
    int n_sound1 = 0;
    int n_sound2 = 0;
    int Tone = 0;
    unsigned long triggertime = 1000;
    unsigned long tonelength = 0;
    unsigned long rewardtime = 0;
    unsigned long iti_min = 29000;
    unsigned long iti_max = 45000;
    // Experimental logic: a stimulus was selected this trial and should be fired
    // as soon as it is ready.
    bool slm_fire_due = false;
    // slm_select_tick counts the pause, so slm_stim_waittime must fit its range.
    static_assert(slm_stim_waittime >= 0 && slm_stim_waittime <= UINT16_MAX,
                  "slm_stim_waittime must fit in slm_select_tick (uint16_t)");

    // slm_stim_pulses_left is pre-decremented on every pulse, so a zero-length
    // train would wrap and run for 256 frames instead of none.
    static_assert(slm_stim_n_triggers >= 1 && slm_stim_n_triggers <= 255,
                  "slm_stim_n_triggers must fit in slm_stim_pulses_left (uint8_t)");

    // Each pulse must be released before the frame clock edge that starts the next
    // one, otherwise the train degenerates into a single long pulse.
    static_assert(slm_stim_duration >= 1,
                  "slm_stim_duration must be at least one gather() tick");

    void slm_select_stim();
    void slm_trigger_stim();
    void AATC();
  public:
    ~SLM_AATCExperiment() override = default;
    void setup() override; // gets called in setup()
    void loopMicro() override; // gets called in loop()
    void loopMilliPre() override; // gets called in gather() before serial port and pin updates
    void loopMilliPost() override; // gets called in gather() after serial port and pin updates
    void reset() override; // gets called in reset()
  };
}

void SLM_AATCExperiment::setup() {
    ;
}

void SLM_AATCExperiment::loopMicro() {
    ;
}

// One tick of the stimulus selection state machine, called from loopMilliPre()
// every millisecond while slm_experiment is on.
//
// When slm_stim_armed is seen in slmSelIdle, the request is consumed and the
// index is clocked out on SLM_STIM_SELECT (reset pulse + 8 bits, MSB first, one
// bit per tick). Once the byte is fully on the wire and the line is back LOW, a
// settling pause of slm_stim_waittime ms gives the SLM time to load the pattern;
// only then is slm_stim_ready set. Starting a selection clears slm_stim_ready and
// aborts any train in progress, so the SLM is never triggered while loading. A
// re-arm while a selection is under way is picked up once it has completed.
void SLM_AATCExperiment::slm_select_stim() {
  switch (slm_select_phase) {
    case slmSelIdle:
      if (slm_stim_armed) {
        // Latch the index so a later write to slm_stim_selected cannot corrupt
        // the byte mid-transmission.
        slm_select_byte = slm_stim_selected;
        slm_stim_armed = false;
        slm_stim_ready = false;
        slm_stim_pulses_left = 0;
        digitalWriteFast(SLM_STIM_TRIGGER, LOW);
        slm_stim_active = false;
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
      if (++slm_select_tick >= slm_stim_waittime) {
        slm_select_tick = 0;
        slm_stim_ready = true;
        slm_select_phase = slmSelIdle;
      }
      break;
  }
}

// One tick of the stimulus trigger state machine, called from loopMilliPre()
// every millisecond while slm_experiment is on, after slm_select_stim().
//
// A slm_stim_fire request is always consumed. It starts a train only if
// slm_stim_ready is set and no train is already running; otherwise it is
// dropped. A train is slm_stim_n_triggers pulses on SLM_STIM_TRIGGER, one
// started on each consecutive falling edge of the scanner frame clock and held
// for slm_stim_duration ms - short enough to end well inside its own frame, so a
// pulse never spans the edge that starts the next one. slm_stim_ready survives
// the train, so the same stimulus can be fired again without reselecting it.
//
// The frame clock level is sampled every tick whether a train is running or
// not, so the comparison is always against the immediately preceding tick
// rather than a stale level. loopMilliPre() calls this before AATC(), so a fire
// request is acted on from the next tick and the edge that starts the train is
// always one sampled strictly after the request.
void SLM_AATCExperiment::slm_trigger_stim() {
  const int slm_frame_clock = digitalReadFast(SCANNER_FRAME_CLOCK);

  if (slm_stim_fire) {
    slm_stim_fire = false;
    if (slm_stim_ready && slm_stim_pulses_left == 0) {
      slm_stim_pulses_left = slm_stim_n_triggers;
    }
  }

  if (slm_stim_pulses_left > 0 &&
      slm_frame_clock_prev == HIGH && slm_frame_clock == LOW) {
    // The release below ends each pulse after slm_stim_duration ms, which the
    // static_assert above plus a frame period longer than that keeps strictly
    // inside the current frame.
    digitalWriteFast(SLM_STIM_TRIGGER, HIGH);
    slm_stim_end_millis = current_millis + slm_stim_duration;
    slm_stim_active = true;
    --slm_stim_pulses_left;
  }
  slm_frame_clock_prev = slm_frame_clock;

  if (slm_stim_active && current_millis >= slm_stim_end_millis) {
    digitalWriteFast(SLM_STIM_TRIGGER, LOW);
    slm_stim_active = false;
  }
}

void SLM_AATCExperiment::loopMilliPre() {
  if (!slm_experiment) {
    // Toggled off, possibly mid-stimulus: release both lines and drop anything
    // pending or in flight, so nothing is left asserted. The guard is false on
    // every following tick, so the pins are not driven while idle. Seeding the
    // edge detector LOW means the first tick after a re-enable cannot see a
    // phantom falling edge.
    if (slm_stim_armed || slm_stim_fire || slm_stim_ready || slm_fire_due ||
        slm_stim_active || slm_stim_pulses_left > 0 ||
        slm_select_phase != slmSelIdle) {
      digitalWriteFast(SLM_STIM_SELECT, LOW);
      digitalWriteFast(SLM_STIM_TRIGGER, LOW);
      slm_stim_armed = false;
      slm_stim_fire = false;
      slm_stim_ready = false;
      slm_fire_due = false;
      slm_stim_active = false;
      slm_stim_pulses_left = 0;
      slm_select_phase = slmSelIdle;
      slm_select_tick = 0;
    }
    slm_frame_clock_prev = LOW;
    return;
  }

  slm_select_stim();
  slm_trigger_stim();

  // the AATC logic
  if (AATC_trigger == 1) {
    AATC();
  } else {
    press_test = 0;
    triggertime = 1000;
  }
}

void SLM_AATCExperiment::loopMilliPost() {
  Experiment::loopMilliPost(); // for lick detection logic
  AATC_trigger = digitalReadFast(TRIGGER_AATC);
}

void SLM_AATCExperiment::reset() {
  // Drop any pending or in-flight SLM stimulus, whatever slm_experiment says -
  // there is nothing to preserve either way. Required because current_millis
  // is zeroed above: a live slm_stim_end_millis would otherwise sit ~49 days in
  // the future and hold the trigger high. Seed the edge detector from the pin so
  // the first tick after a reset cannot see a phantom falling edge. Abandoning a
  // partially sent select byte is safe: SLM_STIM_SELECT was driven LOW by the
  // loop over pinsDigitalOut above, so the receiver sees a truncated packet and
  // no trigger follows it.
  slm_stim_armed = false;
  slm_stim_fire = false;
  slm_stim_ready = false;
  slm_fire_due = false;
  slm_stim_active = false;
  slm_stim_end_millis = 0;
  slm_stim_pulses_left = 0;
  slm_select_phase = slmSelIdle;
  slm_select_tick = 0;
  slm_frame_clock_prev = digitalReadFast(SCANNER_FRAME_CLOCK);
}

// INVERTED SOUNDS, CS+ 9KHZ CS- 3KHZ
void SLM_AATCExperiment::AATC() {
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
      slm_fire_due = true;
    } else {
      analogWriteFrequency(SPEAKER, 9000);
      analogWrite(SPEAKER, 127);
      digitalWriteFast(TONE1, HIGH);
    }
    rewardtime = triggertime + 3000;
    tonelength = triggertime + 2000;

    triggertime = triggertime + random(29000, 45000);
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
      slm_fire_due = true;
    } else {
      tone(SPEAKER, 3000, 2000);
      digitalWriteFast(TONE2, HIGH);
    }
    tonelength = triggertime + 2000;

    triggertime = triggertime + random(iti_min, iti_max);
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
    // Fire the stimulus selected this trial as soon as it is ready. On the tick
    // it is armed slm_stim_ready may still be set from the previous stimulus,
    // but slm_stim_armed is only consumed on the next tick, which also clears
    // slm_stim_ready; so slm_fire_due is held until the new selection and its
    // settling pause have completed.
    if (slm_fire_due && slm_stim_ready && !slm_stim_armed) {
      slm_stim_fire = true;
      slm_fire_due = false;
    }
    slm_stim_selected = Tone;
  }
}


// generate the experiment object of the proper class, defined here
Experiment *makeExperiment() { return new SLM_AATCExperiment(); }