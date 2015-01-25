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

#include "radioclock.h"

namespace BCD {
    void print(const bcd_t value) {
        Serial.print(value.val >> 4 & 0xF, HEX);
        Serial.print(value.val >> 0 & 0xF, HEX);
    }

    void increment(bcd_t &value) {
        if (value.digit.lo < 9) {
            ++value.digit.lo;
        } else {
            value.digit.lo = 0;

            if (value.digit.hi < 9) {
                ++value.digit.hi;
            } else {
                value.digit.hi = 0;
            }
        }
    }

    bcd_t int_to_bcd(const uint8_t value) {
        const uint8_t hi = value / 10;

        bcd_t result;
        result.digit.hi = hi;
        result.digit.lo = value-10*hi;

        return result;
    }

    uint8_t bcd_to_int(const bcd_t value) {
        return value.digit.lo + 10*value.digit.hi;
    }

    bool operator == (const bcd_t a, const bcd_t b) {
        return a.val == b.val;
    }
    bool operator != (const bcd_t a, const bcd_t b) {
        return a.val != b.val;
    }
    bool operator >= (const bcd_t a, const bcd_t b) {
        return a.val >= b.val;
    }
    bool operator <= (const bcd_t a, const bcd_t b) {
        return a.val <= b.val;
    }
    bool operator > (const bcd_t a, const bcd_t b) {
        return a.val > b.val;
    }
    bool operator < (const bcd_t a, const bcd_t b) {
        return a.val < b.val;
    }
}

namespace Arithmetic_Tools {
    void minimize(uint8_t &minimum, const uint8_t value) {
        if (value < minimum) {
            minimum = value;
        }
    }

    void maximize(uint8_t &maximum, const uint8_t value) {
        if (value > maximum) {
            maximum = value;
        }
    }

    uint8_t set_bit(const uint8_t data, const uint8_t number, const uint8_t value) {
        return value? data|(1<<number): data&~(1<<number);
    }
}

namespace Debug {
    void debug_helper(char data) { Serial.print(data == 0? 'S': data == 1? '?': data - 2 + '0', 0); }

    void bcddigit(uint8_t data) {
        if (data <= 0x09) {
            Serial.print(data, HEX);
        } else {
            Serial.print('?');
        }
    }

    void bcddigits(uint8_t data) {
        bcddigit(data >>  4);
        bcddigit(data & 0xf);
    }
}

namespace RadioClock_Clock {
    uint8_t get_clock_state() {
        return RadioClock_Controller::get_clock_state();
    }
}

namespace RadioClock_Demodulator {
    phase_bins bins;

    void setup() {
        Hamming::setup(bins);
    }

    uint16_t wrap(const uint16_t value) {
        // faster modulo function which avoids division
        uint16_t result = value;
        while (result >= bin_count) {
            result-= bin_count;
        }
        return result;
    }	

    void phase_detection() {
        // We will compute the integrals over 200ms.
        // The integrals is used to find the window of maximum signal strength.
        uint32_t integral = 0;

        for (uint16_t bin = 0; bin < bins_per_100ms; ++bin) {
            integral += ((uint32_t)bins.data[bin])<<1;
        }

        for (uint16_t bin = bins_per_100ms; bin < bins_per_200ms; ++bin) {
            integral += (uint32_t)bins.data[bin];
        }

        bins.max = 0;
        bins.max_index = 0;
        for (uint16_t bin = 0; bin < bin_count; ++bin) {
            if (integral > bins.max) {
                bins.max = integral;
                bins.max_index = bin;
            }

            integral -= (uint32_t)bins.data[bin]<<1;
            integral += (uint32_t)(bins.data[wrap(bin + bins_per_100ms)] +
            bins.data[wrap(bin + bins_per_200ms)]);
        }

        // max_index indicates the position of the 200ms second signal window.
        // Now how can we estimate the noise level? This is very tricky because
        // averaging has already happened to some extend.

        // The issue is that most of the undesired noise happens around the signal,
        // especially after high->low transitions. So as an approximation of the
        // noise I test with a phase shift of 200ms.
        bins.noise_max = 0;
        const uint16_t noise_index = wrap(bins.max_index + bins_per_200ms);

        for (uint16_t bin = 0; bin < bins_per_100ms; ++bin) {
            bins.noise_max += ((uint32_t)bins.data[wrap(noise_index + bin)])<<1;
        }

        for (uint16_t bin = bins_per_100ms; bin < bins_per_200ms; ++bin) {
            bins.noise_max += (uint32_t)bins.data[wrap(noise_index + bin)];
        }
    }

