#include <Arduino.h>

// Copyright (c) 2015, 2016 Max Stadler, Patrick Dowling
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/*
*   TODO
* - expand to div/16 ? ... maybe
* - DAC mode should have additional/internal trigger sources: channels 1-3, 5, and 6; and presumably there could be additional DAC modes
* - BPM, 8th
* - trigger delay / configure latency (re: reset)
*
*/

#include "util/util_settings.h"
#include "TU_apps.h"
#include "TU_outputs.h"
#include "TU_menus.h"
#include "TU_strings.h"
#include "TU_visualfx.h"
#include "TU_pattern_edit.h"
#include "TU_patterns.h"
#include "extern/dspinst.h"

namespace menu = TU::menu;
/*
const uint8_t PULSEW_MAX = 255; // max pulse width [ms]
const uint16_t TOGGLE_THRESHOLD = 500; // ADC threshold for 0/1 parameters (500 = ~1.2V)

const uint32_t SCALE_PULSEWIDTH = 58982; // 0.9 for signed_multiply_32x16b
const uint32_t TICKS_TO_MS = 43691; // 0.6667f : fraction, if TU_CORE_TIMER_RATE = 60 us : 65536U * ((1000 / TU_CORE_TIMER_RATE) - 16)
const uint32_t TICK_JITTER = 0xFFFFFFF;  // 1/16 : threshold/double triggers reject -> ext_frequency_in_ticks_
const uint32_t TICK_SCALE  = 0xC0000000; // 0.75 for signed_multiply_32x32

uint32_t ticks_src1 = 0; // main clock frequency (top)
uint32_t ticks_src2 = 0; // sec. clock frequency (bottom)
*/
extern const uint32_t BPM_microseconds_4th[];
extern const uint64_t multipliers_[];

/*
enum ClockSeqChannelSetting {
  // shared
  CHANNEL_SETTING_MODE,
  CHANNEL_SETTING_CLOCK,
  CHANNEL_SETTING_RESET,
  CHANNEL_SETTING_MULT,
  CHANNEL_SETTING_PULSEWIDTH,
  CHANNEL_SETTING_INTERNAL_CLK,

  // cv sources
  CHANNEL_SETTING_MULT_CV_SOURCE,
  CHANNEL_SETTING_PULSEWIDTH_CV_SOURCE,
  CHANNEL_SETTING_CLOCK_CV_SOURCE,
  CHANNEL_SETTING_DUMMY,
  CHANNEL_SETTING_DUMMY_EMPTY,
  CHANNEL_SETTING_SCREENSAVER,
  CHANNEL_SETTING_LAST
};

enum ClockSeqChannelTriggerSource {
  CHANNEL_TRIGGER_TR1,
  CHANNEL_TRIGGER_TR2,
  CHANNEL_TRIGGER_NONE,
  CHANNEL_TRIGGER_INTERNAL,
  CHANNEL_TRIGGER_LAST,
  CHANNEL_TRIGGER_FREEZE_HIGH = CHANNEL_TRIGGER_INTERNAL,
  CHANNEL_TRIGGER_FREEZE_LOW = CHANNEL_TRIGGER_LAST
};

enum ClockSeqChannelCV_Mapping {
  CHANNEL_CV_MAPPING_CV1,
  CHANNEL_CV_MAPPING_CV2,
  CHANNEL_CV_MAPPING_CV3,
  CHANNEL_CV_MAPPING_CV4,
  CHANNEL_CV_MAPPING_LAST
};

enum ClockSeqCLOCKSTATES {
  OFF,
  ON = 4095
};

enum ClockSeqMENUPAGES {
  PARAMETERS,
  CV_SOURCES,
  TEMPO
};
*/
//uint64_t ext_frequency[CHANNEL_TRIGGER_LAST];

class ClockSeq_channel : public settings::SettingsBase<Clock_channel, CHANNEL_SETTING_LAST> {
public:

  uint8_t get_clock_source() const {
    return values_[CHANNEL_SETTING_CLOCK];
  }

  void set_clock_source(uint8_t _src) {
    apply_value(CHANNEL_SETTING_CLOCK, _src);
  }

  int8_t get_multiplier() const {
    return values_[CHANNEL_SETTING_MULT];
  }

  uint16_t get_pulsewidth() const {
    return values_[CHANNEL_SETTING_PULSEWIDTH];
  }

  uint16_t get_internal_timer() const {
    return values_[CHANNEL_SETTING_INTERNAL_CLK];
  }

  uint8_t get_reset_source() const {
    return values_[CHANNEL_SETTING_RESET];
  }

  void set_reset_source(uint8_t src) {
    apply_value(CHANNEL_SETTING_RESET, src);
  }

  uint8_t get_mult_cv_source() const {
    return values_[CHANNEL_SETTING_MULT_CV_SOURCE];
  }

  uint8_t get_pulsewidth_cv_source() const {
    return values_[CHANNEL_SETTING_PULSEWIDTH_CV_SOURCE];
  }

