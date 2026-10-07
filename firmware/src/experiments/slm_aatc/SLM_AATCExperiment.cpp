//
// Created by Francesco Battaglia on 07/09/2026.
//



#include "experiment_config.h"
#include "Experiment.h"
#include "PulsePin.h"

// Uncomment (or add -D SIMULATED_FRAME_CLOCK to build_flags) to drive a simulated
// 30 Hz scanner frame clock on SCANNER_FRAME_CLOCK, for bench tests and demos with
// no microscope attached. Leave it off whenever a real frame clock is connected:
// the pin is turned into an output and would fight the external signal.
// #define SIMULATED_FRAME_CLOCK

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
    static constexpr int slm_stim_waittime = 1000;
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

    enum AATCPhase : uint8_t {
      AATCIdle, // aatc_trigger pin is low, experiment is not running
      AATCReady, // ready to start a new trial, set up, start a ITI and fo to AATCArmed
      AATCArmed, // trial is set up (stimulation if necessary), inter-trial interval ongoing
      AATCCSOn, // Tone or stimulation turned on
      AATCTrace, // All CS off, trace period ongoing
      AATCReward, // Reward delivery ongoing (if needed)
      AATCReset, // AATC trigger turned off, do clean up and go to AATCIdle
    };
    static constexpr uint8_t slmSelectResetTicks = 10; // reset pulse length, ms
    static constexpr uint8_t slmSelectDataBits = 8;
    slmSelectPhase slm_select_phase = slmSelIdle;
    AATCPhase aatc_phase = AATCIdle;
    // Shared phase
    // counter; uint16_t permits settling pauses up to 65,535 ms.
    uint16_t slm_select_tick = 0; // ticks elapsed in the current phase
    byte slm_select_byte = 0; // slm_stim_selected, latched at tx start

    int aatc_trigger = 0;
    int aatc_reversal = 0;
    int slm_rewarded = 0;
    int aatc_tone = 0;
    // slm_experiment, latched when a trial is set up in AATCReady, so that a
    // trial is ended in the same mode (SLM or tone) it was started in even if
    // SLM_STIMULATION changes part way through.
    bool aatc_slm_trial = false;
    // slm_experiment as seen on the previous AATC() tick, to detect a toggle of
    // SLM_STIMULATION, which ends the current trial.
    bool slm_experiment_prev = false;
    // aatc_trigger as seen on the previous AATC() tick, to detect a toggle of
    // AATC_EXP_ON, which restarts the tone counts.
    int aatc_trigger_prev = 0;
    int tone_rep_count[2] = {0,0};
    int tone_tot_count[2] = {0,0};
    int max_tone_reps = 2;
    unsigned long triggertime = 1000;
    unsigned long tone_length = 2000;
    unsigned long trace_time = 1000;
    unsigned long reward_duration = 2000;
    unsigned long iti_min = 6050; // 29000;
    unsigned long iti_max = 6100; //45000;
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
    static int frame_clock_level();
    void AATC();

    static void turnTone1On();
    static void turnTone2On();
    static void turnTonesOff();
    static void releaseAATCOutputs();
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
  // pinMode(SPEAKER, OUTPUT);
#ifdef SIMULATED_FRAME_CLOCK
  // main.cpp has already made it an input, along with the rest of pinsDigitalIn.
  pinMode(SCANNER_FRAME_CLOCK, OUTPUT);
#endif
}

// Level of the scanner frame clock on this tick. With SIMULATED_FRAME_CLOCK it is
// a 30 Hz square wave derived from current_millis: HIGH for the first half of each
// 1000/30 ms period, LOW for the second, so the falling edges come at 30 Hz on
// average, 33 or 34 ms apart given the 1 ms tick. Reducing current_millis modulo
// 1000 first keeps the product from overflowing.
int SLM_AATCExperiment::frame_clock_level() {
#ifdef SIMULATED_FRAME_CLOCK
  return ((current_millis % 1000) * 30) % 1000 < 500 ? HIGH : LOW;
#else
  return digitalReadFast(SCANNER_FRAME_CLOCK);
#endif
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
// the train, so the same stimulus can be fired again without re-selecting it.
//
// The frame clock level is sampled every tick whether a train is running or
// not, so the comparison is always against the immediately preceding tick
// rather than a stale level. loopMilliPre() calls this before AATC(), so a fire
// request is acted on from the next tick and the edge that starts the train is
// always one sampled strictly after the request.
void SLM_AATCExperiment::slm_trigger_stim() {
  const int slm_frame_clock = frame_clock_level();

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
#ifdef SIMULATED_FRAME_CLOCK
  // Drive the simulated clock in both modes, before gather() samples the digital
  // inputs, so it is recorded and can be scoped like the real one.
  digitalWriteFast(SCANNER_FRAME_CLOCK, frame_clock_level());
#endif
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
  } else {
    slm_select_stim();
    slm_trigger_stim();
  }

  // the AATC logic runs in both modes: SLM stimulation or tones
  AATC();

}

void SLM_AATCExperiment::loopMilliPost() {
  Experiment::loopMilliPost(); // for lick detection logic
  aatc_trigger = digitalReadFast(AATC_EXP_ON);
  aatc_reversal = digitalReadFast(AATC_REVERSAL);
  slm_experiment = digitalReadFast(SLM_STIMULATION);
  slm_rewarded = digitalReadFast(SLM_REWARDED);
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
  aatc_phase = AATCIdle;
  aatc_slm_trial = false;
  releaseAATCOutputs();
  slm_select_tick = 0;
  slm_frame_clock_prev = frame_clock_level();
}