    // how many seconds may be cummulated
    // this controls how slow the filter may be to follow a phase drift
    // N times the clock precision shall be smaller 1/100
    // clock 30 ppm => N < 300
    uint16_t N = 300;
    void set_has_tuned_clock() {
        // will be called once crystal is tuned to better than 1 ppm.
        N = 3600;
    }

    uint8_t phase_binning(const uint8_t input) {
        Hamming::advance_tick(bins);

        uint16_t& data = bins.data[bins.tick];

        if (data > N) {
            data = N;
        }

        if (input) {
            if (data < N) {
                ++data;
            }
        } else {
            if (data > 0) {
                --data;
            }
        }
        return bins.tick;
    }

    void get_quality(uint32_t &lock_max, uint32_t &noise_max) {
        const uint8_t prev_SREG = SREG;
        cli();
        lock_max = bins.max;
        noise_max = bins.noise_max;
        SREG = prev_SREG;
    }

    uint8_t get_quality_factor() {
        const uint8_t prev_SREG = SREG;
        cli();
        uint32_t delta = bins.max - bins.noise_max;
        SREG = prev_SREG;

        uint8_t log2_plus_1 = 0;
        uint32_t max = bins.max;
        while (max) {
            max >>= 1;
            ++log2_plus_1;
        }

        // crude approximation for delta/log2(max)
        while (log2_plus_1) {
            log2_plus_1 >>= 1;
            delta >>= 1;
        }

        return delta<256? delta: 255;
    }

    void debug() {
        Serial.print(F("Phase: "));
        Hamming::debug(bins);
    }

    void debug_verbose() {
        // attention: debug_verbose is not really thread save
        //            thus the output may contain unexpected artifacts
        //            do not rely on the output of one debug cycle
        debug();

        Serial.println(F("max_index, max, index, integral"));

        uint32_t integral = 0;
        for (uint16_t bin = 0; bin < bins_per_100ms; ++bin) {
            integral += ((uint32_t)bins.data[bin])<<1;
        }

        for (uint16_t bin = bins_per_100ms; bin < bins_per_200ms; ++bin) {
            integral += (uint32_t)bins.data[bin];
        }

        uint32_t max = 0;
        uint8_t max_index = 0;
        for (uint16_t bin = 0; bin < bin_count; ++bin) {
            if (integral > max) {
                max = integral;
                max_index = bin;
            }

            integral -= (uint32_t)bins.data[bin]<<1;
            integral += (uint32_t)(bins.data[wrap(bin + bins_per_100ms)] +
            bins.data[wrap(bin + bins_per_200ms)]);

            Serial.print(max_index);
            Serial.print(F(", "));
            Serial.print(max);
            Serial.print(F(", "));
            Serial.print(bin);
            Serial.print(F(", "));
            Serial.println(integral);
        }

        // max_index indicates the position of the 200ms second signal window.
        // Now how can we estimate the noise level? This is very tricky because
        // averaging has already happened to some extend.

        // The issue is that most of the undesired noise happens around the signal,
        // especially after high->low transitions. So as an approximation of the
        // noise I test with a phase shift of 200ms.
        uint32_t noise_max = 0;
        const uint16_t noise_index = wrap(max_index + bins_per_200ms);

        for (uint16_t bin = 0; bin < bins_per_100ms; ++bin) {
            noise_max += ((uint32_t)bins.data[wrap(noise_index + bin)])<<1;
        }

        for (uint16_t bin = bins_per_100ms; bin < bins_per_200ms; ++bin) {
            noise_max += (uint32_t)bins.data[wrap(noise_index + bin)];
        }

        Serial.print(F("noise_index, noise_max: "));
        Serial.print(noise_index);
        Serial.print(F(", "));
        Serial.println(noise_max);
    }
}