  uint8_t get_clock_source_cv_source() const {
    return values_[CHANNEL_SETTING_CLOCK_CV_SOURCE];
  }

  uint16_t get_clock_cnt() const {
    return clk_cnt_;
  }

  void update_internal_timer(uint16_t _tempo) {
      apply_value(CHANNEL_SETTING_INTERNAL_CLK, _tempo);
  }

  uint8_t getTriggerState() const {
    return clock_display_.getState();
  }

  int num_enabled_settings() const {
    return num_enabled_settings_;
  }

  // this seems superfluous now:
  void pattern_changed() {
    force_update_ = true;
  }

  void clear_CV_mapping() {

    apply_value(CHANNEL_SETTING_PULSEWIDTH_CV_SOURCE, 0);
    apply_value(CHANNEL_SETTING_MULT_CV_SOURCE,0);
    apply_value(CHANNEL_SETTING_CLOCK_CV_SOURCE,0);
  }

  void sync() {
    clk_cnt_ = 0x0;
    div_cnt_ = 0x0;
  }

  uint8_t get_page() const {
    return menu_page_;
  }

  void set_page(uint8_t _page) {
    menu_page_ = _page;
  }

  uint16_t get_zero(uint8_t channel) const {

    if (channel == CLOCK_CHANNEL_4)
      return _ZERO;

    return 0;
  }

  void Init(ChannelTriggerSource trigger_source) {

    InitDefaults();
    apply_value(CHANNEL_SETTING_CLOCK, trigger_source);

    force_update_ = true;
    gpio_state_ = OFF;
    ticks_ = 0;
    subticks_ = 0;
    tickjitter_ = 10000;
    clk_cnt_ = 0;
    clk_src_ = get_clock_source();
    reset_ = false;
    reset_counter_ = false;
    reset_me_ = false;
    menu_page_ = PARAMETERS;

    prev_multiplier_ = get_multiplier();
    prev_pulsewidth_ = get_pulsewidth();
    bpm_last_ = 0;

    ext_frequency_in_ticks_ = get_pulsewidth() << 15; // init to something...
    channel_frequency_in_ticks_ = get_pulsewidth() << 15;
    pulse_width_in_ticks_ = get_pulsewidth() << 10;

    _ZERO = TU::calibration_data.dac.calibrated_Zero[0x0][0x0];

    clock_display_.Init();
    update_enabled_settings(0);
  }

  void force_update() {
    force_update_ = true;
  }

  /* main channel update below: */

