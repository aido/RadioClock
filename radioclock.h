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

#define GCC_VERSION (__GNUC__ * 10000 \
    + __GNUC_MINOR__ * 100 \
    + __GNUC_PATCHLEVEL__)

#define ERROR_MESSAGE(major, minor, patchlevel) compiler_version__GCC_ ## major ## _ ## minor ## _ ## patchlevel ## __ ;
#define OUTDATED_COMPILER_ERROR(major, minor, patchlevel) ERROR_MESSAGE(major, minor, patchlevel)

#if GCC_VERSION < 40503
// Arduino 1.0.0 - 1.0.6 come with an outdated version of avr-gcc.
// Arduino 1.5.8 comes with a ***much*** better avr-gcc. The library
// will compile but fail to execute properly if compiled with an
// outdated avr-gcc. So here we stop here if the compiler is outdated.
//
// You may find out your compiler version by executing 'avr-gcc --version'

// Visit the compatibility section here:
//     http://blog.blinkenlight.net/experiments/dcf77/dcf77-library/
// for more details.
#error Outdated compiler version < 4.5.3
#error Absolute minimum recommended version is avr-gcc 4.5.3.
#error Use 'avr-gcc --version' from the command line to verify your compiler version.
#error Arduino 1.0.0 - 1.0.6 ship with outdated compilers.
#error Arduino 1.5.8 (avr-gcc 4.8.1) and above are recommended.