namespace RadioClock_1_Khz_Generator {

    extern RadioClock_Clock::input_provider_t the_input_provider = zero_provider;

    bool zero_provider() {
        return 0;
    }

    void adjust(const int16_t pp16m) {
        const uint8_t prev_SREG = SREG;
        cli();
        // positive_value --> increase frequency
        adjust_pp16m = pp16m;
        SREG = prev_SREG;
    }

    int16_t read_adjustment() {
        // positive_value --> increase frequency
        const uint8_t prev_SREG = SREG;
        cli();
        const int16_t pp16m = adjust_pp16m;
        SREG = prev_SREG;
        return pp16m;
    }

    void init_timer() {
#if defined(__AVR_ATmega32U4__)
        // Timer 3 CTC mode, prescaler 64
        TCCR3B = (0<<WGM33) | (1<<WGM32) | (1<<CS31) | (1<<CS30);
        TCCR3A = (0<<WGM31) | (0<<WGM30);

        // 249 + 1 == 250 == 250 000 / 1000 =  (16 000 000 / 64) / 1000
        OCR3A = 249;

        // enable Timer 3 interrupts
        TIMSK3 = (1<<OCIE3A);
#else
        // Timer 2 CTC mode, prescaler 64
        TCCR2B = (0<<WGM22) | (1<<CS22);
        TCCR2A = (1<<WGM21) | (0<<WGM20);

        // 249 + 1 == 250 == 250 000 / 1000 =  (16 000 000 / 64) / 1000
        OCR2A = 249;

        // enable Timer 2 interrupts
        TIMSK2 = (1<<OCIE2A);
#endif
    }

    void stop_timer_0() {
        // ensure that the standard timer interrupts will not
        // mess with msTimer2
        TIMSK0 = 0;
    }

    void setup(const RadioClock_Clock::input_provider_t input_provider) {
        init_timer();
        stop_timer_0();
        the_input_provider = input_provider;
    }
    void isr_handler() {
        cumulated_phase_deviation += RadioClock_1_Khz_Generator::adjust_pp16m;
        // 1 / 250 / 64000 = 1 / 16 000 000
        if (cumulated_phase_deviation >= 64000) {
            cumulated_phase_deviation -= 64000;
            // cumulated drift exceeds 1 timer step (4 microseconds)
            // drop one timer step to realign
#if defined(__AVR_ATmega32U4__)
            OCR3A = 248;
#else
            OCR2A = 248;
#endif
        } else
        if (cumulated_phase_deviation <= -64000) {
            // cumulated drift exceeds 1 timer step (4 microseconds)
            // insert one timer step to realign
            cumulated_phase_deviation += 64000;
#if defined(__AVR_ATmega32U4__)
            OCR3A = 250;
#else
            OCR2A = 250;
#endif
        } else {
            // 249 + 1 == 250 == 250 000 / 1000 =  (16 000 000 / 64) / 1000
#if defined(__AVR_ATmega32U4__)
            OCR3A = 249;
#else
            OCR2A = 249;
#endif
        }
    }
}

namespace RadioClock_Frequency_Control {
    volatile int8_t confirmed_precision = 0;

    // 2*tau_max = 32 000 000 centisecond ticks = 5333 minutes
    volatile uint16_t elapsed_minutes;

    // indicator if data may be persisted to EEPROM
    volatile boolean data_pending;