  inline void Update(uint32_t triggers, CLOCK_CHANNEL clockSeqChan) {

     // increment channel ticks ..
     subticks_++;

     int8_t _clock_source, _reset_source;
     int8_t _multiplier;
     bool _none, _triggered, _tock, _sync;
     uint16_t _output = gpio_state_;
     uint32_t prev_channel_frequency_in_ticks_ = 0x0;

     // core channel parameters --
     // 1. clock source:
     _clock_source = get_clock_source();

     if (get_clock_source_cv_source()){
        int16_t _toggle = TU::ADC::value(static_cast<ADC_CHANNEL>(get_clock_source_cv_source() - 1));

        if (_toggle > TOGGLE_THRESHOLD && _clock_source <= CHANNEL_TRIGGER_TR2)
          _clock_source = (~_clock_source) & 1u;
     }
     // 2. multiplication:
     _multiplier = get_multiplier();

     if (get_mult_cv_source()) {
        _multiplier += (TU::ADC::value(static_cast<ADC_CHANNEL>(get_mult_cv_source() - 1)) + 127) >> 8;
        CONSTRAIN(_multiplier, 0, MULT_MAX);
     }

     // clocked ?
     _none = CHANNEL_TRIGGER_NONE == _clock_source;
     _triggered = !_none && (triggers & DIGITAL_INPUT_MASK(_clock_source - CHANNEL_TRIGGER_TR1));
     _tock = false;
     _sync = false;

     // new tick frequency, external:
     if (_clock_source <= CHANNEL_TRIGGER_TR2) {

         if (_triggered || clk_src_ != _clock_source) {
            ext_frequency_in_ticks_ = ext_frequency[_clock_source];
            _tock = true;
            div_cnt_--;
         }
     }
     // or else, internal clock active?
     else if (_clock_source == CHANNEL_TRIGGER_INTERNAL) {

          ticks_++;
          _triggered = false;

          uint8_t _bpm = get_internal_timer() - BPM_MIN; // substract min value

          if (_bpm != bpm_last_ || clk_src_ != _clock_source) {
            // new BPM value, recalculate channel frequency below ...
            ext_frequency_in_ticks_ = BPM_microseconds_4th[_bpm];
            _tock = true;
          }
          // store current bpm value
          bpm_last_ = _bpm;

          // simulate clock ... ?
          if (ticks_ > ext_frequency_in_ticks_) {
            _triggered |= true;
            ticks_ = 0x0;
            div_cnt_--;
          }
     }
     // store clock source:
     clk_src_ = _clock_source;

     // new multiplier ?
     if (prev_multiplier_ != _multiplier)
       _tock |= true;
     prev_multiplier_ = _multiplier;

     // if so, recalculate channel frequency and corresponding jitter-thresholds:
     if (_tock) {

        // when multiplying, skip too closely spaced triggers:
        if (_multiplier > MULT_BY_ONE)
           prev_channel_frequency_in_ticks_ = multiply_u32xu32_rshift32(channel_frequency_in_ticks_, TICK_SCALE);
        // new frequency:
        channel_frequency_in_ticks_ = multiply_u32xu32_rshift32(ext_frequency_in_ticks_, multipliers_[_multiplier]) << 3;
        tickjitter_ = multiply_u32xu32_rshift32(channel_frequency_in_ticks_, TICK_JITTER);
     }
     // limit frequency to > 0
     if (!channel_frequency_in_ticks_)
        channel_frequency_in_ticks_ = 1u;

     // reset?
     _reset_source = get_reset_source();

     if (_reset_source < CHANNEL_TRIGGER_NONE && reset_me_) {

        uint8_t reset_state_ = !_reset_source ? digitalReadFast(TR1) : digitalReadFast(TR2);

        // ?
        if (reset_state_ < reset_) {
           div_cnt_ = 0x0;
           reset_counter_ = true; // reset clock counter below
           reset_me_ = false;
        }
        reset_ = reset_state_;
     }

    /*
     *  brute force ugly sync hack:
     *  this, presumably, is needlessly complicated.
     *  but seems to work ok-ish, w/o too much jitter and missing clocks...
     */
     uint32_t _subticks = subticks_;

     if (_multiplier <= MULT_BY_ONE && _triggered && div_cnt_ <= 0) {
        // division, so we track
        _sync = true;
        div_cnt_ = 8 - _multiplier; // /1 = 7 ; /2 = 6, /3 = 5 etc
        subticks_ = channel_frequency_in_ticks_; // force sync
     }
     else if (_multiplier <= MULT_BY_ONE && _triggered) {
        // division, mute output:
        TU::OUTPUTS::setState(clockSeqChan, OFF);
     }
     else if (_multiplier > MULT_BY_ONE && _triggered)  {
        // multiplication, force sync, if clocked:
        _sync = true;
        subticks_ = channel_frequency_in_ticks_;
     }
     else if (_multiplier > MULT_BY_ONE)
        _sync = true;
     // end of ugly hack

     // time to output ?
     if (subticks_ >= channel_frequency_in_ticks_ && _sync) {

         // if so, reset ticks:
         subticks_ = 0x0;
         // if tempo changed, reset _internal_ clock counter:
         if (_tock)
            ticks_ = 0x0;

         //reject, if clock is too jittery or skip quasi-double triggers when ext. frequency increases:
         if (_subticks < tickjitter_ || (_subticks < prev_channel_frequency_in_ticks_ && reset_me_))
            return;

         // only then count clocks:
         clk_cnt_++;

         // reset counter ? (SEQ/Euclidian)
         if (reset_counter_)
            clk_cnt_ = 0x0;

         // freeze ?
         if (_reset_source > CHANNEL_TRIGGER_NONE) {

             if (_reset_source == CHANNEL_TRIGGER_FREEZE_HIGH && !digitalReadFast(TR2))
              return;
             else if (_reset_source == CHANNEL_TRIGGER_FREEZE_LOW && digitalReadFast(TR2))
              return;
         }
         // clear for reset:
         reset_me_ = true;
         reset_counter_ = false;
         // finally, process trigger + output
         _output = gpio_state_ = ON; // = either ON, OFF, or anything (DAC)
         TU::OUTPUTS::setState(clockSeqChan, _output);
     }

     /*
      *  below: pulsewidth stuff
      */

     if (gpio_state_) {

        // pulsewidth setting --
        int16_t _pulsewidth = get_pulsewidth();

        if (_pulsewidth || _multiplier > MULT_BY_ONE) {

            bool _gates = false;

            // do we echo && multiply? if so, do half-duty cycle:
            if (!_pulsewidth)
                _pulsewidth = PULSEW_MAX;

            if (_pulsewidth == PULSEW_MAX)
              _gates = true;
            // CV?
            if (get_pulsewidth_cv_source()) {

              if (!_gates) {
                _pulsewidth += (TU::ADC::value(static_cast<ADC_CHANNEL>(get_pulsewidth_cv_source() - 1)) + 16) >> 4;
                CONSTRAIN(_pulsewidth, 1, PULSEW_MAX);
              }
              else {
                // CV for 50% duty cycle:
                _pulsewidth += (TU::ADC::value(static_cast<ADC_CHANNEL>(get_pulsewidth_cv_source() - 1)) + 8) >> 3;
                CONSTRAIN(_pulsewidth, 1, (PULSEW_MAX<<1) - 55);  // incl margin, max < 2x mult. see below
              }
            }
            // recalculate (in ticks), if new pulsewidth setting:
            if (prev_pulsewidth_ != _pulsewidth || ! subticks_) {
                if (!_gates) {
                  int32_t _fraction = signed_multiply_32x16b(TICKS_TO_MS, static_cast<int32_t>(_pulsewidth)); // = * 0.6667f
                  _fraction = signed_saturate_rshift(_fraction, 16, 0);
                  pulse_width_in_ticks_  = (_pulsewidth << 4) + _fraction;
                }
                else { // put out gates/half duty cycle:
                  pulse_width_in_ticks_ = channel_frequency_in_ticks_ >> 1;

                  if (_pulsewidth != PULSEW_MAX) { // CV?
                    pulse_width_in_ticks_ = signed_multiply_32x16b(static_cast<int32_t>(_pulsewidth) << 8, pulse_width_in_ticks_); //
                    pulse_width_in_ticks_ = signed_saturate_rshift(pulse_width_in_ticks_, 16, 0);
                  }
                }
            }
            prev_pulsewidth_ = _pulsewidth;

            // limit pulsewidth, if approaching half duty cycle:
            if (!_gates && pulse_width_in_ticks_ >= channel_frequency_in_ticks_>>1)
              pulse_width_in_ticks_ = (channel_frequency_in_ticks_ >> 1) | 1u;

            // turn off output?
            if (subticks_ >= pulse_width_in_ticks_)
              _output = gpio_state_ = OFF;
            else // keep on
              _output = ON;
         }
         else {
            // we simply echo the pulsewidth:
            bool _state = (_clock_source == CHANNEL_TRIGGER_TR1) ? !digitalReadFast(TR1) : !digitalReadFast(TR2);

            if (_state)
              _output = ON;
            else
              _output = gpio_state_ = OFF;
         }
     }
     // DAC channel needs extra treatment / zero offset:
     if (clockSeqChan == CLOCK_CHANNEL_4 && gpio_state_ == OFF)
       _output += _ZERO;

     // update (physical) outputs:
     TU::OUTPUTS::set(clockSeqChan, _output);
  } // end update