void SLM_AATCExperiment::turnTone1On() {
  analogWriteFrequency(SPEAKER, 9000);
  analogWrite(SPEAKER, 127);
  digitalWriteFast(TONE1, HIGH);
}

void SLM_AATCExperiment::turnTone2On() {
  tone(SPEAKER, 3000, 2000);
  digitalWriteFast(TONE2, HIGH);
}

void SLM_AATCExperiment::turnTonesOff() {
  digitalWriteFast(TONE1, LOW);
  digitalWriteFast(TONE2, LOW);
  // tone() runs on its own timer until its duration elapses, so stop it
  // explicitly rather than rely on that duration matching tone_length.
  noTone(SPEAKER);
  analogWrite(SPEAKER, 0);
}

// Release every output the AATC state machine may have left asserted: tones,
// SLM CS indicators and the reward (which VALVE follows in gather()).
void SLM_AATCExperiment::releaseAATCOutputs() {
  turnTonesOff();
  digitalWriteFast(SLM_STIM_1, LOW);
  digitalWriteFast(SLM_STIM_2, LOW);
  digitalWriteFast(REWARD, LOW);
}

void SLM_AATCExperiment::AATC() {
  // AATCIdle, // aatc_trigger pin is low, experiment is not running
  // AATCReady, a new trial is starting
  //     AATCArmed, // trial is set up (stimulation if necessary), inter-trial interval ongoing
  //     AATCCSOn, // Tone or stimulation turned on
  //     AATCTrace, // All CS off, trace period ongoing
  //     AATCReward, // Reward delivery ongoing (if needed)
  //     AATCReset, // AATC trigger turned off, do clean up and go to AATCIdle

  if (aatc_trigger == 0 && aatc_phase != AATCIdle)
    aatc_phase = AATCReset;
  // Toggling AATC_EXP_ON also restarts the tone counts.
  if (aatc_trigger != aatc_trigger_prev) {
    aatc_trigger_prev = aatc_trigger;
    tone_tot_count[0] = 0;
    tone_tot_count[1] = 0;
  }
  // Toggling SLM_STIMULATION ends the current trial and restarts the tone counts.
  // AATCReset below cleans up on this same tick.
  if (slm_experiment != slm_experiment_prev) {
    slm_experiment_prev = slm_experiment;
    tone_tot_count[0] = 0;
    tone_tot_count[1] = 0;
    if (aatc_phase != AATCIdle)
      aatc_phase = AATCReset;
  }
  switch (aatc_phase) {
    case AATCIdle:
      if (aatc_trigger == 1) {
        // Go through AATCReady so the first trial also picks its tone, sets up
        // the stimulation and starts a fresh ITI.
        aatc_phase = AATCReady;
        tone_rep_count[0] = 0;
        tone_rep_count[1] = 0;
      }
      break;
    case AATCReady:
      // stim selection logic
      aatc_tone = random(2);
      tone_rep_count[aatc_tone]++;
      if (tone_rep_count[aatc_tone] > max_tone_reps) {
        tone_rep_count[aatc_tone] = 0;
        aatc_tone = 1 - aatc_tone;
      }
      tone_tot_count[aatc_tone]++;

      // set up stimulation
      aatc_slm_trial = slm_experiment;
      if (aatc_slm_trial) {
        slm_stim_selected = aatc_tone + 1;
        slm_stim_armed = true;
        slm_fire_due = true;

      }
      triggertime = current_millis + random(iti_min, iti_max); // start the ITI
                                                                                  // triggertime is now the time
                                                                                  // at which the next phase must start
      aatc_phase = AATCArmed;
      break;
    case AATCArmed:
      if (current_millis > triggertime) {
        // ready to start the CS, whether a slm stimulation or a Tone
        if (aatc_slm_trial) {
          slm_stim_fire = true;
          if (slm_stim_selected == 1) {
            digitalWriteFast(SLM_STIM_1, HIGH);
          } else {
            digitalWriteFast(SLM_STIM_2, HIGH);
          }

        }
        else {
          if (aatc_tone == 0) {
            turnTone1On();
          } else {
            turnTone2On();
          }
        }
        triggertime = current_millis + tone_length;
        aatc_phase = AATCCSOn;
      }

      break;
    case AATCCSOn:
      if (current_millis > triggertime) {
        // terminate sound delivery if needed, go to trace
        if (aatc_slm_trial) {
          digitalWriteFast(SLM_STIM_1, LOW);
          digitalWriteFast(SLM_STIM_2, LOW);
        } else {
          turnTonesOff();
        }
        aatc_phase = AATCTrace;
        triggertime = current_millis + trace_time;
      }
      break;
    case AATCTrace:
      if (current_millis > triggertime) {
        // when trace is concluded, turn on reward if appropriate
        aatc_phase = AATCReward;
        if ((aatc_tone != aatc_reversal) && (!aatc_slm_trial || slm_rewarded)) {
          digitalWriteFast(REWARD, HIGH);
        }
        triggertime = current_millis + reward_duration;
      }
      break;
    case AATCReward:
      if (current_millis > triggertime) {
        // stop reward, and start the next trial
        digitalWriteFast(REWARD, LOW);
        aatc_phase = AATCReady;
      }
      break;
    case AATCReset:
      releaseAATCOutputs();
      slm_stim_armed = false;
      slm_stim_fire = false;
      slm_fire_due = false;
      aatc_phase = AATCIdle;
      break;
  }

  state_variables[STATE_TONE] = aatc_tone + 1;
  state_variables[STATE_N_TONE_1] = tone_tot_count[0];
  state_variables[STATE_N_TONE_2] = tone_tot_count[1];
}




// generate the experiment object of the proper class, defined here
Experiment *makeExperiment() { return new SLM_AATCExperiment(); }