    // 60000 centiseconds = 10 minutes
    // maximum drift in 32 000 000 centiseconds @ 900 ppm would result
    // in a drift of +/- 28800 centiseconds
    // thus it is uniquely measured if we know it mod 60 000
    volatile uint16_t elapsed_centiseconds_mod_60000;
    volatile uint8_t  start_minute_mod_10;


    // Seconds 0 and 15 already receive more computation than
    // other seconds thus calibration will run in second 5.
    const int8_t calibration_second = 5;

    volatile calibration_state_t calibration_state = {false ,false};
    volatile int16_t deviation;

    // get the adjust step that was used for the last adjustment
    //   if there was no adjustment or if the phase drift was poor it will return 0
    //   if the adjustment was from eeprom it will return the negative value of the
    //   persisted adjust step
    int8_t get_confirmed_precision() {
        return confirmed_precision;
    }

    void qualify_calibration() {
        calibration_state.qualified = true;
    };
    
    // do not call during ISRs
    void auto_persist() {
        // ensure that reading of data can not be interrupted!!
        // do not write EEPROM while interrupts are blocked
        int16_t adjust;
        int8_t  precision;
        const uint8_t prev_SREG = SREG;
        cli();
        if (data_pending && get_confirmed_precision() > 0) {
            precision = get_confirmed_precision();
            adjust = RadioClock_1_Khz_Generator::read_adjustment();
        } else {
            data_pending = false;
        }
        SREG = prev_SREG;
        if (data_pending) {
            int16_t ee_adjust;
            int8_t  ee_precision;
            read_from_eeprom(ee_precision, ee_adjust);

            if (get_confirmed_precision() < abs(ee_precision) ||        // - precision better than it used to be
                    ( abs(ee_precision) < 8 &&                        // - precision better than 8 Hz or 0.5 ppm @ 16 MHz
                        abs(ee_adjust-adjust) > 8 )           ||        //   deviation worse than 8 Hz (thus 16 Hz or 1 ppm)
                    ( get_confirmed_precision() == 1 &&                     // - It takes more than 1 day to arrive at 1 Hz precision
                        abs(ee_adjust-adjust) > 0 ) )                   //   thus it acceptable to always write
            {
                cli();
                const int16_t new_ee_adjust = adjust;
                const int8_t  new_ee_precision = precision;
                SREG = prev_SREG;
                persist_to_eeprom(new_ee_precision, new_ee_adjust);
                RadioClock_Controller::on_tuned_clock();
            }
            data_pending = false;
        }
    }

    void setup() {
        int16_t adjust;
        int8_t ee_precision;

        read_from_eeprom(ee_precision, adjust);
        if (ee_precision) {
            RadioClock_Controller::on_tuned_clock();
        }

        const uint8_t prev_SREG = SREG;
        cli();

        SREG = prev_SREG;
        RadioClock_1_Khz_Generator::adjust(adjust);
    }

    void unqualify_calibration() {
        calibration_state.qualified = false;
    };

    int16_t compute_phase_deviation(uint8_t current_second, uint8_t current_minute_mod_10) {
        int32_t deviation=
        ((int32_t) elapsed_centiseconds_mod_60000) -
        ((int32_t) current_second        - (int32_t) calibration_second)  * 100 -
        ((int32_t) current_minute_mod_10 - (int32_t) start_minute_mod_10) * 6000;

        // ensure we are between 30000 and -29999
        while (deviation >  30000) { deviation -= 60000; }
        while (deviation <=-30000) { deviation += 60000; }

        return deviation;
    }

    calibration_state_t get_calibration_state() {
        return *(calibration_state_t *)&calibration_state;
    }

    int16_t get_current_deviation() {
        return deviation;
    }
    
    int16_t set_current_deviation(int16_t get_current_deviation) {
        deviation = get_current_deviation;
    }