  /////////////////////////////////////////
  /* details re: trigger processing happens (mostly) here: */
  /////////////////////////////////////////

  inline uint16_t calc_average(const uint16_t *data, uint8_t depth) {
    uint32_t sum = 0;
    uint8_t n = depth;
    while (n--)
      sum += *data++;
    return sum / depth;
  }


  ChannelSetting enabled_setting_at(int index) const {
    return enabled_settings_[index];
  }

  void update_enabled_settings(uint8_t channel_id) {

    ChannelSetting *settings = enabled_settings_;

    if (menu_page_ != TEMPO)
      *settings++ = CHANNEL_SETTING_MODE;
    else
      *settings++ = CHANNEL_SETTING_INTERNAL_CLK;


    if (menu_page_ == CV_SOURCES) {

      *settings++ = CHANNEL_SETTING_PULSEWIDTH_CV_SOURCE;
      *settings++ = CHANNEL_SETTING_MULT_CV_SOURCE;

      *settings++ = CHANNEL_SETTING_CLOCK_CV_SOURCE;
      //if (mode == SEQ || mode == EUCLID)
      *settings++ = CHANNEL_SETTING_DUMMY; // make # items the same / no CV for reset source ...

    }

    else if (menu_page_ == PARAMETERS) {

        *settings++ = CHANNEL_SETTING_PULSEWIDTH;
        *settings++ = CHANNEL_SETTING_MULT;
        *settings++ = CHANNEL_SETTING_CLOCK;
        *settings++ = CHANNEL_SETTING_RESET;
    }
    else if (menu_page_ == TEMPO) {

      *settings++ = CHANNEL_SETTING_DUMMY_EMPTY;
      *settings++ = CHANNEL_SETTING_SCREENSAVER;
      *settings++ = CHANNEL_SETTING_DUMMY_EMPTY;
    }
    num_enabled_settings_ = settings - enabled_settings_;
  }

  void RenderScreensaver(weegfx::coord_t start_x, CLOCK_CHANNEL clockSeqChan) const;

private:
  uint16_t _sync_cnt;
  bool force_update_;
  uint16_t _ZERO;
  uint8_t clk_src_;
  bool reset_;
  bool reset_me_;
  bool reset_counter_;
  uint32_t ticks_;
  uint32_t subticks_;
  uint32_t tickjitter_;
  uint32_t clk_cnt_;
  int16_t div_cnt_;
  uint32_t ext_frequency_in_ticks_;
  uint32_t channel_frequency_in_ticks_;
  uint32_t pulse_width_in_ticks_;
  uint16_t gpio_state_;
  uint8_t prev_multiplier_;
  uint8_t prev_pulsewidth_;
  uint8_t display_sequence_;
  uint16_t display_mask_;
  uint8_t menu_page_;
  uint8_t bpm_last_;

  int num_enabled_settings_;
  ChannelSetting enabled_settings_[CHANNEL_SETTING_LAST];
  TU::DigitalInputDisplay clock_display_;

};

