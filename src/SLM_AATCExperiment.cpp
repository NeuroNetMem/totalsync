//
// Created by Francesco Battaglia on 07/09/2026.
//


#include <PacketSerial.h>
#include "pins_slm.h"

void SLM_AATCExperiment::setup() {
    ;
}

void SLM_AATCExperiment::loopMicro() {
    ;
}

// One tick of the SLM stimulation state machine, called from gather() every
// millisecond and gated on the slm_experiment flag.
//
// Once armed, the selected stimulus index is clocked out on SLM_STIM_SELECT
// (reset pulse + 8 bits, MSB first, one bit per tick). Once that byte is fully
// on the wire and the line is back LOW, a settling pause of slm_stim_waittime
// ms runs, giving the SLM time to load the pattern; only then does the train
// start. It is slm_stim_n_triggers pulses on SLM_STIM_TRIGGER, one started on
// each consecutive falling edge of the scanner frame clock and held for
// slm_stim_duration ms - short enough to end well inside its own frame, so a
// pulse never spans the edge that starts the next one. The stimulus disarms as
// the last pulse of the train begins.
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

void SLM_AATCExperiment::loopMilliPre() {
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
      slm_stim_pulses_left = 0;
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
        // slmSelDone is only reachable from here, so loading the train counter
        // on the transition guarantees every train starts from a full count.
        slm_stim_pulses_left = slm_stim_n_triggers;
        slm_select_phase = slmSelDone;
      }
      break;

    case slmSelDone:
      if (!slm_stim_armed) {
        // Arming was withdrawn (e.g. by reset()) while the byte was going out
        // or part way through the train; drop the pulses still owed.
        slm_stim_pulses_left = 0;
        slm_select_phase = slmSelIdle;
      } else if (slm_frame_clock_prev == HIGH && slm_frame_clock == LOW) {
        // One pulse per falling edge until the train is spent. The release
        // below ends each pulse after slm_stim_duration ms, which the
        // static_assert above plus a frame period longer than that keeps
        // strictly inside the current frame.
        digitalWriteFast(SLM_STIM_TRIGGER, HIGH);
        slm_stim_end_millis = current_millis + slm_stim_duration;
        slm_stim_active = true;
        if (--slm_stim_pulses_left == 0) {
          slm_stim_armed = false;
          slm_select_phase = slmSelIdle;
        }
        // Otherwise stay in slmSelDone, armed, waiting for the next edge. Arming
        // is held for the whole train, so SLM_DEBUG_OUT reads high across it and
        // a re-arm while a train is running is absorbed rather than restarting
        // the select transmission mid-train.
      }
      break;
  }
  slm_frame_clock_prev = slm_frame_clock;

  if (slm_stim_active && current_millis >= slm_stim_end_millis) {
    digitalWriteFast(SLM_STIM_TRIGGER, LOW);
    slm_stim_active = false;
  }

  // the AATC logic
  if (AATC_trigger == 1) {
    AATC();
  } else {
    press_test = 0;
    triggertime = 1000;
  }
}

void SLM_AATCExperiment::loopMilliPost() {
#ifdef SLM_DEBUG
  AATC_trigger = true;
#else
  AATC_trigger = digitalReadFast(TRIGGER_AATC);
#endif
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
  slm_stim_active = false;
  slm_stim_end_millis = 0;
  slm_stim_pulses_left = 0;
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