OUTDATED_COMPILER_ERROR(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#endif

#ifndef radioclock_h
#define radioclock_h

#include <stdint.h>
#include <avr/eeprom.h>
#include <Arduino.h>

namespace BCD {
    typedef union {
        struct {
            uint8_t lo:4;
            uint8_t hi:4;
        } digit;

        struct {
            uint8_t b0:1;
            uint8_t b1:1;
            uint8_t b2:1;
            uint8_t b3:1;
            uint8_t b4:1;
            uint8_t b5:1;
            uint8_t b6:1;
            uint8_t b7:1;
        } bit;

        uint8_t val;
    } bcd_t;

    bool operator == (const bcd_t a, const bcd_t b);
    bool operator != (const bcd_t a, const bcd_t b);
    bool operator >= (const bcd_t a, const bcd_t b);
    bool operator <= (const bcd_t a, const bcd_t b);
    bool operator >  (const bcd_t a, const bcd_t b);
    bool operator <  (const bcd_t a, const bcd_t b);

    void print(const bcd_t value);
    void increment(bcd_t &value);

    bcd_t int_to_bcd(const uint8_t value);
    uint8_t bcd_to_int(const bcd_t value);
}

namespace Arithmetic_Tools {
    template <uint8_t N> inline void bounded_increment(uint8_t &value) __attribute__((always_inline));
    template <uint8_t N>
    void bounded_increment(uint8_t &value) {
        if (value >= 255 - N) { value = 255; } else { value += N; }
    }

    template <uint8_t N> inline void bounded_decrement(uint8_t &value) __attribute__((always_inline));
    template <uint8_t N>
    void bounded_decrement(uint8_t &value) {
        if (value <= N) { value = 0; } else { value -= N; }
    };

    inline void bounded_add(uint8_t &value, const uint8_t amount) __attribute__((always_inline));
    inline void bounded_add(uint8_t &value, const uint8_t amount) {
        if (value >= 255-amount) { value = 255; } else { value += amount; }
    } ;

    inline void bounded_sub(uint8_t &value, const uint8_t amount) __attribute__((always_inline));
    inline void bounded_sub(uint8_t &value, const uint8_t amount) {
        if (value <= amount) { value = 0; } else { value -= amount; }
    } ;

    inline uint8_t bit_count(const uint8_t value) __attribute__((always_inline));
    inline uint8_t bit_count(const uint8_t value) {
        const uint8_t tmp1 = (value & 0b01010101) + ((value>>1) & 0b01010101);
        const uint8_t tmp2 = (tmp1  & 0b00110011) + ((tmp1>>2) & 0b00110011);
        return (tmp2 & 0x0f) + (tmp2>>4);
    } ;

    inline uint8_t parity(const uint8_t value) __attribute__((always_inline));
    inline uint8_t parity(const uint8_t value) {
        uint8_t tmp = value;

        tmp = (tmp & 0xf) ^ (tmp >> 4);
        tmp = (tmp & 0x3) ^ (tmp >> 2);
        tmp = (tmp & 0x1) ^ (tmp >> 1);

        return tmp;
    } ;

    inline uint8_t reverse(const uint8_t value) __attribute__((always_inline));
    inline uint8_t reverse(const uint8_t value) {
        uint8_t tmp = value;

        tmp = (tmp & 0xf0) >> 4 | (tmp & 0x0f) << 4;
        tmp = (tmp & 0xcc) >> 2 | (tmp & 0x33) << 2;
        tmp = (tmp & 0xaa) >> 1 | (tmp & 0x55) << 1;

        return tmp;
    } ;

    void minimize(uint8_t &minimum, const uint8_t value);

    void maximize(uint8_t &maximum, const uint8_t value);

    uint8_t set_bit(const uint8_t data, const uint8_t number, const uint8_t value);
}

namespace Hamming {
    typedef struct {
        uint8_t lock_max;
        uint8_t noise_max;
    } lock_quality_t;

    template <uint8_t significant_bits>
    void score (uint8_t &bin, const BCD::bcd_t input, const BCD::bcd_t candidate) {
        using namespace Arithmetic_Tools;

        const uint8_t the_score = significant_bits - bit_count(input.val ^ candidate.val);
        bounded_add(bin, the_score);
    }

    template <typename bins_t>
    void advance_tick(bins_t &bins) {
        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);
        if (bins.tick < number_of_bins - 1) {
            ++bins.tick;
        } else {
            bins.tick = 0;
        }
    }

    template <typename bins_type, uint8_t significant_bits, bool with_parity>
    void hamming_binning(bins_type &bins, const BCD::bcd_t input) {
        using namespace Arithmetic_Tools;
        using namespace BCD;

        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);

        if (bins.max > 255-significant_bits) {
            // If we know we can not raise the maximum any further we
            // will lower the noise floor instead.
            for (uint8_t bin_index = 0; bin_index <number_of_bins; ++bin_index) {
                bounded_decrement<significant_bits>(bins.data[bin_index]);
            }
            bins.max -= significant_bits;
            bounded_decrement<significant_bits>(bins.noise_max);
        }

        const uint8_t offset = number_of_bins-1-bins.tick;
        uint8_t bin_index = offset;
        // for minutes, hours have parity and start counting at 0
        // for days, weeks, month we have no parity and start counting at 1
        // for years and decades we have no parity and start counting at 0
        bcd_t candidate;
        candidate.val = (with_parity || number_of_bins == 10)? 0x00: 0x01;
        for (uint8_t pass=0; pass < number_of_bins; ++pass) {

            if (with_parity) {
                candidate.bit.b7 = parity(candidate.val);
                score<significant_bits>(bins.data[bin_index], input, candidate);
                candidate.bit.b7 = 0;
            } else {
                score<significant_bits>(bins.data[bin_index], input, candidate);
            }

            bin_index = bin_index < number_of_bins-1? bin_index+1: 0;
            increment(candidate);
        }
    }

    template <typename bins_t>
    void compute_max_index(bins_t &bins) {
        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);

        bins.noise_max = 0;
        bins.max = 0;
        bins.max_index = 255;
        for (uint8_t index = 0; index < number_of_bins; ++index) {
            const uint8_t bin_data = bins.data[index];

            if (bin_data >= bins.max) {
                bins.noise_max = bins.max;
                bins.max = bin_data;
                bins.max_index = index;
            } else if (bin_data > bins.noise_max) {
                bins.noise_max = bin_data;
            }
        }
    }

    template <typename bins_t>
    void setup(bins_t &bins) {
        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);

        for (uint8_t index = 0; index < number_of_bins; ++index) {
            bins.data[index] = 0;
        }
        bins.tick = 0;

        bins.max = 0;
        bins.max_index = 255;
        bins.noise_max = 0;
    }

    template <typename bins_t>
    BCD::bcd_t get_time_value(const bins_t &bins) {
        // there is a trade off involved here:
        //    low threshold --> lock will be detected earlier
        //    low threshold --> if lock is not clean output will be garbled
        //    a proper lock will fix the issue
        //    the question is: which start up behaviour do we prefer?
        const uint8_t threshold = 2;

        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);
        const uint8_t offset = (number_of_bins == 60 || number_of_bins == 24 || number_of_bins == 10)? 0x00: 0x01;

        if (bins.max-bins.noise_max >= threshold) {
            return BCD::int_to_bcd((bins.max_index + bins.tick + 1) % number_of_bins + offset);
        } else {
            BCD::bcd_t undefined;
            undefined.val = 0xff;
            return undefined;
        }
    }

    template <typename bins_t>
    void get_quality(const bins_t bins, Hamming::lock_quality_t &lock_quality) {
        const uint8_t prev_SREG = SREG;
        cli();
        lock_quality.lock_max = bins.max;
        lock_quality.noise_max = bins.noise_max;
        SREG = prev_SREG;
    }

    template <typename bins_t>
    uint8_t get_quality_factor(const bins_t bins) {
        const uint8_t prev_SREG = SREG;
        cli();

        uint8_t quality_factor;
        if (bins.max <= bins.noise_max) {
            quality_factor = 0;
        } else {
            const uint16_t delta = bins.max - bins.noise_max;
            // we define the quality factor as
            //   (delta) / ld (max + 3)

            // unfortunately this is prohibitive expensive to compute

            // --> we need some shortcuts
            // --> we will cheat a lot

            // lookup for ld(n):
            //   4 -->  2,  6 -->  2.5,   8 -->  3,  12 -->  3.5
            // above 16 --> only count the position of the leading digit

            if (bins.max >= 32-3) {
                // delta / ld(bins.max+3) ~ delta / ld(bins.max)
                uint16_t max = bins.max;
                uint8_t log2 = 0;
                while (max > 0) {
                    max >>= 1;
                    ++log2;
                }
                log2 -= 1;
                // now 15 >= log2 >= 5
                // multiply by 256/log2 and divide by 256
                const uint16_t multiplier =
                log2 > 12? log2 > 13? log2 > 14? 256/15
                : 256/14
                : 256/13
                : log2 > 8 ? log2 > 10? log2 > 11? 256/12
                : 256/11
                : log2 >  9? 256/10
                : 256/ 9
                : log2 >  6? log2 >  7? 256/ 8
                : 256/ 7
                : log2 >  5? 256/ 6
                : 256/ 5;
                quality_factor = ((uint16_t)delta * multiplier) >> 8;

            } else if (bins.max >= 16-3) {
                // delta / 4
                quality_factor = delta >> 2;


            } else if (bins.max >= 12-3) {
                // delta / 3.5
                // we know delta <= max < 16-3 = 13 --> delta <= 12
                quality_factor = delta >= 11? 3:
                delta >=  7? 2:
                delta >=  4? 1:
                0;

            } else if (bins.max >= 8-3) {
                // delta / 3
                // we know delta <= max < 12-3 = 9 --> delta <= 8
                quality_factor = delta >= 6? 2:
                delta >= 3? 1:
                0;

            } else if (bins.max >= 6-3) {
                // delta / 2.5
                // we know delta <= max < 8-3 = 5 --> delta <= 4
                quality_factor = delta >= 3? 1: 0;

            } else {  // if (bins.max >= 4-3) {
                // delta / 2
                quality_factor = delta >> 1;
            }
        }
        SREG = prev_SREG;
        return quality_factor;
    }

    template <typename bins_t>
    void debug (const bins_t &bins) {
        const uint8_t number_of_bins = sizeof(bins.data) / sizeof(bins.data[0]);
        const bool uses_integrals = sizeof(bins.max) == 4;

        Serial.print(get_time_value(bins).val, HEX);
        Serial.print(F(" Tick: "));
        Serial.print(bins.tick);
        Serial.print(F(" Quality: "));
        Serial.print(bins.max, DEC);
        Serial.print('-');
        Serial.print(bins.noise_max, DEC);
        Serial.print(F(" Max Index: "));
        Serial.print(bins.max_index, DEC);
        Serial.print(F(" Quality Factor: "));
        Serial.println(get_quality_factor(bins), DEC);
        Serial.print('>');

        for (uint8_t index = 0; index < number_of_bins; ++index) {
            Serial.print(
            (index == bins.max_index                                          ||
            (!uses_integrals && index == (bins.max_index+1) % number_of_bins) ||
            (uses_integrals && (index == (bins.max_index+10) % number_of_bins || (index == (bins.max_index+20) % number_of_bins))))
            ? '|': ',');
            Serial.print(bins.data[index], HEX);
        }
        Serial.println();
    }
}