SETTINGS_DECLARE(ClockSeq_channel, CHANNEL_SETTING_LAST) {

  { 0, 0, MODES - 2, "mode", TU::Strings::mode, settings::STORAGE_TYPE_U4 },
  { 0, 0, MODES - 1, "mode", TU::Strings::mode, settings::STORAGE_TYPE_U4 },
  { CHANNEL_TRIGGER_TR1, 0, CHANNEL_TRIGGER_LAST, "clock src", TU::Strings::channel_trigger_sources, settings::STORAGE_TYPE_U4 },
  { CHANNEL_TRIGGER_NONE, 0, CHANNEL_TRIGGER_LAST, "reset/mute", TU::Strings::reset_trigger_sources, settings::STORAGE_TYPE_U4 },
  { MULT_BY_ONE, 0, MULT_MAX, "mult/div", multiplierStrings, settings::STORAGE_TYPE_U8 },
  { 25, 0, PULSEW_MAX, "pulsewidth", TU::Strings::pulsewidth_ms, settings::STORAGE_TYPE_U8 },
  { 100, BPM_MIN, BPM_MAX, "BPM:", NULL, settings::STORAGE_TYPE_U8 },

  { 0, 0, 1, "track -->", TU::Strings::logic_tracking, settings::STORAGE_TYPE_U4 },
  { 0, 0, 1, "track -->", TU::Strings::binary_tracking, settings::STORAGE_TYPE_U4 },
  { 0, 0, 255, "rnd hist.", NULL, settings::STORAGE_TYPE_U8 }, /// "history"
  { 65535, 1, 65535, "--> edit", NULL, settings::STORAGE_TYPE_U16 }, // seq 1
  { 65535, 1, 65535, "--> edit", NULL, settings::STORAGE_TYPE_U16 }, // seq 2
  { 65535, 1, 65535, "--> edit", NULL, settings::STORAGE_TYPE_U16 }, // seq 3
  { 65535, 1, 65535, "--> edit", NULL, settings::STORAGE_TYPE_U16 }, // seq 4
  { TU::Patterns::PATTERN_USER_0, 0, TU::Patterns::PATTERN_USER_LAST-1, "sequence #", TU::pattern_names_short, settings::STORAGE_TYPE_U8 },
  { TU::Patterns::kMax, TU::Patterns::kMin, TU::Patterns::kMax, "sequence length", NULL, settings::STORAGE_TYPE_U8 }, // seq 1
  { TU::Patterns::kMax, TU::Patterns::kMin, TU::Patterns::kMax, "sequence length", NULL, settings::STORAGE_TYPE_U8 }, // seq 2
  { TU::Patterns::kMax, TU::Patterns::kMin, TU::Patterns::kMax, "sequence length", NULL, settings::STORAGE_TYPE_U8 }, // seq 3
  { TU::Patterns::kMax, TU::Patterns::kMin, TU::Patterns::kMax, "sequence length", NULL, settings::STORAGE_TYPE_U8 }, // seq 4
  // cv sources
  { 0, 0, 4, "mult/div    >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "pulsewidth  >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "clock src   >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "sequence #  >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "mask        >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "rnd hist.   >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 4, "hist. depth >>", TU::Strings::cv_sources, settings::STORAGE_TYPE_U4 },
  { 0, 0, 0, "---------------------", NULL, settings::STORAGE_TYPE_U4 }, // DUMMY
  { 0, 0, 0, "  ", NULL, settings::STORAGE_TYPE_U4 }, // DUMMY empty
  { 0, 0, 0, "  ", NULL, settings::STORAGE_TYPE_U4 }  // screensaver
};


class ClockSeqState {
public:
  void Init() {
    selected_channel = 0;
    cursor.Init(CHANNEL_SETTING_MODE, CHANNEL_SETTING_LAST - 1);
    resetSequence();
    sequenceLength = 5;
  }

  inline bool editing() const {
    return cursor.editing();
  }

  inline int cursor_pos() const {
    return cursor.cursor_pos();
  }

  int selected_channel;
  menu::ScreenCursor<menu::kScreenLines> cursor;
  menu::ScreenCursor<menu::kScreenLines> cursor_state;

  uint8_t sequenceLength;
  uint8_t sequencePos;

  void advanceSequence()
  {
    sequencePos++;
    if (sequencePos > sequenceLength)
    {
      resetSequence();
    }
  }

  inline void resetSequence()
  {
    sequencePos = 0;
  }

  inline uint8_t sequencePosition() const {return sequencePos;}
};

ClockSeqState clockSeqState;
ClockSeq_channel clockSeqChan[NUM_CHANNELS];

void CLOCKSEQ_init() {

  TU::Patterns::Init();

  ext_frequency[CHANNEL_TRIGGER_TR1]  = 0xFFFFFFFF;
  ext_frequency[CHANNEL_TRIGGER_TR2]  = 0xFFFFFFFF;
  ext_frequency[CHANNEL_TRIGGER_NONE] = 0xFFFFFFFF;

  clockSeqState.Init();
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    clockSeqChan[i].Init(static_cast<ChannelTriggerSource>(CHANNEL_TRIGGER_TR1));
  }
  clockSeqState.cursor.AdjustEnd(clockSeqChan[0].num_enabled_settings() - 1);
}