    void adjust() {
        int16_t total_adjust = RadioClock_1_Khz_Generator::read_adjustment();

        // The proper formular would be
        //     int32_t adjust == (16000000 / (elapsed_minutes * 6000)) * new_deviation;
        // The total error of the formula below is ~ 1/(3*elapsed_minutes)
        //     which is  ~ 1/1000
        // Also notice that 2667*deviation will not overflow even if the
        // local clock would deviate by more than 400 ppm or 6 kHz
        // from its nominal frequency.
        // Finally notice that the frequency_offset will always be rounded towards zero_provider
        // while the confirmed_precision is rounded away from zereo. The first should
        // be considered a kind of relaxation while the second should be considered
        // a defensive computation.
        const int16_t frequency_offset = ((2667 * (int32_t)deviation) / elapsed_minutes);
        // In doubt confirmed precision will be slightly larger than the true value
        confirmed_precision = (((2667 - 1) * 1) + elapsed_minutes) / elapsed_minutes;
        if (confirmed_precision == 0) { confirmed_precision = 1; }

        total_adjust -= frequency_offset;

        if (total_adjust >  max_total_adjust) { total_adjust =  max_total_adjust; }
        if (total_adjust < -max_total_adjust) { total_adjust = -max_total_adjust; }

        RadioClock_1_Khz_Generator::adjust(total_adjust);
    }

    void process_1_kHz_tick() {
        static uint8_t divider = 0;
        if (divider < 9) {
            ++divider;
        }  else {
            divider = 0;

            if (elapsed_centiseconds_mod_60000 < 59999) {
                ++elapsed_centiseconds_mod_60000;
            } else {
                elapsed_centiseconds_mod_60000 = 0;
            }
            if (elapsed_centiseconds_mod_60000 % 6000 == 0) {
                ++elapsed_minutes;
            }
        }
    }

    // ID constants to see if EEPROM has already something stored
    const char ID_u = 'u';
    const char ID_k = 'k';
    void persist_to_eeprom(const int8_t precision, const int16_t adjust) {
        // this is slow, do not call during interrupt handling
        uint16_t eeprom = eeprom_base;
        eeprom_write_byte((uint8_t *)(eeprom++), ID_u);
        eeprom_write_byte((uint8_t *)(eeprom++), ID_k);
        eeprom_write_byte((uint8_t *)(eeprom++), (uint8_t) precision);
        eeprom_write_byte((uint8_t *)(eeprom++), (uint8_t) precision);
        eeprom_write_word((uint16_t *)eeprom, (uint16_t) adjust);
        eeprom += 2;
        eeprom_write_word((uint16_t *)eeprom, (uint16_t) adjust);
    }

    void read_from_eeprom(int8_t &precision, int16_t &adjust) {
        uint16_t eeprom = eeprom_base;
        if (eeprom_read_byte((const uint8_t *)(eeprom++)) == ID_u &&
                eeprom_read_byte((const uint8_t *)(eeprom++)) == ID_k) {
            uint8_t ee_precision = eeprom_read_byte((const uint8_t *)(eeprom++));
            if (ee_precision == eeprom_read_byte((const uint8_t *)(eeprom++))) {
                const uint16_t ee_adjust = eeprom_read_word((const uint16_t *)eeprom);
                eeprom += 2;
                if (ee_adjust == eeprom_read_word((const uint16_t *)eeprom)) {
                    precision = (int8_t) ee_precision;
                    adjust = (int16_t) ee_adjust;
                    return;
                }
            }
        }
        precision = 0;
        adjust = 0;
    }