namespace Debug {
    void debug_helper(char data);
    void bcddigit(uint8_t data);
    void bcddigits(uint8_t data);
}

namespace RadioClock {
    typedef struct {
        BCD::bcd_t year;     // 0..99
        BCD::bcd_t month;    // 1..12
        BCD::bcd_t day;      // 1..31
        BCD::bcd_t weekday;  // Mo = 1, So = 7
        BCD::bcd_t hour;     // 0..23
        BCD::bcd_t minute;   // 0..59
    } time_info_t;

    typedef struct _time_data_t : time_info_t {
        uint8_t second;      // 0..60
    } time_data_t;

    // DCF77
    // =====
    // https://en.wikipedia.org/wiki/DCF77#Time_code_interpretation
    //
    // byte_0:	bit 16-20  // flags
    // byte_1:	bit 21-28  // minutes
    // byte_2:	bit 29-36  // hours, bit 0 of day
    // byte_3:	bit 37-44  // day + weekday
    // byte_4:	bit 45-52  // month + bit 0-2 of year
    // byte_5:	bit 52-58  // year + parity

    // MSF
    // ===
    // https://en.wikipedia.org/wiki/Time_from_NPL#Protocol	
    //
    // byte_0A:	bit 17-24	// year
    // byte_1A:	bit 25-32	// month + bit 0-2 day
    // byte_2A:	bit 33-40	// bit 3-5 day + weekday + bit 0-1 hour
    // byte_3A:	bit 41-48	// bit 2-5 hour + bit 0-3 minute
    // byte_4A:	bit 49-56	// bit 4-6 minute + 01111
    // byte_5A:	bit 57-59	// 110
    //
    // byte_0B:	bit 17-24	// 00000000
    // byte_1B:	bit 25-32	// 00000000
    // byte_2B:	bit 33-40	// 00000000
    // byte_3B:	bit 41-48	// 00000000
    // byte_4B:	bit 49-56	// 0000 + flags + parity
    // byte_5B:	bit 57-59	// flags + parity + 0