size_t CLOCKSEQ_storageSize() {
  return NUM_CHANNELS * Clock_channel::storageSize();
}

size_t CLOCKSEQ_save(void *storage) {

  size_t used = 0;
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    used += clockSeqChan[i].Save(static_cast<char*>(storage) + used);
  }
  return used;
}
size_t CLOCKSEQ_restore(const void *storage) {
  size_t used = 0;
  for (size_t i = 0; i < NUM_CHANNELS; ++i) {
    used += clockSeqChan[i].Restore(static_cast<const char*>(storage) + used);
    clockSeqChan[i].update_enabled_settings(i);
  }
  clockSeqState.cursor.AdjustEnd(clockSeqChan[0].num_enabled_settings() - 1);
  return used;
}

void CLOCKSEQ_handleAppEvent(TU::AppEvent event) {
  switch (event) {
    case TU::APP_EVENT_RESUME:
        clockSeqState.cursor.set_editing(false);
    break;
    case TU::APP_EVENT_SUSPEND:
    case TU::APP_EVENT_SCREENSAVER_ON:
    case TU::APP_EVENT_SCREENSAVER_OFF:
    {
      ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];
        if (selected.get_page() > PARAMETERS) {
          selected.set_page(PARAMETERS);
          selected.update_enabled_settings(clockSeqState.selected_channel);
          clockSeqState.cursor.Init(CHANNEL_SETTING_MODE, 0);
          clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
        }
    }
    break;
  }
}

void CLOCKSEQ_loop() {
}

void CLOCKSEQ_isr() {

  ticks_src1++; // src #1 ticks
  ticks_src2++; // src #2 ticks

  uint32_t triggers = TU::DigitalInputs::clocked();

  // clocked? reset ; better use ARM_DWT_CYCCNT ?
  if (triggers == 1)  {
    ext_frequency[CHANNEL_TRIGGER_TR1] = ticks_src1;
    ticks_src1 = 0x0;
    clockSeqState.advanceSequence();
  }
  if (triggers == 2) {
    ext_frequency[CHANNEL_TRIGGER_TR2] = ticks_src2;
    ticks_src2 = 0x0;
    clockSeqState.resetSequence();
  }

  const uint8_t pos = clockSeqState.sequencePosition();
  // update channels:
  clockSeqChan[0].Update(pos == 0, CLOCK_CHANNEL_1);
  clockSeqChan[1].Update(pos == 1, CLOCK_CHANNEL_2);
  clockSeqChan[2].Update(pos == 2, CLOCK_CHANNEL_3);
  clockSeqChan[4].Update(pos == 4, CLOCK_CHANNEL_5);
  clockSeqChan[5].Update(pos == 5, CLOCK_CHANNEL_6);
  clockSeqChan[3].Update(pos == 3, CLOCK_CHANNEL_4); // = DAC channel; update last, because of the binary Sequencer thing.
}

void CLOCKSEQ_handleButtonEvent(const UI::Event &event) {

  if (UI::EVENT_BUTTON_LONG_PRESS == event.type) {
     switch (event.control) {
      case TU::CONTROL_BUTTON_UP:
         CLOCKS_upButtonLong();
        break;
      case TU::CONTROL_BUTTON_DOWN:
        CLOCKS_downButtonLong();
        break;
       case TU::CONTROL_BUTTON_L:
        CLOCKS_leftButtonLong();
        break;
      default:
        break;
     }
  }

  if (UI::EVENT_BUTTON_PRESS == event.type) {
    switch (event.control) {
      case TU::CONTROL_BUTTON_UP:
        CLOCKS_upButton();
        break;
      case TU::CONTROL_BUTTON_DOWN:
        CLOCKS_downButton();
        break;
      case TU::CONTROL_BUTTON_L:
        CLOCKS_leftButton();
        break;
      case TU::CONTROL_BUTTON_R:
        CLOCKS_rightButton();
        break;
    }
  }
}