    void debug() {
        Serial.println(F("confirmed_precision ?? adjustment, deviation, elapsed"));
        Serial.print(confirmed_precision);
        Serial.print(F(" Hz "));
        Serial.print(calibration_state.running? '@': '.');
        Serial.print(calibration_state.qualified? '+': '-');
        Serial.print(' ');

        Serial.print(F(", "));
        Serial.print(RadioClock_1_Khz_Generator::read_adjustment());
        Serial.print(F(" Hz, "));

        Serial.print(deviation);
        Serial.print(F(" ticks, "));

        Serial.print(elapsed_minutes);
        Serial.print(F(" min, "));

        Serial.print(elapsed_centiseconds_mod_60000);
        Serial.println(F(" cs mod 60000"));

    }
}

namespace RaidoClock_Decade_Decoder {
    decade_bins bins;

    void advance_decade() {
        Hamming::advance_tick(bins);
    }

    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_decade() {
        return Hamming::get_time_value(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void debug() {
        Serial.print(F("Decade: "));
        Hamming::debug(bins);
    }
}

namespace RadioClock_Year_Decoder {
    year_bins bins;
    
    void advance_year() {
        Hamming::advance_tick(bins);
        if (Hamming::get_time_value(bins).val == 0) {
            RaidoClock_Decade_Decoder::advance_decade();
        }
    }	
    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);

        Hamming::lock_quality_t decade_lock_quality;
        RaidoClock_Decade_Decoder::get_quality(decade_lock_quality);

        Arithmetic_Tools::minimize(lock_quality.lock_max, decade_lock_quality.lock_max);
        Arithmetic_Tools::maximize(lock_quality.noise_max, decade_lock_quality.noise_max);
    }

    uint8_t get_quality_factor() {
        const uint8_t qf_years = Hamming::get_quality_factor(bins);
        const uint8_t qf_decades = RaidoClock_Decade_Decoder::get_quality_factor();
        return min(qf_years, qf_decades);
    }

    BCD::bcd_t get_year() {
        BCD::bcd_t year = Hamming::get_time_value(bins);
        BCD::bcd_t decade = RaidoClock_Decade_Decoder::get_decade();

        if (year.val == 0xff || decade.val == 0xff) {
            // undefined handling
            year.val = 0xff;
        } else {
            year.val += decade.val << 4;
        }
        return year;
    }

    void setup() {
        Hamming::setup(bins);
        RaidoClock_Decade_Decoder::setup();
    }

    void debug() {
        Serial.print(F("Year: "));
        Hamming::debug(bins);
        RaidoClock_Decade_Decoder::debug();
    }
}

namespace RadioClock_Month_Decoder {
    month_bins bins;