    typedef struct {
        uint8_t byte_0;
        uint8_t byte_1;
        uint8_t byte_2;
        uint8_t byte_3;
        uint8_t byte_4;
        uint8_t byte_5;
    } serialized_clock_stream;

    typedef enum {
        useless  = 0,  // waiting for good enough signal
        dirty    = 1,  // time data available but unreliable
        free     = 2,  // clock was once synced but now may deviate more than 200 ms, must not re-lock if valid phase is detected
        unlocked = 3,  // lock was once synced, inaccuracy below 200 ms, may re-lock if a valid phase is detected
        locked   = 4,  // clock driven by accurate phase, time is accurate but not all decoder stages have sufficient quality for sync
        synced   = 5   // best possible quality, clock is 100% synced
    } clock_state_t;
}

namespace RadioClock_Clock {
    typedef struct _time_t : RadioClock::time_info_t {
        BCD::bcd_t second;   // 0..60
    } time_t;

    // input provider will be called each millisecond and must
    // provide the input of the raw radio clock signal
    typedef bool (*input_provider_t)(void);

    // determine the internal clock state
    uint8_t get_clock_state();
}

namespace RadioClock_Demodulator {
    const uint8_t bin_count = 100;

    const uint16_t samples_per_second = 1000;
    const uint16_t samples_per_bin = samples_per_second / RadioClock_Demodulator::bin_count;
    const uint16_t bins_per_10ms  = RadioClock_Demodulator::bin_count / 100;
    const uint16_t bins_per_50ms  =  5 * bins_per_10ms;
    const uint16_t bins_per_100ms = 10 * bins_per_10ms;
    const uint16_t bins_per_200ms = 20 * bins_per_10ms;
    const uint16_t bins_per_300ms = 30 * bins_per_10ms;
    const uint16_t bins_per_500ms = 50 * bins_per_10ms;

    typedef struct {
        uint16_t data[bin_count];
        uint8_t tick;

        uint32_t noise_max;
        uint32_t max;
        uint8_t max_index;
    } phase_bins;

    extern phase_bins bins;

    void setup();

