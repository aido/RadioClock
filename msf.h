//
//  www.blinkenlight.net
//
//  Copyright 2015 Udo Klein
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see http://www.gnu.org/licenses/

#ifndef msf_h
#define msf_h

#include "radioclock.h"

namespace MSF_Clock {
    typedef struct MSF_Clock_time_t : RadioClock_Clock::time_t {
        bool uses_summertime;
        bool timezone_change_scheduled;
    } time_t;

    // Once the clock has locked to the MSF signal
    // and has decoded a reliable time signal it will
    // call output handler once per second
    typedef void (*output_handler_t)(const time_t &decoded_time);

    void setup();
    void setup(const RadioClock_Clock::input_provider_t input_provider, const output_handler_t output_handler);

    void set_input_provider(const RadioClock_Clock::input_provider_t);
    void set_output_handler(const output_handler_t output_handler);

    // blocking till start of next second
    void get_current_time(time_t &now);
    // non-blocking, reads current second
    void read_current_time(time_t &now);
    // non-blocking, reads current second+1
    void read_future_time(time_t &now_plus_1s);

    void auto_persist();  // this is slow and messes with the interrupt flag, do not call during interrupt handling

    void print(time_t time);

    void debug();

    // determine quality of the MSF signal lock
    uint8_t get_overall_quality_factor();

    // determine the short term signal quality
    // 0xff = not available
    // 0..25 = extraordinary poor
    // 25.5 would be the expected value for 100% noise
    // 26 = very poor
    // 50 = best possible, every signal bit matches with the local clock
    uint8_t get_prediction_match();
}

namespace MSF {
    typedef enum {
        min_marker = 5,
        undefined  = 4,
        A1_B1      = 3,
        A1_B0      = 2,
        A0_B1      = 1,
        A0_B0      = 0
    } tick_t;

    // https://en.wikipedia.org/wiki/Time_from_NPL#Protocol	
    typedef struct {
        RadioClock::serialized_clock_stream A;
        RadioClock::serialized_clock_stream B;
    } serialized_clock_stream;		

    typedef struct MSF_time_data_t : RadioClock::time_data_t {
        bool uses_summertime                : 1;  // false -> wintertime, true, summertime
        bool timezone_change_scheduled      : 1;

        bool undefined_year_output          : 1;
        bool undefined_day_output           : 1;
        bool undefined_weekday_output       : 1;
        bool undefined_time_output          : 1;
        bool undefined_timezone_change_scheduled_output     : 1;
        bool undefined_uses_summertime_output     : 1;
    } time_data_t;

    typedef void (*output_handler_t)(const MSF::time_data_t &decoded_time);
}

namespace MSF_1_Khz_Generator {
    void isr_handler();
}

namespace MSF_Frequency_Control {
    void process_1_Hz_tick(const MSF::time_data_t &decoded_time);
}

namespace MSF_Encoder {
    // What *** exactly *** is the semantics of the "Encoder"?
    // It only *** encodes *** whatever time is set
    // It does never attempt to verify the data

    void reset(MSF::time_data_t &now);

    void get_serialized_clock_stream(const MSF::time_data_t &now, MSF::serialized_clock_stream &data);

    uint8_t weekday(const MSF::time_data_t &now);  // sunday == 0
    BCD::bcd_t bcd_weekday(const MSF::time_data_t &now);  // sunday == 7

    MSF::tick_t get_current_signal(const MSF::time_data_t &now);

    // This will advance the second. It will consider the control
    // bits while doing so. It will NOT try to properly set the
    // control bits. If this is desired "autoset" must be called in
    // advance.
    void advance_second(MSF::time_data_t &now);

    // The same but for the minute
    void advance_minute(MSF::time_data_t &now);

    // This will set the weekday by evaluating the date.
    void autoset_weekday(MSF::time_data_t &now);

    // This will set the control bits
    // It will generate the control bits exactly like MSF would.
    // Look at the summer / wintertime transistions
    // to understand the subtle implications.
    void autoset_control_bits(MSF::time_data_t &now);


    void debug(const MSF::time_data_t &clock);
    void debug(const MSF::time_data_t &clock, const uint16_t cycles);
}

namespace MSF_Naive_Bitstream_Decoder {
    void set_bit(const uint8_t second, const uint8_t value, MSF::time_data_t &now);
}

namespace MSF_Flag_Decoder {
    void setup();
    void process_tick(const uint8_t current_second, const bool tick_value);

    void reset_after_previous_hour();
    void reset_before_new_day();


    bool get_uses_summertime();
    bool get_timezone_change_scheduled();

    void debug();
}

namespace MSF_Decade_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Year_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Month_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Day_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);	
}

namespace MSF_Weekday_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Hour_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Minute_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value);
}

namespace MSF_Second_Decoder {
    void set_convolution_time(const MSF::time_data_t &now);
    uint8_t get_second();
    void process_single_tick_data(const MSF::tick_t tick_data);
    uint8_t get_prediction_match();

    void debug();
}

namespace MSF_Local_Clock {
    // Convenient for flashing second signal on clock
    extern volatile bool second_toggle;

    void setup();
    void process_1_Hz_tick(const MSF::time_data_t &decoded_time);
    void process_1_kHz_tick();
    void debug();

    // blocking till start of next second
    void get_current_time(MSF::time_data_t &now);

    // non-blocking, reads current second
    void read_current_time(MSF::time_data_t &now);
}

namespace MSF_Clock_Controller {
    void process_1_kHz_tick_data(const bool sampled_data);

    void process_single_tick_data(const MSF::tick_t tick_data);

    void flush(const MSF::time_data_t &decoded_time);
    void set_output_handler(const MSF_Clock::output_handler_t output_handler);

    typedef struct MSF_Clock_Controller_clock_quality_t : RadioClock_Controller::clock_quality_t {
        uint8_t uses_summertime_quality;
        uint8_t timezone_change_scheduled_quality;
    } clock_quality_t;

    void get_quality(clock_quality_t &clock_quality);

    uint8_t get_overall_quality_factor();
    uint8_t get_prediction_match();

    // blocking, will unblock at the start of the second
    void get_current_time(MSF::time_data_t &now);

    // non-blocking reads current second
    void read_current_time(MSF::time_data_t &now);

    void debug();
}

namespace MSF_Demodulator {
    void detector(const bool sampled_data);

    // RadioClock_Demodulator::phase_detection the filter kernel is suitable for both DCF77 and MSF.
    //
    // However RadioClock_Demodulator::phase_detection() is not optimal for MSF but definitely more
    // than good enough.
    //
    // On the other hand if you do not mind the CPU utilization a proper kernel MSF_Demodulator::phase_detection()
    // below can be used instead.


    void phase_detection();
}
#endif