    void advance_month() {
        Hamming::advance_tick(bins);
    }
    
    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_month() {
        return Hamming::get_time_value(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void debug() {
        Serial.print(F("Month: "));
        Hamming::debug(bins);
    }
}

namespace RadioClock_Weekday_Decoder {
    weekday_bins bins;
    
    void advance_weekday() {
        Hamming::advance_tick(bins);
    }

    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_weekday() {
        return Hamming::get_time_value(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void debug() {
        Serial.print(F("Weekday: "));
        Hamming::debug(bins);
    }	
}

namespace RadioClock_Day_Decoder {
    day_bins bins;

    void advance_day() {
        Hamming::advance_tick(bins);
    }

    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_day() {
        return Hamming::get_time_value(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void debug() {
        Serial.print(F("Day: "));
        Hamming::debug(bins);
    }
}

namespace RadioClock_Hour_Decoder {
    hour_bins bins;
    
    void advance_hour() {
        Hamming::advance_tick(bins);
    }

    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_hour() {
        return Hamming::get_time_value(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void debug() {
        Serial.print(F("Hour: "));
        Hamming::debug(bins);
    }	
}

namespace RadioClock_Minute_Decoder {
    minute_bins bins;

    void advance_minute() {
        Hamming::advance_tick(bins);
    }

    void setup() {
        Hamming::setup(bins);
    }

    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }

    BCD::bcd_t get_minute() {
        return Hamming::get_time_value(bins);
    }

    void debug() {
        Serial.print(F("Minute: "));
        Hamming::debug(bins);
    }	
}

namespace RadioClock_Second_Decoder {
    sync_bins bins;

    void setup() {
        Hamming::setup(bins);
    }


    void get_quality(Hamming::lock_quality_t &lock_quality) {
        Hamming::get_quality(bins, lock_quality);
    }

    uint8_t get_quality_factor() {
        return Hamming::get_quality_factor(bins);
    }	
}

namespace RadioClock_Controller {
    void on_tuned_clock() {
        RadioClock_Demodulator::set_has_tuned_clock();
        RadioClock_Local_Clock::set_has_tuned_clock();
    };

    void get_quality_factor(clock_quality_factor_t &clock_quality_factor) {
        clock_quality_factor.phase   = RadioClock_Demodulator::get_quality_factor();
        clock_quality_factor.second  = RadioClock_Second_Decoder::get_quality_factor();
        clock_quality_factor.minute  = RadioClock_Minute_Decoder::get_quality_factor();
        clock_quality_factor.hour    = RadioClock_Hour_Decoder::get_quality_factor();
        clock_quality_factor.day     = RadioClock_Day_Decoder::get_quality_factor();
        clock_quality_factor.weekday = RadioClock_Weekday_Decoder::get_quality_factor();
        clock_quality_factor.month   = RadioClock_Month_Decoder::get_quality_factor();
        clock_quality_factor.year    = RadioClock_Year_Decoder::get_quality_factor();
    }

    void phase_lost_event_handler() {
        // do not reset frequency control as a reset would also reset
        // the current value for the measurement period length
        RadioClock_Second_Decoder::setup();
        RadioClock_Minute_Decoder::setup();
        RadioClock_Hour_Decoder::setup();
        RadioClock_Day_Decoder::setup();
        RadioClock_Weekday_Decoder::setup();
        RadioClock_Month_Decoder::setup();
        RadioClock_Year_Decoder::setup();
    }

    void sync_achieved_event_handler() {
        // It can be argued if phase events instead of sync events
        // should be used. In theory it would be sufficient to have a
        // reasonable phase at the start and end of a calibration measurement
        // interval.
        // On the other hand a clean signal will provide a better calibration.
        // Since it is sufficient if the calibration happens only once in a
        // while we are satisfied with hooking at the sync events.
        RadioClock_Frequency_Control::qualify_calibration();
    }

    void sync_lost_event_handler() {
        RadioClock_Frequency_Control::unqualify_calibration();

        bool reset_successors = (RadioClock_Demodulator::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Second_Decoder::setup();
        }

        reset_successors |= (RadioClock_Second_Decoder::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Minute_Decoder::setup();
        }

        reset_successors |= (RadioClock_Minute_Decoder::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Hour_Decoder::setup();
        }

        reset_successors |= (RadioClock_Hour_Decoder::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Weekday_Decoder::setup();
            RadioClock_Day_Decoder::setup();
        }

        reset_successors |= (RadioClock_Hour_Decoder::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Month_Decoder::setup();
        }

        reset_successors |= (RadioClock_Month_Decoder::get_quality_factor() == 0);
        if (reset_successors) {
            RadioClock_Year_Decoder::setup();
        }
    }
    
    void setup() {
        RadioClock_Demodulator::setup();
        RadioClock_Controller::phase_lost_event_handler();
        RadioClock_Frequency_Control::setup();
    }
    
    void auto_persist() {
        RadioClock_Frequency_Control::auto_persist();
    }

    uint8_t get_clock_state() {
        return RadioClock_Local_Clock::get_state();
    }
}

namespace RadioClock_Local_Clock {
    RadioClock::clock_state_t clock_state = RadioClock::useless;
    uint32_t max_unlocked_seconds = 3000;

    void set_has_tuned_clock() {
        max_unlocked_seconds = 30000;
    }
    
    RadioClock::clock_state_t get_state() {
        return RadioClock_Local_Clock::clock_state;
    }
}