    // According to Wikipedia https://en.wikipedia.org/wiki/Time_from_NPL#Shortcomings_of_the_current_signal_format
    // the signal will show 100 ms without carrier ("1"), then 100 ms for bit A and 100 ms for bit B
    // no carrier translates to "1", carrier translates to "0". The signal will end with 700 ms of carrier "0".
    // The only exception is the first minute which has 500 ms of no carrier and then 500 ms with carrier.
    // This implies: 59x"1" and  1x"0" for the first 100ms
    // Then           7x"1" and 18x"0" for the second 100 ms as well as 35 data bits
    // Finally        1x"1" and 37x"0" for the third  100 ms as well as 22 data bits
    //
    // This gives a rough estimate of
    // 59x"1" and  1x"0" for the first 100 ms
    // 34x"1" and 36x"0" for the second 100 ms (notice that "1" is slightly less likely to appear
    //  8x"1" and 42x"0" notice that the dut bits have only 25% of probability for a 1 as 50% must always be zero --> count 12x"0" and 4x"1"
    //
    // As a consequence the filter kernel is suitable for both DCF77 and MSF
    // However RadioClock_Demodulator::phase_detection() is not optimal for MSF but definitely more
    // than good enough.
    //
    // On the other hand if you do not mind the CPU utilization a proper kernel MSF_Demodulator::phase_detection()
    // can be used for MSF instead.
    void phase_detection();
    uint8_t phase_binning(const uint8_t input);

    void set_has_tuned_clock();
    uint16_t wrap(const uint16_t);
    void get_quality(uint32_t &lock_max, uint32_t &noise_max);
    uint8_t get_quality_factor();

    void debug();
    // attention: debug_verbose is not really thread save
    //            thus the output may contain unexpected artifacts
    //            do not rely on the output of one debug cycle
    void debug_verbose();
}

namespace RadioClock_1_Khz_Generator {
    void setup(const RadioClock_Clock::input_provider_t input_provider);
    bool zero_provider();

    extern RadioClock_Clock::input_provider_t the_input_provider;
    static int16_t adjust_pp16m = 0;
    static int32_t cumulated_phase_deviation = 0;

    // positive_value --> increase frequency
    // pp16m = parts per 16 million = 1 Hz @ 16 Mhz
    void adjust(const int16_t pp16m);
    int16_t read_adjustment();
    void isr_handler();
}

namespace RadioClock_Frequency_Control {
    extern volatile uint16_t elapsed_minutes;
    // Precision at tau min is 8 Hz == 0.5 ppm or better
    // This is because 334 m = 334 * 60 * 100 centiseconds = 2004000 centiseconds
    // Do not decrease this value!
    const uint16_t tau_min_minutes   = 334;

    // Precision at tau_max would be 0.5 Hz
    // This may be decreased if desired. Do not decrease below 2*tau_min.
    const uint16_t tau_max_minutes = 5334; // 5334 * 6000 = 32004000

    // 1600 Hz = 100 ppm
    // Theoretically higher values would be possible.
    // However if a tuning beyond 100 ppm is necessary then there is something
    // fundamentally wrong with the oscillator.
    const int16_t max_total_adjust = 1600;

    // indicator if data may be persisted to EEPROM
    extern volatile boolean data_pending;	

    extern volatile uint16_t elapsed_centiseconds_mod_60000;
    extern volatile uint8_t  start_minute_mod_10;

    void debug();
    void setup();
    void auto_persist();  // this is slow and messes with the interrupt flag, do not call during interrupt handling
    void adjust();
    void process_1_kHz_tick();

    void qualify_calibration();
    void unqualify_calibration();

    typedef struct {
        bool qualified : 1;
        bool running : 1;
    } calibration_state_t;

    extern volatile calibration_state_t calibration_state;

    extern const int8_t calibration_second;
    calibration_state_t get_calibration_state();
    // The phase deviation is only meaningful if calibration is running.
    int16_t get_current_deviation();
    int16_t set_current_deviation(int16_t current_deviation);
    int16_t compute_phase_deviation(uint8_t current_second, uint8_t current_minute_mod_10);