void CLOCKSEQ_handleEncoderEvent(const UI::Event &event) {

  if (TU::CONTROL_ENCODER_L == event.control) {

    int selected_channel = clockSeqState.selected_channel + event.value;
    CONSTRAIN(selected_channel, 0, NUM_CHANNELS-1);
    clockSeqState.selected_channel = selected_channel;

    ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

    if (selected.get_page() == TEMPO || selected.get_page() == CV_SOURCES)
      selected.set_page(PARAMETERS);

    selected.update_enabled_settings(clockSeqState.selected_channel);
    clockSeqState.cursor.Init(CHANNEL_SETTING_MODE, 0);
    clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);

  } else if (TU::CONTROL_ENCODER_R == event.control) {

       ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

       if (selected.get_page() == TEMPO) {

         uint16_t int_clock_used_ = 0x0;
         for (int i = 0; i < NUM_CHANNELS; i++)
              int_clock_used_ += selected.get_clock_source() == CHANNEL_TRIGGER_INTERNAL ? 0x10 : 0x00;
         if (!int_clock_used_) {
          selected.set_page(PARAMETERS);
          clockSeqState.cursor = clockSeqState.cursor_state;
          selected.update_enabled_settings(clockSeqState.selected_channel);
          return;
         }
       }

       if (clockSeqState.editing()) {
          ChannelSetting setting = selected.enabled_setting_at(clockSeqState.cursor_pos());

           if (CHANNEL_SETTING_MASK1 != setting || CHANNEL_SETTING_MASK2 != setting || CHANNEL_SETTING_MASK3 != setting || CHANNEL_SETTING_MASK4 != setting) {
            if (selected.change_value(setting, event.value))
             selected.force_update();

            switch (setting) {
              case CHANNEL_SETTING_MODE:
              case CHANNEL_SETTING_MODE4:
              case CHANNEL_SETTING_DAC_MODE:
                 selected.update_enabled_settings(clockSeqState.selected_channel);
                 clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
              break;
             default:
              break;
            }
        }
      } else {
      clockSeqState.cursor.Scroll(event.value);
    }
  }
}

void CLOCKSEQ_upButton() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

  uint8_t _menu_page = selected.get_page();

  switch (_menu_page) {

    case TEMPO:
      selected.set_page(PARAMETERS);
      clockSeqState.cursor = clockSeqState.cursor_state;
      break;
    default:
      clockSeqState.cursor_state = clockSeqState.cursor;
      selected.set_page(TEMPO);
      clockSeqState.cursor.Init(CHANNEL_SETTING_MODE, 0);
      clockSeqState.cursor.toggle_editing();
     break;
  }
  selected.update_enabled_settings(clockSeqState.selected_channel);
  clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
}

void CLOCKSEQ_downButton() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

  uint8_t _menu_page = selected.get_page();

  switch (_menu_page) {

    case CV_SOURCES:
      // don't get stuck:
      if (selected.enabled_setting_at(clockSeqState.cursor_pos()) == CHANNEL_SETTING_MASK_CV_SOURCE)
        clockSeqState.cursor.set_editing(false);
      selected.set_page(PARAMETERS);
      break;
    case TEMPO:
      selected.set_page(CV_SOURCES);
      clockSeqState.cursor = clockSeqState.cursor_state;
    default:
      // don't get stuck:
      if (selected.enabled_setting_at(clockSeqState.cursor_pos()) == CHANNEL_SETTING_RESET)
        clockSeqState.cursor.set_editing(false);
      selected.set_page(CV_SOURCES);
     break;
  }
  selected.update_enabled_settings(clockSeqState.selected_channel);
  clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
}

void CLOCKSEQ_rightButton() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

  if (selected.get_page() == TEMPO) {
    selected.set_page(PARAMETERS);
    clockSeqState.cursor = clockSeqState.cursor_state;
    selected.update_enabled_settings(clockSeqState.selected_channel);
    clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
    return;
  }

  switch (selected.enabled_setting_at(clockSeqState.cursor_pos())) {

    case CHANNEL_SETTING_MASK1:
    case CHANNEL_SETTING_MASK2:
    case CHANNEL_SETTING_MASK3:
    case CHANNEL_SETTING_MASK4:
    break;
    case CHANNEL_SETTING_DUMMY:
    case CHANNEL_SETTING_DUMMY_EMPTY:
    break;
    default:
     clockSeqState.cursor.toggle_editing();
    break;
  }

}

void CLOCKSEQ_leftButton() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

  if (selected.get_page() == TEMPO) {
    selected.set_page(PARAMETERS);
    clockSeqState.cursor = clockSeqState.cursor_state;
    selected.update_enabled_settings(clockSeqState.selected_channel);
    clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
    return;
  }
  // sync:
  for (int i = 0; i < NUM_CHANNELS; ++i)
        clockSeqChan[i].sync();
}

void CLOCKSEQ_leftButtonLong() {

  for (int i = 0; i < NUM_CHANNELS; ++i)
        clockSeqChan[i].InitDefaults();

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];
  selected.set_page(PARAMETERS);
  selected.update_enabled_settings(clockSeqState.selected_channel);
  clockSeqState.cursor.set_editing(false);
  clockSeqState.cursor.AdjustEnd(selected.num_enabled_settings() - 1);
}

void CLOCKSEQ_upButtonLong() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];
  // set all channels to internal ?
  if (selected.get_page() == TEMPO) {
    for (int i = 0; i < NUM_CHANNELS; ++i)
        clockSeqChan[i].set_clock_source(CHANNEL_TRIGGER_INTERNAL);
    // and clear outputs:
    TU::OUTPUTS::set(CLOCK_CHANNEL_1, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_1, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_2, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_2, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_3, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_3, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_4, selected.get_zero(CLOCK_CHANNEL_4));
    TU::OUTPUTS::setState(CLOCK_CHANNEL_4, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_5, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_5, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_6, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_6, OFF);
  }
}

void CLOCKSEQ_downButtonLong() {

  ClockSeq_channel &selected = clockSeqChan[clockSeqState.selected_channel];

  if (selected.get_page() == CV_SOURCES)
    selected.clear_CV_mapping();
  else if (selected.get_page() == TEMPO)   {
    for (int i = 0; i < NUM_CHANNELS; ++i)
        clockSeqChan[i].set_clock_source(CHANNEL_TRIGGER_TR1);
    // and clear outputs:
    TU::OUTPUTS::set(CLOCK_CHANNEL_1, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_1, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_2, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_2, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_3, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_3, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_4, selected.get_zero(CLOCK_CHANNEL_4));
    TU::OUTPUTS::setState(CLOCK_CHANNEL_4, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_5, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_5, OFF);
    TU::OUTPUTS::set(CLOCK_CHANNEL_6, OFF);
    TU::OUTPUTS::setState(CLOCK_CHANNEL_6, OFF);
  }
}

void CLOCKSEQ_menu() {

  menu::SixTitleBar::Draw();
  uint16_t int_clock_used_ = 0x0;

  for (int i = 0, x = 0; i < NUM_CHANNELS; ++i, x += 21) {

    const ClockSeq_channel &channel = clockSeqChan[i];
    menu::SixTitleBar::SetColumn(i);
    graphics.print((char)('1' + i));
    graphics.movePrintPos(2, 0);
    //
    uint16_t internal_ = channel.get_clock_source() == CHANNEL_TRIGGER_INTERNAL ? 0x10 : 0x00;
    int_clock_used_ += internal_;
    menu::SixTitleBar::DrawGateIndicator(i, internal_);
  }

  const ClockSeq_channel &channel = clockSeqChan[clockSeqState.selected_channel];
  if (channel.get_page() != TEMPO)
    menu::SixTitleBar::Selected(clockSeqState.selected_channel);

  menu::SettingsList<menu::kScreenLines, 0, menu::kDefaultValueX> settings_list(clockSeqState.cursor);

  menu::SettingsListItem list_item;

   while (settings_list.available()) {
    const int setting =
        channel.enabled_setting_at(settings_list.Next(list_item));
    const int value = channel.get_value(setting);
    const settings::value_attr &attr = Clock_channel::value_attr(setting);

    switch (setting) {

      case CHANNEL_SETTING_MASK1:
      case CHANNEL_SETTING_MASK2:
      case CHANNEL_SETTING_MASK3:
      case CHANNEL_SETTING_MASK4:
        list_item.DrawNoValue<false>(value, attr);
        break;
      case CHANNEL_SETTING_DUMMY:
      case CHANNEL_SETTING_DUMMY_EMPTY:
        list_item.DrawNoValue<false>(value, attr);
        break;
      case CHANNEL_SETTING_SCREENSAVER:
        CLOCKS_screensaver();
        break;
      case CHANNEL_SETTING_INTERNAL_CLK:
        for (int i = 0; i < 6; i++)
          clockSeqChan[i].update_internal_timer(value);
        if (int_clock_used_)
          list_item.DrawDefault(value, attr);
        break;
      default:
        list_item.DrawDefault(value, attr);
    }
  }
}

void ClockSeq_channel::RenderScreensaver(weegfx::coord_t start_x, CLOCK_CHANNEL clockSeqChan) const {

  uint16_t _square, _frame;

  _square = TU::OUTPUTS::state(clockSeqChan);
  _frame  = TU::OUTPUTS::value(clockSeqChan);

  // DAC needs special treatment:
  if (clockSeqChan == CLOCK_CHANNEL_4) {
      _square = _square == ON ? 0x1 : 0x0;
      _frame  = _frame  == ON ? 0x1 : 0x0;
  }

  // draw little square thingies ..
  if (_square && _frame) {
    graphics.drawRect(start_x, 36, 10, 10);
    graphics.drawFrame(start_x-2, 34, 14, 14);
  }
  else if (_square)
    graphics.drawRect(start_x, 36, 10, 10);
  else
   graphics.drawRect(start_x+3, 39, 4, 4);
}

void CLOCKSEQ_screensaver() {

  clockSeqChan[0].RenderScreensaver(4,  CLOCK_CHANNEL_1);
  clockSeqChan[1].RenderScreensaver(26, CLOCK_CHANNEL_2);
  clockSeqChan[2].RenderScreensaver(48, CLOCK_CHANNEL_3);
  clockSeqChan[3].RenderScreensaver(70, CLOCK_CHANNEL_4);
  clockSeqChan[4].RenderScreensaver(92, CLOCK_CHANNEL_5);
  clockSeqChan[5].RenderScreensaver(114,CLOCK_CHANNEL_6);
}