    // Offset for writing to EEPROM / reading from EEPROM
    // this is necesarry if other libraries also want to
    // use EEPROM.
    // This library will use 8 bytes of EEPROM
    // 2 bytes for an identifier and 3 bytes for storing the
    // data redundantantly.
    const uint16_t eeprom_base = 0x00;
    void persist_to_eeprom(const int8_t adjust_steps, const int16_t adjust);  // this is slow, do not call during interrupt handling
    void read_from_eeprom(int8_t &adjust_steps, int16_t &adjust);

    // get the adjust step that was used for the last adjustment
    //   if there was no adjustment or if the frequency adjustment was poor it will return 0
    //   if the adjustment was from eeprom it will return the negative value of the persisted adjust step
    int8_t get_confirmed_precision();
}

namespace RaidoClock_Decade_Decoder {
    const uint8_t decades_per_century = 10;

    typedef struct {
        uint8_t data[decades_per_century];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } decade_bins;

    extern decade_bins bins;

    void setup();
    void advance_decade();
    BCD::bcd_t get_decade();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Year_Decoder {
    const uint8_t years_per_decade = 10;

    typedef struct {
        uint8_t data[years_per_decade];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } year_bins;

    extern year_bins bins;

    void setup();
    void advance_year();
    BCD::bcd_t get_year();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Month_Decoder {
    const uint8_t months_per_year = 12;

    typedef struct {
        uint8_t data[months_per_year];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } month_bins;

    extern month_bins bins;

    void setup();
    void advance_month();
    BCD::bcd_t get_month();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Weekday_Decoder {
    const uint8_t weekdays_per_week = 7;

    typedef struct {
        uint8_t data[weekdays_per_week];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } weekday_bins;

    extern weekday_bins bins;

    void setup();
    void advance_weekday();
    BCD::bcd_t get_weekday();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Day_Decoder {
    const uint8_t days_per_month = 31;

    typedef struct {
        uint8_t data[days_per_month];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } day_bins;

    extern day_bins bins;

    void setup();
    void advance_day();
    BCD::bcd_t get_day();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Hour_Decoder {
    const uint8_t hours_per_day = 24;

    typedef struct {
        uint8_t data[hours_per_day];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } hour_bins;

    extern hour_bins bins;

    void setup();
    void advance_hour();
    BCD::bcd_t get_hour();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();
}

namespace RadioClock_Minute_Decoder {
    const uint8_t minutes_per_hour = 60;

    typedef struct {
        uint8_t data[minutes_per_hour];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } minute_bins;

    extern minute_bins bins;

    void setup();
    void advance_minute();
    BCD::bcd_t get_minute();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();

    void debug();	
}

namespace RadioClock_Second_Decoder {
    const uint8_t seconds_per_minute = 60;

    typedef struct {
        uint8_t data[seconds_per_minute];
        uint8_t tick;

        uint8_t noise_max;
        uint8_t max;
        uint8_t max_index;
    } sync_bins;

    extern sync_bins bins;

    void setup();
    void get_quality(Hamming::lock_quality_t &lock_quality);
    uint8_t get_quality_factor();
}

namespace RadioClock_Controller {

    typedef Hamming::lock_quality_t lock_quality_t;

    typedef struct {
        struct {
            uint32_t lock_max;
            uint32_t noise_max;
        } phase;

        RadioClock::clock_state_t clock_state;
        uint8_t prediction_match;

        lock_quality_t second;
        lock_quality_t minute;
        lock_quality_t hour;
        lock_quality_t weekday;
        lock_quality_t day;
        lock_quality_t month;
        lock_quality_t year;
    } clock_quality_t;

    typedef struct {
        uint8_t phase;
        uint8_t second;
        uint8_t minute;
        uint8_t hour;
        uint8_t weekday;
        uint8_t day;
        uint8_t month;
        uint8_t year;
    } clock_quality_factor_t;

    void setup();	
    void on_tuned_clock();
    void auto_persist();  // this is slow and messes with the interrupt flag, do not call during interrupt handling
    void get_quality_factor(clock_quality_factor_t &clock_quality_factor);
    uint8_t get_clock_state();
    void phase_lost_event_handler();
    void sync_lost_event_handler();
    void sync_achieved_event_handler();	
}

namespace RadioClock_Local_Clock {
    extern RadioClock::clock_state_t clock_state;
    extern uint32_t max_unlocked_seconds;

    void set_has_tuned_clock();
    RadioClock::clock_state_t get_state();
}
#endif