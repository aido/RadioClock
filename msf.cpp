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


#include "msf.h"

namespace MSF_Encoder {
    using namespace MSF;

    inline uint8_t days_per_month(const MSF::time_data_t &now);
    uint8_t days_per_month(const MSF::time_data_t &now) {
        switch (now.month.val) {
        case 0x02:
            // valid till 31.12.2399
            // notice year mod 4 == year & 0x03
            return 28 + ((now.year.val != 0) && ((bcd_to_int(now.year) & 0x03) == 0)? 1: 0);
        case 0x01: case 0x03: case 0x05: case 0x07: case 0x08: case 0x10: case 0x12: return 31;
        case 0x04: case 0x06: case 0x09: case 0x11:                                  return 30;
        default: return 0;
        }
    }

    void reset(MSF::time_data_t &now) {
        now.second      = 0;
        now.minute.val  = 0x00;
        now.hour.val    = 0x00;
        now.day.val     = 0x01;
        now.month.val   = 0x01;
        now.year.val    = 0x00;
        now.weekday.val = 0x01;
        now.uses_summertime             = false;
        now.timezone_change_scheduled   = false;

        now.undefined_year_output       = false;
        now.undefined_day_output        = false;
        now.undefined_weekday_output    = false;
        now.undefined_time_output       = false;
    }

    uint8_t weekday(const MSF::time_data_t &now) {  // attention: sunday will be ==0 instead of 7

        if (now.day.val <= 0x31 && now.month.val <= 0x12 && now.year.val <= 0x99) {
            // This will compute the weekday for each year in 2001-2099.
            // If you really plan to use my code beyond 2099 take care of this
            // on your own. My assumption is that it is even unclear if MSF
            // will still exist then.

            // http://de.wikipedia.org/wiki/Gau%C3%9Fsche_Wochentagsformel
            const uint8_t  d = bcd_to_int(now.day);
            const uint16_t m = now.month.val <= 0x02? now.month.val + 10:
            bcd_to_int(now.month) - 2;
            const uint8_t  y = bcd_to_int(now.year) - (now.month.val <= 0x02);
            // m must be of type uint16_t otherwise this will compute crap
            uint8_t day_mod_7 = d + (26*m - 2)/10 + y + y/4;
            // We exploit 8 mod 7 = 1
            while (day_mod_7 >= 7) {
                day_mod_7 -= 7;
                day_mod_7 = (day_mod_7 >> 3) + (day_mod_7 & 7);
            }

            return day_mod_7;  // attention: sunday will be == 0 instead of 7
        } else {
            return 0xff;
        }
    }

    BCD::bcd_t bcd_weekday(const MSF::time_data_t &now) {
        BCD::bcd_t today;

        today.val = weekday(now);
        if (today.val == 0) {
            today.val = 7;
        }

        return today;
    }

    void autoset_weekday(MSF::time_data_t &now) {
        now.weekday = bcd_weekday(now);
    }

    void autoset_timezone(MSF::time_data_t &now) {
        // timezone change may only happen at the last sunday of march / october
        // the last sunday is always somewhere in [25-31]

        // Wintertime --> Summertime happens at 01:00 UTC == 01:00 GMT == 01:00 BST,
        // Summertime --> Wintertime happens at 01:00 UTC == 01:00 GMT == 02:00 BST

        if (now.month.val < 0x03) {
            // January or February
            now.uses_summertime = false;
        } else
        if (now.month.val == 0x03) {
            // March
            if (now.day.val < 0x25) {
                // Last Sunday of March must be 0x25-0x31
                // Thus still to early for summertime
                now.uses_summertime = false;
            } else
            if (uint8_t wd = weekday(now)) {
                // wd != 0 --> not a Sunday
                if (now.day.val - wd < 0x25) {
                    // early March --> wintertime
                    now.uses_summertime = false;
                } else {
                    // late march summertime
                    now.uses_summertime = true;
                }
            } else {
                // last sunday of march
                // decision depends on the current hour
                now.uses_summertime = (now.hour.val > 2);
            }
        } else
        if (now.month.val < 0x10) {
            // April - September
            now.uses_summertime = true;
        } else
        if (now.month.val == 0x10) {
            // October
            if (now.day.val < 0x25) {
                // early October
                now.uses_summertime = true;
            } else
            if (uint8_t wd = weekday(now)) {
                // wd != 0 --> not a Sunday
                if (now.day.val - wd < 0x25) {
                    // early October --> summertime
                    now.uses_summertime = true;
                } else {
                    // late October --> wintertime
                    now.uses_summertime = false;
                }
            } else {  // last sunday of october
                if (now.hour.val == 2) {
                    // can not derive the flag from time data
                    // this is the only time the flag is derived
                    // from the flag vector

                } else {
                    // decision depends on the current hour
                    now.uses_summertime = (now.hour.val < 2);
                }
            }
        } else {
            // November and December
            now.uses_summertime = false;
        }
    }

    void autoset_timezone_change_scheduled(MSF::time_data_t &now) {
        // summer/wintertime change will always happen
        // at clearly defined hours
        // https://en.wikipedia.org/wiki/British_Summer_Time

        // in doubt have a look here: http://www.dcf77logs.de/
        if (now.day.val < 0x25 || weekday(now) != 0) {
            // timezone change may only happen at the last sunday of march / october
            // the last sunday is always somewhere in [25-31]

            // notice that undefined (==0xff) day/weekday data will not cause any action
            now.timezone_change_scheduled = false;
        } else {
            if (now.month.val == 0x03) {
                if (now.uses_summertime) {
                    now.timezone_change_scheduled = (now.hour.val == 0x03 && now.minute.val == 0x00); // wintertime to summertime, preparing first minute of summertime
                } else {
                    now.timezone_change_scheduled = (now.hour.val == 0x01 && now.minute.val != 0x00); // wintertime to summertime
                }
            } else if (now.month.val == 0x10) {
                if (now.uses_summertime) {
                    now.timezone_change_scheduled = (now.hour.val == 0x02 && now.minute.val != 0x00); // summertime to wintertime
                } else {
                    now.timezone_change_scheduled = (now.hour.val == 0x02 && now.minute.val == 0x00); // summertime to wintertime, preparing first minute of wintertime
                }
            } else if (now.month.val <= 0x12) {
                now.timezone_change_scheduled = false;
            }
        }
    }

    void autoset_control_bits(MSF::time_data_t &now) {
        autoset_weekday(now);
        autoset_timezone(now);
        autoset_timezone_change_scheduled(now);
    }

    void advance_second(MSF::time_data_t &now) {
        // in case some value is out of range it will not be advanced
        // this is on purpose
        if (now.second < 59) {
            ++now.second;
            if (now.second == 36) {
                autoset_control_bits(now);
            }
        } else if (now.second == 59 || now.second == 60) {
            now.second = 0;
            advance_minute(now);
        }
    }
    // Wintertime --> Summertime happens at 01:00 UTC == 01:00 GMT == 01:00 BST,
    // Summertime --> Wintertime happens at 01:00 UTC == 01:00 GMT == 02:00 BST

    void advance_minute(MSF::time_data_t &now) {
        if (now.minute.val < 0x59) {
            increment(now.minute);
        } else if (now.minute.val == 0x59) {
            now.minute.val = 0x00;
            // in doubt have a look here: http://www.dcf77logs.de/
            if (now.timezone_change_scheduled && !now.uses_summertime && now.hour.val == 0x01) {
                // Wintertime --> Summertime happens at 01:00 UTC == 01:00 GMT == 02:00 BST,
                // the clock must be advanced from 00:59 GMT to 02:00 BST
                increment(now.hour);
                increment(now.hour);
                now.uses_summertime = true;
            }  else if (now.timezone_change_scheduled && now.uses_summertime && now.hour.val == 0x02) {
                // Summertime --> Wintertime happens at 01:00 UTC == 01:00 GMT == 02:00 BST,
                // the clock must be decreased from 01:59 BST to 01:00 GMT
                now.uses_summertime = false;
            } else {
                if (now.hour.val < 0x23) {
                    increment(now.hour);
                } else if (now.hour.val == 0x23) {
                    now.hour.val = 0x00;

                    if (now.weekday.val < 0x07) {
                        increment(now.weekday);
                    } else if (now.weekday.val == 0x07) {
                        now.weekday.val = 0x01;
                    }

                    if (bcd_to_int(now.day) < days_per_month(now)) {
                        increment(now.day);
                    } else if (bcd_to_int(now.day) == days_per_month(now)) {
                        now.day.val = 0x01;

                        if (now.month.val < 0x12) {
                            increment(now.month);
                        } else if (now.month.val == 0x12) {
                            now.month.val = 0x01;

                            if (now.year.val < 0x99) {
                                increment(now.year);
                            } else if (now.year.val == 0x99) {
                                now.year.val = 0x00;
                            }
                        }
                    }
                }
            }
        }
    }

    MSF::tick_t get_current_signal(const MSF::time_data_t &now) {
        using namespace Arithmetic_Tools;

        // With MSF signal two data bits are transmitted per second
        bool result_A;
        bool result_B;

        switch (now.second) {
        case 0:  // start of minute
            return min_marker; break;

        case 1 ... 16:  // DUT1 data we can not compute
            return undefined; break;

        case 17:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.hi & 0x8;
            result_B = 0; break;
        case 18:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.hi & 0x4;
            result_B = 0; break;
        case 19:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.hi & 0x2;
            result_B = 0; break;
        case 20:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.hi & 0x1;
            result_B = 0; break;
        case 21:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.lo & 0x8;
            result_B = 0; break;
        case 22:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.lo & 0x4;
            result_B = 0; break;
        case 23:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.lo & 0x2;
            result_B = 0; break;
        case 24:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = now.year.digit.lo & 0x1;
            result_B = 0; break;

        case 25:
            if (now.undefined_day_output || now.month.val > 0x12) { return undefined; }
            result_A = now.month.digit.hi & 0x1;
            result_B = 0; break;
        case 26:
            if (now.undefined_day_output || now.month.val > 0x12) { return undefined; }
            result_A = now.month.digit.lo & 0x8;
            result_B = 0; break;
        case 27:
            if (now.undefined_day_output || now.month.val > 0x12) { return undefined; }
            result_A = now.month.digit.lo & 0x4;
            result_B = 0; break;
        case 28:
            if (now.undefined_day_output || now.month.val > 0x12) { return undefined; }
            result_A = now.month.digit.lo & 0x2;
            result_B = 0; break;
        case 29:
            if (now.undefined_day_output || now.month.val > 0x12) { return undefined; }
            result_A = now.month.digit.lo & 0x1;
            result_B = 0; break;

        case 30:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.hi & 0x2; break;
            result_B = 0;
        case 31:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.hi & 0x1;				
            result_B = 0; break;
        case 32:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.lo & 0x8;
            result_B = 0; break;
        case 33:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.lo & 0x4;
            result_B = 0; break;
        case 34:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.lo & 0x2;;
            result_B = 0; break;
        case 35:
            if (now.undefined_day_output || now.day.val > 0x31) { return undefined; }
            result_A = now.day.digit.lo & 0x1;
            result_B = 0; break;

        case 36:
            if (now.undefined_weekday_output || now.weekday.val > 0x7) { return undefined; }
            result_A = now.weekday.val & 0x4;
            result_B = 0; break;
        case 37:
            if (now.undefined_weekday_output || now.weekday.val > 0x7) { return undefined; }
            result_A = now.weekday.val & 0x2;
            result_B = 0; break;
        case 38:
            if (now.undefined_weekday_output || now.weekday.val > 0x7) { return undefined; }
            result_A = now.weekday.val & 0x1;
            result_B = 0; break;

        case 39:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.hi & 0x2;
            result_B = 0; break;
        case 40:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.hi & 0x1;
            result_B = 0; break;
        case 41:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.lo & 0x8;
            result_B = 0; break;
        case 42:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.lo & 0x4;
            result_B = 0; break;
        case 43:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.lo & 0x2;
            result_B = 0; break;
        case 44:
            if (now.undefined_time_output || now.hour.val > 0x23) { return undefined; }
            result_A = now.hour.digit.lo & 0x1;
            result_B = 0; break;

        case 45:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.hi & 0x4;
            result_B = 0; break;
        case 46:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.hi & 0x2;
            result_B = 0; break;
        case 47:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.hi & 0x1;
            result_B = 0; break;
        case 48:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.lo & 0x8;
            result_B = 0; break;
        case 49:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.lo & 0x4;
            result_B = 0; break;
        case 50:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.lo & 0x2;
            result_B = 0; break;
        case 51:
            if (now.undefined_time_output || now.minute.val > 0x59) { return undefined; }
            result_A = now.minute.digit.lo & 0x1;
            result_B = 0; break;

        case 52:
            result_A = 0; result_B = 0; break;

        case 53:
            if (now.undefined_timezone_change_scheduled_output) { return undefined; }
            result_A = 1;			
            result_B = now.timezone_change_scheduled; break;

        case 54:
            if (now.undefined_year_output || now.year.val > 0x99) { return undefined; }
            result_A = 1;			
            result_B = !parity(now.year.val); break;
        case 55:
            if (now.undefined_day_output || now.month.val > 0x12 || now.day.val > 0x31) { return undefined; }
            result_A = 1;			
            result_B = !(parity(now.month.digit.hi) ^
                        parity(now.month.digit.lo) ^
                        parity(now.day.digit.hi) ^
                        parity(now.day.digit.lo)); break;
        case 56:
            if (now.undefined_weekday_output || now.weekday.val > 0x07) { return undefined; }
            result_A = 1;			
            result_B = !parity(now.weekday.val); break;
        case 57:
            if (now.undefined_time_output || now.hour.val > 0x23 || now.minute.val > 0x59) { return undefined; }
            result_A = 1;			
            result_B = !(parity(now.hour.digit.hi) ^
                        parity(now.hour.digit.lo) ^
                        parity(now.minute.digit.hi) ^
                        parity(now.minute.digit.lo)); break;

        case 58:
            if (now.undefined_uses_summertime_output) {return undefined; }
            result_A = 1;			
            result_B = now.uses_summertime; break;

        case 59:
            result_A = 0; result_B = 0; break;

        default:
            return undefined;
        }

        return ((MSF::tick_t)(result_A<<1 + result_B));
    }

    void get_serialized_clock_stream(const MSF::time_data_t &now, MSF::serialized_clock_stream &data) {
        using namespace Arithmetic_Tools;

        const uint8_t reverse_day = reverse(now.day.val);
        const uint8_t reverse_hour = reverse(now.hour.val);
        const uint8_t reverse_min = reverse(now.minute.val);

        // byte_0A:	bit 17-24	// year
        // byte_1A:	bit 25-32	// month + bit 0-2 day
        // byte_2A:	bit 33-40	// bit 3-5 day + weekday + bit 0-1 hour
        // byte_3A:	bit 41-48	// bit 2-5 hour + bit 0-3 minute
        // byte_4A:	bit 49-56	// bit 4-6 minute + 11110
        // byte_5A:	bit 57-59	// 011

        // bit A 17-24
        data.A.byte_0 = reverse(now.year.val);
        // bit A 25-32
        data.A.byte_1 = reverse(now.month.val)>>3 | reverse_day<<3;
        // bit A 33-40
        data.A.byte_2 = reverse_day>>5 | reverse(now.weekday.val)>>2 | reverse_hour<<4;
        // bit A 41-48
        data.A.byte_3 = reverse_hour>>4 | reverse_min<<3;
        // bit A 49-56
        data.A.byte_4 = reverse_min>>5 | 0b01111110<<3;
        // bit A 57-59
        data.A.byte_5 = 0b01111110>>5;

        // byte_0B:	bit 17-24   // 0
        // byte_1B:	bit 25-32	// 0
        // byte_2B:	bit 33-40	// 0
        // byte_3B:	bit 41-48	// 0
        // byte_4B:	bit 49-56	// 0000 + flags + parity
        // byte_5B:	bit 57-59	// flags + parity + 0

        data.B.byte_0 = 0;
        data.B.byte_1 = 0;
        data.B.byte_2 = 0;
        data.B.byte_3 = 0;


        // bit B 53-58 // flags and parity
        data.B.byte_4 = set_bit(0, 4, now.timezone_change_scheduled);
        data.B.byte_4 = set_bit(data.B.byte_4, 5, !parity(now.year.val));
        data.B.byte_4 = set_bit(data.B.byte_4, 6, !parity(now.day.val));
        data.B.byte_4 = set_bit(data.B.byte_4, 7, !parity(now.weekday.val));
        const uint8_t time_parity =	!(parity(now.hour.digit.hi) ^
                                    parity(now.hour.digit.lo) ^
                                    parity(now.minute.digit.hi) ^
                                    parity(now.minute.digit.lo));
        data.B.byte_5 = set_bit(0, 0, time_parity);
        data.B.byte_5 = set_bit(data.B.byte_5, 1, now.uses_summertime);
    }

    void debug(const MSF::time_data_t &clock) {
        using namespace Debug;

        Serial.print(F("  "));
        bcddigits(clock.year.val);
        Serial.print('.');
        bcddigits(clock.month.val);
        Serial.print('.');
        bcddigits(clock.day.val);
        Serial.print('(');
        bcddigit(clock.weekday.val);
        Serial.print(',');
        bcddigit(weekday(clock));
        Serial.print(')');
        bcddigits(clock.hour.val);
        Serial.print(':');
        bcddigits(clock.minute.val);
        Serial.print(':');
        if (clock.second < 10) {
            Serial.print('0');
        }
        Serial.print(clock.second, DEC);
        if (clock.uses_summertime) {
            Serial.print(F(" BST "));
        } else {
            Serial.print(F(" GMT "));
        }
        if (clock.timezone_change_scheduled) {
            Serial.print(F("time zone change scheduled"));
        }
    }

    void debug(const MSF::time_data_t &clock, const uint16_t cycles) {
        MSF::time_data_t local_clock = clock;
        MSF::time_data_t decoded_clock;

        Serial.print(F("M ???????? ???????? YYYYyyyy Mmmmm DDdddd www HHhhhh MMMmmmm 0 W PPPP S 0"));
        for (uint16_t second = 0; second < cycles; ++second) {
            switch (local_clock.second) {
            case  0: Serial.println(); break;
            case  1: case  9: case 17: case 25: case 30: case 36:
            case 39: case 45: case 52: case 53: case 58: case 59: Serial.print(' ');
            }

            const MSF::tick_t tick_data = get_current_signal(local_clock);
            Debug::debug_helper(tick_data);

            MSF_Naive_Bitstream_Decoder::set_bit(local_clock.second, tick_data, decoded_clock);

            advance_second(local_clock);

            if (local_clock.second == 0) {
                debug(decoded_clock);
            }
        }

        Serial.println();
        Serial.println();
    }
}

namespace MSF_Naive_Bitstream_Decoder {
    using namespace MSF;

    void set_bit(const uint8_t second, const uint8_t value, time_data_t &now) {
        // The naive value is a way to guess a value for unclean decoded data.
        // It is obvious that this is not necessarily a good value but better
        // than nothing.
        const bool naive_value_A = (value == A1_B0 || value == A1_B1 || value == undefined)? 1: 0;
        const bool naive_value_B = (value == A0_B1 || value == A1_B1 || value == undefined)? 1: 0;
        const bool is_value_bad = value != A0_B1 && value != A1_B1 && value != A0_B0 && value != A0_B1;

        now.second = second;

        switch (second) {
        case 16: now.year.val = 0x00; now.undefined_year_output = false; break;
        case 17: now.year.val += 0x80*naive_value_A; break;
        case 18: now.year.val += 0x40*naive_value_A; break;
        case 19: now.year.val += 0x20*naive_value_A; break;
        case 20: now.year.val += 0x10*naive_value_A; break;
        case 21: now.year.val +=  0x8*naive_value_A; break;
        case 22: now.year.val +=  0x4*naive_value_A; break;
        case 23: now.year.val +=  0x2*naive_value_A; break;
        case 24: now.year.val +=      naive_value_A;
                 now.month.val = 0x00; now.undefined_day_output = false; break;

        case 25: now.month.val += 0x10*naive_value_A; break;
        case 26: now.month.val +=  0x8*naive_value_A; break;
        case 27: now.month.val +=  0x4*naive_value_A; break;
        case 28: now.month.val +=  0x2*naive_value_A; break;
        case 29: now.month.val +=      naive_value_A;
                 now.day.val = 0x00; break;

        case 30: now.day.val += 0x20*naive_value_A; break;
        case 31: now.day.val += 0x10*naive_value_A; break;
        case 32: now.day.val +=  0x8*naive_value_A; break;
        case 33: now.day.val +=  0x4*naive_value_A; break;
        case 34: now.day.val +=  0x2*naive_value_A; break;
        case 35: now.day.val +=      naive_value_A;
                 now.weekday.val = 0x00; now.undefined_weekday_output = false; break;

        case 36: now.weekday.val += 0x4*naive_value_A; break;
        case 37: now.weekday.val += 0x2*naive_value_A; break;
        case 38: now.weekday.val +=     naive_value_A;
                 now.hour.val = 0; now.undefined_time_output = false; break;

        case 39: now.hour.val += 0x20*naive_value_A; break;
        case 40: now.hour.val += 0x10*naive_value_A; break;
        case 41: now.hour.val +=  0x8*naive_value_A; break;
        case 42: now.hour.val +=  0x4*naive_value_A; break;
        case 43: now.hour.val +=  0x2*naive_value_A; break;
        case 44: now.hour.val +=      naive_value_A;
                 now.minute.val = 0x00; break;

        case 45: now.minute.val += 0x40*naive_value_A; break;
        case 46: now.minute.val += 0x20*naive_value_A; break;
        case 47: now.minute.val += 0x10*naive_value_A; break;
        case 48: now.minute.val +=  0x8*naive_value_A; break;
        case 49: now.minute.val +=  0x4*naive_value_A; break;
        case 50: now.minute.val +=  0x2*naive_value_A; break;
        case 51: now.minute.val +=      naive_value_A; break;

        case 53: now.timezone_change_scheduled = naive_value_B;
                 now.undefined_timezone_change_scheduled_output = is_value_bad; break;

        case 58: now.uses_summertime = naive_value_B;
                 now.undefined_uses_summertime_output = is_value_bad; break;
        }
    }
}

namespace MSF_Flag_Decoder {

    int8_t timezone_change_scheduled;
    int8_t uses_summertime;
    int8_t year_parity;
    int8_t date_parity;
    int8_t weekday_parity;	
    int8_t time_parity;

    void setup() {
        uses_summertime = 0;
        timezone_change_scheduled = 0;
        year_parity = 0;
        date_parity = 0;
        weekday_parity = 0;
        time_parity = 0;
    }

    void cummulate(int8_t &average, bool count_up) {
        if (count_up) {
            average += (average < 127);
        } else {
            average -= (average > -127);
        }
    }

    void process_tick(const uint8_t current_second, const bool tick_value) {

        switch (current_second) {
        case 53: cummulate(timezone_change_scheduled, tick_value); break;
        case 54: cummulate(year_parity, tick_value); break;
        case 55: cummulate(date_parity, tick_value); break;
        case 56: cummulate(weekday_parity, tick_value); break;
        case 57: cummulate(time_parity, tick_value); break;
        case 58: cummulate(uses_summertime, tick_value); break;
        }
    }

    void reset_after_previous_hour() {
        // HH := hh+1
        // timezone_change_scheduled will be set from hh:01 to HH:00

        if (timezone_change_scheduled) {
            timezone_change_scheduled = 0;
            uses_summertime -= uses_summertime;
        }
    }

    void reset_before_new_day() {
        // date_parity will stay the same 00:00-23:59
        date_parity = 0;
        weekday_parity = 0;
    }

    bool get_uses_summertime() {
        return uses_summertime > 0;
    }

    bool get_timezone_change_scheduled() {
        return timezone_change_scheduled > 0;
    }


    void get_quality(uint8_t &uses_summertime_quality,
    uint8_t &timezone_change_scheduled_quality) {
        uses_summertime_quality = abs(uses_summertime);
        timezone_change_scheduled_quality = abs(timezone_change_scheduled);
    }

    void debug() {
        Serial.print(F("TZ change, TZ, Year parity: Date parity: Time parity: Weekday parity: "));
        Serial.print(timezone_change_scheduled, DEC);
        Serial.print(',');
        Serial.print(uses_summertime, DEC);
        Serial.print(',');
        Serial.println(year_parity, DEC);
        Serial.print(',');
        Serial.println(date_parity, DEC);
        Serial.print(',');
        Serial.println(time_parity, DEC);
        Serial.print(',');
        Serial.println(weekday_parity);
    }
}

namespace MSF_Decade_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t decade_data;

        switch (current_second) {
        case 17: decade_data.val += 0x08*tick_value; break;
        case 18: decade_data.val += 0x04*tick_value; break;
        case 19: decade_data.val += 0x02*tick_value; break;
        case 20: decade_data.val +=      tick_value;
            hamming_binning<RaidoClock_Decade_Decoder::decade_bins, 4, false>(RaidoClock_Decade_Decoder::bins, decade_data); break;

        case 21: compute_max_index(RaidoClock_Decade_Decoder::bins);
            // fall through on purpose
        default: decade_data.val = 0;
        }
    }
}

namespace MSF_Year_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t year_data;

        switch (current_second) {
        case 21: year_data.val +=  0x8*tick_value; break;
        case 22: year_data.val +=  0x4*tick_value; break;
        case 23: year_data.val +=  0x2*tick_value; break;
        case 24: year_data.val +=      tick_value;
            hamming_binning<RadioClock_Year_Decoder::year_bins, 4, false>(RadioClock_Year_Decoder::bins, year_data); break;

        case 25: compute_max_index(RadioClock_Year_Decoder::bins);
            // fall through on purpose
        default: year_data.val = 0;
        }

        MSF_Decade_Decoder::process_tick(current_second, tick_value);
    }
}

namespace MSF_Month_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t month_data;

        switch (current_second) {
        case 25: month_data.val += 0x10*tick_value; break;
        case 26: month_data.val +=  0x8*tick_value; break;
        case 27: month_data.val +=  0x4*tick_value; break;
        case 28: month_data.val +=  0x2*tick_value; break;
        case 29: month_data.val +=      tick_value;
            hamming_binning<RadioClock_Month_Decoder::month_bins, 5, false>(RadioClock_Month_Decoder::bins, month_data); break;

        case 30: compute_max_index(RadioClock_Month_Decoder::bins);
            // fall through on purpose
        default: month_data.val = 0;
        }
    }
}

namespace MSF_Day_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t day_data;

        switch (current_second) {
        case 30: day_data.val += 0x20*tick_value; break;
        case 31: day_data.val += 0x10*tick_value; break;
        case 32: day_data.val +=  0x8*tick_value; break;
        case 33: day_data.val +=  0x4*tick_value; break;
        case 34: day_data.val +=  0x2*tick_value; break;
        case 35: day_data.val +=      tick_value;
            hamming_binning<RadioClock_Day_Decoder::day_bins, 6, false>(RadioClock_Day_Decoder::bins, day_data); break;
        case 36: compute_max_index(RadioClock_Day_Decoder::bins);
            // fall through on purpose
        default: day_data.val = 0;
        }
    }
}

namespace MSF_Weekday_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t weekday_data;

        switch (current_second) {
        case 36: weekday_data.val +=  0x4*tick_value; break;
        case 37: weekday_data.val +=  0x2*tick_value; break;
        case 38: weekday_data.val +=      tick_value;
            hamming_binning<RadioClock_Weekday_Decoder::weekday_bins, 3, false>(RadioClock_Weekday_Decoder::bins, weekday_data); break;
        case 39: compute_max_index(RadioClock_Weekday_Decoder::bins);
            // fall through on purpose
        default: weekday_data.val = 0;
        }
    }
}

namespace MSF_Hour_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;			

        static BCD::bcd_t hour_data;

        switch (current_second) {
        case 39: hour_data.val += 0x20*tick_value; break;
        case 40: hour_data.val += 0x10*tick_value; break;
        case 41: hour_data.val +=  0x8*tick_value; break;
        case 42: hour_data.val +=  0x4*tick_value; break;
        case 43: hour_data.val +=  0x2*tick_value; break;
        case 44: hour_data.val +=      tick_value;
            hamming_binning<RadioClock_Hour_Decoder::hour_bins, 6, false>(RadioClock_Hour_Decoder::bins, hour_data); break;

        case 45: compute_max_index(RadioClock_Hour_Decoder::bins);
            // fall through on purpose
        default: hour_data.val = 0;
        }
    }
}

namespace MSF_Minute_Decoder {
    void process_tick(const uint8_t current_second, const bool tick_value) {
        using namespace Hamming;

        static BCD::bcd_t minute_data;

        switch (current_second) {
        case 45: minute_data.val += 0x40*tick_value; break;
        case 46: minute_data.val += 0x20*tick_value; break;
        case 47: minute_data.val += 0x10*tick_value; break;
        case 48: minute_data.val +=  0x8*tick_value; break;
        case 49: minute_data.val +=  0x4*tick_value; break;
        case 50: minute_data.val +=  0x2*tick_value; break;
        case 51: minute_data.val +=      tick_value;
            hamming_binning<RadioClock_Minute_Decoder::minute_bins, 7, false>(RadioClock_Minute_Decoder::bins, minute_data); break;
        case 52: compute_max_index(RadioClock_Minute_Decoder::bins);
            // fall through on purpose
        default: minute_data.val = 0;
        }
    }
}

namespace MSF_Second_Decoder {
    using namespace MSF;

    // this is a trick threshold
    //    lower it to get a faster second lock
    //    but then risk to garble the successive stages during startup
    //    --> too low and total startup time will increase
    const uint8_t lock_threshold = 12;

    serialized_clock_stream convolution_kernel;
    // used to determine how many of the predicted bits are actually observed,
    // also used to indicate if convolution is already applied
    const uint8_t convolution_binning_not_ready = 0xff;
    uint8_t prediction_match = convolution_binning_not_ready;
    uint8_t buffered_match = convolution_binning_not_ready;

    uint8_t get_prediction_match() {
        return buffered_match;
    };

    void set_convolution_time(const MSF::time_data_t &now) {
        MSF::time_data_t convolution_clock = now;

        // we are always decoding the data for the NEXT minute
        MSF_Encoder::advance_minute(convolution_clock);

        // the convolution kernel shall have proper flag settings
        MSF_Encoder::autoset_control_bits(convolution_clock);

        MSF_Encoder::get_serialized_clock_stream(convolution_clock, convolution_kernel);
        prediction_match = 0;
    }

    void convolution_binning(const uint8_t tick_data) {
        using namespace Arithmetic_Tools;

        // determine sync lock
        if (RadioClock_Second_Decoder::bins.max - RadioClock_Second_Decoder::bins.noise_max <= lock_threshold || get_second() == 3) {
            // after a lock is acquired this happens only once per minute and it is
            // reasonable cheap to process,
            //
            // that is: after we have a "lock" this will be processed whenever
            // the sync mark was detected

            Hamming::compute_max_index(RadioClock_Second_Decoder::bins);

            const uint8_t convolution_weight = 50;
            if (RadioClock_Second_Decoder::bins.max > 255-convolution_weight) {
                // If we know we can not raise the maximum any further we
                // will lower the noise floor instead.
                for (uint8_t bin_index = 0; bin_index < RadioClock_Second_Decoder::seconds_per_minute; ++bin_index) {
                    bounded_decrement<convolution_weight>(RadioClock_Second_Decoder::bins.data[bin_index]);
                }
                RadioClock_Second_Decoder::bins.max -= convolution_weight;
                bounded_decrement<convolution_weight>(RadioClock_Second_Decoder::bins.noise_max);
            }
            buffered_match = prediction_match;
        }

        if (tick_data == min_marker) {
            bounded_increment<6>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
            if (RadioClock_Second_Decoder::bins.tick == RadioClock_Second_Decoder::bins.max_index) {
                prediction_match += 6;
            }
        } else if (tick_data == A0_B0 || tick_data == A0_B1 || tick_data == A1_B0 || tick_data == A1_B1) {
            for (uint8_t current_byte_index = 0, current_byte_A_value = convolution_kernel.A.byte_0, current_byte_B_value = convolution_kernel.B.byte_0; current_byte_index < 6; current_byte_index++, current_byte_A_value = (&(convolution_kernel.A.byte_0))[current_byte_index], current_byte_B_value = (&(convolution_kernel.B.byte_0))[current_byte_index]) {
                // bit 17 is where the convolution kernel starts
                for (uint8_t current_bit_index = 0, bin = bin>16? bin-17: bin + RadioClock_Second_Decoder::seconds_per_minute-17; current_bit_index < 8 && !(current_byte_index == 5 && current_bit_index > 2); current_bit_index++, current_byte_A_value >>= 1, current_byte_B_value >>= 1, bin = bin>0? bin-1: RadioClock_Second_Decoder::seconds_per_minute-1) {
                    const bool is_match = (tick_data == (current_byte_A_value<<1 & 2) | (current_byte_B_value & 1));
                    RadioClock_Second_Decoder::bins.data[bin] += is_match;

                    if (bin == RadioClock_Second_Decoder::bins.max_index) {
                        prediction_match += is_match;
                    }
                }
            }
        }

        RadioClock_Second_Decoder::bins.tick = RadioClock_Second_Decoder::bins.tick<RadioClock_Second_Decoder::seconds_per_minute-1? RadioClock_Second_Decoder::bins.tick+1: 0;
    }

    uint8_t get_previous_n_tick(uint8_t n) {
        return (RadioClock_Second_Decoder::bins.tick > (n-1) ? RadioClock_Second_Decoder::bins.tick - n: RadioClock_Second_Decoder::bins.tick + RadioClock_Second_Decoder::seconds_per_minute - n);
    }

    void sync_mark_binning(const uint8_t tick_data) {
        // We use a binning approach to find out the proper phase.
        // The goal is to localize the sync_mark. Due to noise
        // there may be wrong marks of course. The idea is to not
        // only look at the statistics of the marks but to exploit
        // additional data properties:
        //
        // Bit A and B position 52 after a proper sync are both 0.
        // Bit A and B position 59 after a proper sync are both 0.
        // Bit A positions 1-16 after a proper sync are all 0.
        // Bit B positions 17-52 after a proper sync are all 0.
        //
        // The binning will work as follows:
        //   1) A sync mark will score +10 points for the current bin
        //   2) A "1" in bit A will score +1 points 53-58 bins back
        //  2b) A "0" in bit A will score +1 points for bins 1-16 back as well as 52 and 59 back
        //   3) A "0" in bit B will score +1 points for bins 17-52 and 59.
        //   4) An undefined value will score -2 point for the current bin
        //   5) Scores have an upper limit of 255 and a lower limit of 0.
        //
        // Summary: sync mark earns 10 points, a 0 in position 52 and 59 earn 1 bonus point
        //          anything that allows to infer that any of the "connected" positions is not a sync will remove 2 points
        //
        // It follows that the score of a sync mark (during good reception)
        // may move up/down the whole scale in slightly below 64 minutes.
        // If the receiver should glitch for whatever reason this implies
        // that the clock will take about 33 minutes to recover the proper
        // phase (during phases of good reception). During bad reception things
        // are more tricky.
        using namespace Arithmetic_Tools;

        switch (tick_data) {
            case min_marker:
                bounded_increment<10>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            case A0_B0:
                for (uint8_t i=1; i<=51; ++i) {
                    bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(i)]);
                }
                bounded_increment<2>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(52)]);
                bounded_increment<2>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(59)]);
                bounded_decrement<60>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            case A0_B1:
                for (uint8_t i=1; i<=16; ++i) {
                    bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(i)]);
                }
                bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(52)]);
                bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(59)]);
                bounded_decrement<60>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            case A1_B0:
                for (uint8_t i=17; i<=59; ++i) {
                    bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(i)]);
                }
                bounded_decrement<60>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            case A1_B1:
                for (uint8_t i=53; i<=58; ++i) {
                    bounded_increment<1>(RadioClock_Second_Decoder::bins.data[get_previous_n_tick(i)]);
                }
                bounded_decrement<60>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            case undefined:
                bounded_decrement<55>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
                break;

            default:
                bounded_decrement<60>(RadioClock_Second_Decoder::bins.data[RadioClock_Second_Decoder::bins.tick]);
        }

        RadioClock_Second_Decoder::bins.tick = RadioClock_Second_Decoder::bins.tick<RadioClock_Second_Decoder::seconds_per_minute-1? RadioClock_Second_Decoder::bins.tick+1: 0;

        // determine sync lock
        if (RadioClock_Second_Decoder::bins.max - RadioClock_Second_Decoder::bins.noise_max <=lock_threshold || get_second() == 3) {

            // after a lock is acquired this happens only once per minute and it is
            // reasonable cheap to process,
            //
            // that is: after we have a "lock" this will be processed whenever
            // the sync mark was detected

            Hamming::compute_max_index(RadioClock_Second_Decoder::bins);
        }
    }

    uint8_t get_second() {
        if (RadioClock_Second_Decoder::bins.max - RadioClock_Second_Decoder::bins.noise_max >= lock_threshold) {
            // at least one sync mark and a 0 and a 1 seen
            // the threshold is tricky:
            //   higher --> takes longer to acquire an initial lock, but higher probability of an accurate lock
            //
            //   lower  --> higher probability that the lock will oscillate at the beginning
            //              and thus spoil the downstream stages

            // we have to subtract 1 seconds because the seconds already advanced by 1 tick

            uint8_t second = 2*RadioClock_Second_Decoder::seconds_per_minute + RadioClock_Second_Decoder::bins.tick - 1 - RadioClock_Second_Decoder::bins.max_index;
            while (second >= RadioClock_Second_Decoder::seconds_per_minute) { second-= RadioClock_Second_Decoder::seconds_per_minute; }

            return second;
        } else {
            return 0xff;
        }
    }

    void process_single_tick_data(const MSF::tick_t tick_data) {
        if (prediction_match == convolution_binning_not_ready) {
            sync_mark_binning(tick_data);
        } else {
            convolution_binning(tick_data);
        }
    }

    void debug() {
        static uint8_t prev_tick;

        if (prev_tick == RadioClock_Second_Decoder::bins.tick) {
            return;
        } else {
            prev_tick = RadioClock_Second_Decoder::bins.tick;

            Serial.print(F("second: "));
            Serial.print(get_second(), DEC);
            Serial.print(F(" Sync mark index "));
            Hamming::debug(RadioClock_Second_Decoder::bins);
            Serial.print(F("Prediction Match: "));
            Serial.println(prediction_match, DEC);
            Serial.println();
        }
    }
}

namespace MSF_Local_Clock {
    MSF::output_handler_t output_handler = 0;
    MSF::time_data_t local_clock_time;
    volatile bool second_toggle;
    uint16_t tick = 0;

    // This will take more than 100 years to overflow.
    // An overflow would indicate that the clock is
    // running for >100 years without a sync.
    // --> It is pointless to handle this.
    uint32_t unlocked_seconds = 0;

    void setup() {
        MSF_Encoder::reset(local_clock_time);
    }

    void read_current_time(MSF::time_data_t &now) {
        const uint8_t prev_SREG = SREG;
        cli();

        now = local_clock_time;

        SREG = prev_SREG;
    }

    void get_current_time(MSF::time_data_t &now) {
        for (bool stopper = second_toggle; stopper == second_toggle; ) {
            // wait for second_toggle to toggle
            // that is wait for decoded time to be ready
        }
        read_current_time(now);
    }

    void process_1_Hz_tick(const MSF::time_data_t &decoded_time) {
        uint8_t quality_factor = MSF_Clock_Controller::get_overall_quality_factor();

        if (quality_factor > 1) {
            if (RadioClock_Local_Clock::clock_state != RadioClock::synced) {
                RadioClock_Controller::sync_achieved_event_handler();
                RadioClock_Local_Clock::clock_state = RadioClock::synced;
            }
        } else if (RadioClock_Local_Clock::clock_state == RadioClock::synced) {
            RadioClock_Controller::sync_lost_event_handler();
            RadioClock_Local_Clock::clock_state = RadioClock::locked;
        }

        while (true) {
            switch (RadioClock_Local_Clock::clock_state) {
            case RadioClock::useless: {
                    if (quality_factor > 0) {
                        RadioClock_Local_Clock::clock_state = RadioClock::dirty;
                        break;  // goto dirty state
                    } else {
                        second_toggle = !second_toggle;
                        return;
                    }
                }

            case RadioClock::dirty: {
                    if (quality_factor == 0) {
                        RadioClock_Local_Clock::clock_state = RadioClock::useless;
                        second_toggle = !second_toggle;
                        MSF_Encoder::reset(local_clock_time);
                        return;
                    } else {
                        tick = 0;
                        local_clock_time = decoded_time;
                        MSF_Clock_Controller::flush(decoded_time);
                        second_toggle = !second_toggle;
                        return;
                    }
                }

            case RadioClock::synced: {
                    tick = 0;
                    local_clock_time = decoded_time;
                    MSF_Clock_Controller::flush(decoded_time);
                    second_toggle = !second_toggle;
                    return;
                }

            case RadioClock::locked: {
                    if ( RadioClock_Demodulator::get_quality_factor() > 10) {
                        // autoset_control_bits is not required because
                        // advance_second will call this internally anyway
                        //MSF_Encoder::autoset_control_bits(local_clock_time);
                        MSF_Encoder::advance_second(local_clock_time);
                        MSF_Clock_Controller::flush(local_clock_time);
                        tick = 0;
                        second_toggle = !second_toggle;
                        return;
                    } else {
                        RadioClock_Local_Clock::clock_state = RadioClock::unlocked;
                        RadioClock_Controller::phase_lost_event_handler();
                        unlocked_seconds = 0;
                        return;
                    }
                }

            case RadioClock::unlocked: {
                    if (RadioClock_Demodulator::get_quality_factor() > 10) {
                        // Quality is somewhat reasonable again, check
                        // if the phase offset is in reasonable bounds.
                        if (200 < tick && tick < 800) {
                            // Deviation of local phase vs. decoded phase exceeds 200 ms.
                            // So something is not OK. We can not relock.
                            // On the other hand we are still below max_unlocked_seconds.
                            // --> Stay in unlocked mode.
                            return;
                        } else {
                            // Phase drift was below 200 ms and clock was not unlocked
                            // for max_unlocked_seconds. So we know that we are close
                            // enough to the proper time.
                            // We ignore this issue as it is not worse than running in
                            // free mode.
                            RadioClock_Local_Clock::clock_state = RadioClock::locked;
                            if (tick < 200) {
                                // time output was handled at most 200 ms before
                                tick = 0;
                                return;
                            } else {
                                break;  // goto locked state
                            }
                        }
                    } else {
                        // quality is still poor, we stay in unlocked mode
                        return;
                    }
                }

            case RadioClock::free: {
                    return;
                }
            }
        }
    }

    void process_1_kHz_tick() {
        ++tick;

        if (RadioClock_Local_Clock::clock_state == RadioClock::synced || RadioClock_Local_Clock::clock_state == RadioClock::locked) {
            // the important part is 150 < 200,
            // otherwise it will fall through to free immediately after changing to unlocked
            if (tick >= 1150) {
                // The 1 Hz pulse was locked but now
                // it is definitely out of phase.
                unlocked_seconds = 1;

                // 1 Hz tick missing for more than 1200ms
                RadioClock_Local_Clock::clock_state = RadioClock::unlocked;
                RadioClock_Controller::phase_lost_event_handler();
            }
        }

        if (RadioClock_Local_Clock::clock_state == RadioClock::unlocked || RadioClock_Local_Clock::clock_state == RadioClock::free) {
            if (tick >= 1000) {
                tick -= 1000;
                // autoset_control_bits is not required because
                // advance_second will call this internally anyway
                //MSF_Encoder::autoset_control_bits(local_clock_time);
                MSF_Encoder::advance_second(local_clock_time);
                MSF_Clock_Controller::flush(local_clock_time);
                second_toggle = !second_toggle;

                ++unlocked_seconds;
                if (unlocked_seconds > RadioClock_Local_Clock::max_unlocked_seconds) {
                    RadioClock_Local_Clock::clock_state = RadioClock::free;
                }
            }
        }
    }


    void set_output_handler(const MSF::output_handler_t new_output_handler) {
        output_handler = new_output_handler;
    }

    void debug() {
        Serial.print(F("Clock state: "));
        switch (RadioClock_Local_Clock::clock_state) {
        case RadioClock::useless:  Serial.println(F("useless"));  break;
        case RadioClock::dirty:    Serial.println(F("dirty"));    break;
        case RadioClock::free:     Serial.println(F("free"));     break;
        case RadioClock::unlocked: Serial.println(F("unlocked")); break;
        case RadioClock::locked:   Serial.println(F("locked"));   break;
        case RadioClock::synced:   Serial.println(F("synced"));   break;
        default:       Serial.println(F("undefined"));
        }
        Serial.print(F("Tick: "));
        Serial.println(tick);
    }
}

namespace MSF_Clock_Controller {
    MSF_Clock::output_handler_t output_handler = 0;
    MSF::time_data_t decoded_time;

    void get_current_time(MSF::time_data_t &now) {
        RadioClock_Controller::auto_persist();
        MSF_Local_Clock::get_current_time(now);
    }

    void read_current_time(MSF::time_data_t &now) {
        MSF_Local_Clock::read_current_time(now);
    }

    void set_MSF_encoder(MSF::time_data_t &now) {
        using namespace MSF_Second_Decoder;
        using namespace MSF_Flag_Decoder;

        now.second  = get_second();
        now.minute  = RadioClock_Minute_Decoder::get_minute();
        now.hour    = RadioClock_Hour_Decoder::get_hour();
        now.weekday = RadioClock_Weekday_Decoder::get_weekday();
        now.day     = RadioClock_Day_Decoder::get_day();
        now.month   = RadioClock_Month_Decoder::get_month();
        now.year    = RadioClock_Year_Decoder::get_year();

        now.timezone_change_scheduled      = get_timezone_change_scheduled();
        now.uses_summertime                = get_uses_summertime();
    }

    void flush() {
        // This is called "at the end of each second / before the next second begins."
        // The call is triggered by the decoder stages. Thus it flushes the current
        // decoded time. If the decoders are out of sync this may not be
        // called at all.

        MSF::time_data_t now;
        MSF::time_data_t now_1;

        set_MSF_encoder(now);
        now_1 = now;

        MSF_Encoder::advance_second(now);
        MSF_Encoder::autoset_control_bits(now);

        decoded_time.second = now.second;
        if (now.second == 0) {
            // the decoder will always decode the data for the NEXT minute
            // thus we have to keep the data of the previous minute
            decoded_time = now_1;
            decoded_time.second = 0;

            if (now.minute.val == 0x01) {
                // We are at the last moment of the "old" hour.
                // The data for the first minute of the new hour is now complete.
                // The point is that we can reset the flags only now.
                MSF_Flag_Decoder::reset_after_previous_hour();

                now.uses_summertime = MSF_Flag_Decoder::get_uses_summertime();
                now.timezone_change_scheduled = MSF_Flag_Decoder::get_timezone_change_scheduled();

                MSF_Encoder::autoset_control_bits(now);
                decoded_time.uses_summertime                = now.uses_summertime;
                decoded_time.timezone_change_scheduled      = now.timezone_change_scheduled;
            }

            if (now.hour.val == 0x23 && now.minute.val == 0x59) {
                // We are now starting to process the data for the
                // new day. Thus the parity flag is of little use anymore.

                // We could be smarter though and merge synthetic parity information with measured historic values.

                // that is: advance(now), see if parity changed, if not so --> fine, otherwise change sign of flag
                MSF_Flag_Decoder::reset_before_new_day();
            }
        }

        // pass control to local clock
        MSF_Local_Clock::process_1_Hz_tick(decoded_time);
    }

    void flush(const MSF::time_data_t &decoded_time) {
        // This is the callback for the "local clock".
        // It will be called once per second.

        // It ensures that the local clock is decoupled
        // from things like "output handling".

        // frequency control must be handled before output handling, otherwise
        // output handling might introduce undesirable jitter to frequency control
        MSF_Frequency_Control::process_1_Hz_tick(decoded_time);

        if (output_handler) {
            MSF_Clock::time_t time;

            time.second                    = BCD::int_to_bcd(decoded_time.second);
            time.minute                    = decoded_time.minute;
            time.hour                      = decoded_time.hour;
            time.weekday                   = decoded_time.weekday;
            time.day                       = decoded_time.day;
            time.month                     = decoded_time.month;
            time.year                      = decoded_time.year;
            time.uses_summertime           = decoded_time.uses_summertime;
            time.timezone_change_scheduled = decoded_time.timezone_change_scheduled;
            output_handler(time);
        }

        if (decoded_time.second == 15 && RadioClock_Local_Clock::clock_state != RadioClock::useless
                && RadioClock_Local_Clock::clock_state != RadioClock::dirty
                ) {
            MSF_Second_Decoder::set_convolution_time(decoded_time);
        }
    }

    void process_1_kHz_tick_data(const bool sampled_data) {
        MSF_Demodulator::detector(sampled_data);
        MSF_Local_Clock::process_1_kHz_tick();
        RadioClock_Frequency_Control::process_1_kHz_tick();
    }

    void set_output_handler(const MSF_Clock::output_handler_t new_output_handler) {
        output_handler = new_output_handler;
    }

    void get_quality(clock_quality_t &clock_quality) {
        RadioClock_Demodulator::get_quality(clock_quality.phase.lock_max, clock_quality.phase.noise_max);
        RadioClock_Second_Decoder::get_quality(clock_quality.second);
        RadioClock_Minute_Decoder::get_quality(clock_quality.minute);
        RadioClock_Hour_Decoder::get_quality(clock_quality.hour);
        RadioClock_Day_Decoder::get_quality(clock_quality.day);
        RadioClock_Weekday_Decoder::get_quality(clock_quality.weekday);
        RadioClock_Month_Decoder::get_quality(clock_quality.month);
        RadioClock_Year_Decoder::get_quality(clock_quality.year);

        MSF_Flag_Decoder::get_quality(clock_quality.uses_summertime_quality,
        clock_quality.timezone_change_scheduled_quality);
    }

    uint8_t get_overall_quality_factor() {
        using namespace Arithmetic_Tools;

        uint8_t quality_factor = RadioClock_Demodulator::get_quality_factor();
        minimize(quality_factor, RadioClock_Second_Decoder::get_quality_factor());
        minimize(quality_factor, RadioClock_Minute_Decoder::get_quality_factor());
        minimize(quality_factor, RadioClock_Hour_Decoder::get_quality_factor());

        uint8_t date_quality_factor = RadioClock_Day_Decoder::get_quality_factor();
        minimize(date_quality_factor, RadioClock_Month_Decoder::get_quality_factor());
        minimize(date_quality_factor, RadioClock_Year_Decoder::get_quality_factor());

        const uint8_t weekday_quality_factor = RadioClock_Weekday_Decoder::get_quality_factor();
        if (date_quality_factor > 0 && weekday_quality_factor > 0) {

            MSF::time_data_t now;
            now.second  = MSF_Second_Decoder::get_second();
            now.minute  = RadioClock_Minute_Decoder::get_minute();
            now.hour    = RadioClock_Hour_Decoder::get_hour();
            now.day     = RadioClock_Day_Decoder::get_day();
            now.weekday = RadioClock_Weekday_Decoder::get_weekday();
            now.month   = RadioClock_Month_Decoder::get_month();
            now.year    = RadioClock_Year_Decoder::get_year();

            BCD::bcd_t weekday = MSF_Encoder::bcd_weekday(now);
            if (weekday.val == 0) {
                weekday.val = 7;
            }
            if (now.weekday.val == weekday.val) {
                date_quality_factor += 1;
            } else if (date_quality_factor <= weekday_quality_factor) {
                date_quality_factor = 0;
            }
        }

        minimize(quality_factor, date_quality_factor);

        return quality_factor;
    };

    uint8_t get_prediction_match() {
        return MSF_Second_Decoder::get_prediction_match();
    }

    void debug() {
        clock_quality_t clock_quality;
        get_quality(clock_quality);

        RadioClock_Controller::clock_quality_factor_t clock_quality_factor;
        RadioClock_Controller::get_quality_factor(clock_quality_factor);

        Serial.print(F("Quality (p,s,m,h,wd,d,m,y,st,tz,pm): "));
        Serial.print(get_overall_quality_factor(), DEC);
        Serial.print(F(" ("));
        Serial.print(clock_quality.phase.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.phase.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.phase, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.second.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.second.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.second, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.minute.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.minute.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.minute, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.hour.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.hour.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.hour, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.weekday.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.weekday.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.weekday, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.day.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.day.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.day, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.month.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.month.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.month, DEC);
        Serial.print(')');

        Serial.print('(');
        Serial.print(clock_quality.year.lock_max, DEC);
        Serial.print('-');
        Serial.print(clock_quality.year.noise_max, DEC);
        Serial.print(':');
        Serial.print(clock_quality_factor.year, DEC);
        Serial.print(')');

        Serial.print(clock_quality.uses_summertime_quality, DEC);
        Serial.print(',');
        Serial.print(clock_quality.timezone_change_scheduled_quality, DEC);

        Serial.print(',');
        Serial.println(get_prediction_match(), DEC);
    }

    void process_single_tick_data(const MSF::tick_t tick_data) {
        using namespace MSF;

        time_data_t now;
        set_MSF_encoder(now);

        MSF_Encoder::advance_second(now);
        MSF_Second_Decoder::process_single_tick_data(tick_data);

        if (now.second == 0) {
            RadioClock_Minute_Decoder::advance_minute();
            if (now.minute.val == 0x00) {

                // "while" takes automatically care of timezone change
                while (RadioClock_Hour_Decoder::get_hour().val <= 0x23 && RadioClock_Hour_Decoder::get_hour().val != now.hour.val) { RadioClock_Hour_Decoder::advance_hour(); }

                if (now.hour.val == 0x00) {
                    if (RadioClock_Weekday_Decoder::get_weekday().val <= 0x07) { RadioClock_Weekday_Decoder::advance_weekday(); }

                    // "while" takes automatically care of different month lengths
                    while (RadioClock_Day_Decoder::get_day().val <= 0x31 && RadioClock_Day_Decoder::get_day().val != now.day.val) { RadioClock_Day_Decoder::advance_day(); }

                    if (now.day.val == 0x01) {
                        if (RadioClock_Month_Decoder::get_month().val <= 0x12) { RadioClock_Month_Decoder::advance_month(); }
                        if (now.month.val == 0x01) {
                            if (now.year.val <= 0x99) { RadioClock_Year_Decoder::advance_year(); }
                        }
                    }
                }
            }
        }
        const bool tick_value_A = (tick_data == A1_B0 || tick_data == A1_B1 || tick_data == undefined)? 1: 0;
        const bool tick_value_B = (tick_data == A0_B1 || tick_data == A1_B1 || tick_data == undefined)? 1: 0;

        MSF_Flag_Decoder::process_tick(now.second, tick_value_B);
        MSF_Minute_Decoder::process_tick(now.second, tick_value_A);
        MSF_Hour_Decoder::process_tick(now.second, tick_value_A);
        MSF_Weekday_Decoder::process_tick(now.second, tick_value_A);
        MSF_Day_Decoder::process_tick(now.second, tick_value_A);
        MSF_Month_Decoder::process_tick(now.second, tick_value_A);
        MSF_Year_Decoder::process_tick(now.second, tick_value_A);
    }
}

namespace MSF_Demodulator {
    const uint16_t num_interesting_bins = RadioClock_Demodulator::bins_per_500ms;

    void decode_interesting(const bool input, const uint16_t bins_to_go) {
        // will be called for each bin during the "interesting" period

        static uint8_t count = 0;
        static uint8_t decoded_data = 4;
        static bool sec_marker_seen = false;

        // pass control further
        // decoded_data: 0 --> A0_B0,
        //               1 --> A0_B1,
        //               2 --> A1_B0,
        //               3 --> A1_B1,
        //               4 --> undefined,
        //               5 --> min_marker
        count += input;

        switch(bins_to_go) {
        if (!sec_marker_seen) {
            // Second marker 100ms in length (1ms - 100ms after start of second)
            case num_interesting_bins - RadioClock_Demodulator::bins_per_100ms:
                sec_marker_seen = (count > RadioClock_Demodulator::bins_per_50ms);
                count = 0;
                break;

        } else {

            // Bit A 100ms in length after second marker (101ms - 200ms after start of second)
            case num_interesting_bins - RadioClock_Demodulator::bins_per_100ms * 2:
                decoded_data = (count > RadioClock_Demodulator::bins_per_50ms) << 1;
                count = 0;
                break;

            // Bit B 100ms in length after bit A (201ms - 300ms after start of second)
            case num_interesting_bins - RadioClock_Demodulator::bins_per_100ms * 3:
                decoded_data += (count > RadioClock_Demodulator::bins_per_50ms);
                count = 0;
                break;

            // this case reads 301ms - 400ms after start of second
            case num_interesting_bins - RadioClock_Demodulator::bins_per_100ms * 4:
                if (count > RadioClock_Demodulator::bins_per_50ms) {
                    decoded_data += (decoded_data == 3);
                }
                count = 0;
                break;

            // Minute marker is 500ms in length, this case reads 401ms - 500ms after start of second
            case 0:

                if (count > RadioClock_Demodulator::bins_per_50ms) {
                    decoded_data = 4 + (decoded_data == 4);
                }
                MSF_Clock_Controller::process_single_tick_data((MSF::tick_t)decoded_data);
                count = 0;
                sec_marker_seen = false;
                decoded_data = 4;
            }
        }
    }

    void detector_stage_2(const bool input) {
        const uint8_t current_bin = RadioClock_Demodulator::bins.tick;

        const uint8_t threshold = 30;

        if (RadioClock_Demodulator::bins.max-RadioClock_Demodulator::bins.noise_max < threshold ||
                RadioClock_Demodulator::wrap(RadioClock_Demodulator::bin_count + current_bin - RadioClock_Demodulator::bins.max_index) == 53) {
            // Phase detection far enough out of phase from anything that
            // might consume runtime otherwise.
            MSF_Demodulator::phase_detection();
        }

        static uint16_t bins_to_process = 0;
        if (bins_to_process == 0) {
            if (RadioClock_Demodulator::wrap((RadioClock_Demodulator::bin_count + current_bin - RadioClock_Demodulator::bins.max_index)) <= RadioClock_Demodulator::bins_per_100ms ||   // current_bin at most 100ms after phase_bin
                    RadioClock_Demodulator::wrap((RadioClock_Demodulator::bin_count + RadioClock_Demodulator::bins.max_index - current_bin)) <= RadioClock_Demodulator::bins_per_10ms ) {   // current bin at most 10ms before phase_bin
                // if phase bin varies too much during one period we will always be screwed in may ways...

                // last 10ms of current second
                MSF_Clock_Controller::flush();

                // start processing of bins
                bins_to_process = num_interesting_bins;
            }
        }

        if (bins_to_process > 0) {
            --bins_to_process;

            // this will be called for each bin in the "interesting" 500ms
            // this is also a good place for a "monitoring hook"
            decode_interesting(input, bins_to_process);
        }
    }

    void detector(const bool sampled_data) {
        static uint8_t current_sample = 0;
        static uint8_t average = 0;

        // detector stage 0: average 10 samples (per bin)
        average += sampled_data;

        if (++current_sample >= RadioClock_Demodulator::samples_per_bin) {
            // once all samples for the current bin are captured the bin gets updated
            // that is each 10ms control is passed to stage 1
            const bool input = (average> RadioClock_Demodulator::samples_per_bin/2);

            RadioClock_Demodulator::phase_binning(input);

            detector_stage_2(input);

            average = 0;
            current_sample = 0;
        }
    }

    void phase_detection() {
        // We will compute the integrals over 200ms.
        // The integrals is used to find the window of maximum signal strength.
        uint32_t integral = 0;

        for (uint16_t bin = 0; bin < RadioClock_Demodulator::bins_per_100ms; ++bin) {
            integral += ((uint32_t)RadioClock_Demodulator::bins.data[bin])*59;
        }

        for (uint16_t bin = RadioClock_Demodulator::bins_per_100ms; bin < RadioClock_Demodulator::bins_per_200ms; ++bin) {
            integral += (uint32_t)RadioClock_Demodulator::bins.data[bin]*34;
        }

        for (uint16_t bin = RadioClock_Demodulator::bins_per_200ms; bin < RadioClock_Demodulator::bins_per_300ms; ++bin) {
            integral += (uint32_t)RadioClock_Demodulator::bins.data[bin]<<3;
        }

        RadioClock_Demodulator::bins.max = 0;
        RadioClock_Demodulator::bins.max_index = 0;
        for (uint16_t bin = 0; bin < RadioClock_Demodulator::bin_count; ++bin) {
            if (integral > RadioClock_Demodulator::bins.max) {
                RadioClock_Demodulator::bins.max = integral;
                RadioClock_Demodulator::bins.max_index = bin;
            }

            integral -= (uint32_t)RadioClock_Demodulator::bins.data[bin]<<1;
            integral += (uint32_t)(RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(bin + RadioClock_Demodulator::bins_per_100ms)]*(59-34) +
            RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(bin + RadioClock_Demodulator::bins_per_200ms)]*(34-8) +
            RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(bin + RadioClock_Demodulator::bins_per_300ms)]<<3);
        }

        // max_index indicates the position of the 200ms second signal window.
        // Now how can we estimate the noise level? This is very tricky because
        // averaging has already happened to some extend.

        // The issue is that most of the undesired noise happens around the signal,
        // especially after high->low transitions. So as an approximation of the
        // noise I test with a phase shift of 200ms.
        RadioClock_Demodulator::bins.noise_max = 0;
        const uint16_t noise_index = RadioClock_Demodulator::wrap(RadioClock_Demodulator::bins.max_index + RadioClock_Demodulator::bins_per_200ms);

        for (uint16_t bin = 0; bin < RadioClock_Demodulator::bins_per_100ms; ++bin) {
            RadioClock_Demodulator::bins.noise_max += ((uint32_t)RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(noise_index + bin)])*59;
        }

        for (uint16_t bin = RadioClock_Demodulator::bins_per_100ms; bin < RadioClock_Demodulator::bins_per_200ms; ++bin) {
            RadioClock_Demodulator::bins.noise_max += (uint32_t)RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(noise_index + bin)]*34;
        }

        for (uint16_t bin = RadioClock_Demodulator::bins_per_200ms; bin < RadioClock_Demodulator::bins_per_300ms; ++bin) {
            RadioClock_Demodulator::bins.noise_max += (uint32_t)RadioClock_Demodulator::bins.data[RadioClock_Demodulator::wrap(noise_index + bin)]<<3;
        }
    }
}

namespace MSF_Clock {
    typedef void (*output_handler_t)(const time_t &decoded_time);

    void setup() {
        RadioClock_Controller::setup();
    }

    void setup(const RadioClock_Clock::input_provider_t input_provider, const output_handler_t output_handler) {
        RadioClock_Controller::setup();
        MSF_Clock_Controller::set_output_handler(output_handler);
        RadioClock_1_Khz_Generator::setup(input_provider);
    };

    void debug() {
        MSF_Clock_Controller::debug();
    }

    void set_input_provider(const RadioClock_Clock::input_provider_t input_provider) {
        RadioClock_1_Khz_Generator::setup(input_provider);
    }

    void set_output_handler(const output_handler_t output_handler) {
        MSF_Clock_Controller::set_output_handler(output_handler);
    }

    void auto_persist() {
        RadioClock_Controller::auto_persist();
    }

    void convert_time(const MSF::time_data_t &current_time, time_t &now) {
        now.second                    = BCD::int_to_bcd(current_time.second);
        now.minute                    = current_time.minute;
        now.hour                      = current_time.hour;
        now.weekday                   = current_time.weekday;
        now.day                       = current_time.day;
        now.month                     = current_time.month;
        now.year                      = current_time.year;
        now.uses_summertime           = current_time.uses_summertime;
        now.timezone_change_scheduled = current_time.timezone_change_scheduled;
    }

    void get_current_time(time_t &now) {

        MSF::time_data_t current_time;
        MSF_Clock_Controller::get_current_time(current_time);

        convert_time(current_time, now);
    };

    void read_current_time(time_t &now) {
        MSF::time_data_t current_time;
        MSF_Clock_Controller::read_current_time(current_time);

        convert_time(current_time, now);
    };

    void read_future_time(time_t &now_plus_1s) {
        MSF::time_data_t current_time;
        MSF_Clock_Controller::read_current_time(current_time);
        MSF_Encoder::advance_second(current_time);

        convert_time(current_time, now_plus_1s);
    }

    void print(time_t time) {
        BCD::print(time.year);
        Serial.print('-');
        BCD::print(time.month);
        Serial.print('-');
        BCD::print(time.day);
        Serial.print(' ');
        Serial.print(time.weekday.val & 0xF, HEX);
        Serial.print(' ');
        BCD::print(time.hour);
        Serial.print(':');
        BCD::print(time.minute);
        Serial.print(':');
        BCD::print(time.second);

        if (time.uses_summertime) {
            Serial.print(F(" BST "));
        } else {
            Serial.print(F(" GMT "));
        }

        Serial.print(time.timezone_change_scheduled? '*': '.');
    }

    uint8_t get_overall_quality_factor() {
        return MSF_Clock_Controller::get_overall_quality_factor();
    };

    uint8_t get_prediction_match() {
        return MSF_Clock_Controller::get_prediction_match();
    };
}

namespace MSF_Frequency_Control {
    void process_1_Hz_tick(const MSF::time_data_t &decoded_time) {
        const int16_t deviation_to_trigger_readjust = 5;

        RadioClock_Frequency_Control::set_current_deviation(RadioClock_Frequency_Control::compute_phase_deviation(decoded_time.second, decoded_time.minute.digit.lo));

        if (decoded_time.second == RadioClock_Frequency_Control::calibration_second) {
            if (RadioClock_Frequency_Control::calibration_state.running) {
                if (RadioClock_Frequency_Control::calibration_state.qualified) {
                    if ((RadioClock_Frequency_Control::elapsed_minutes >= RadioClock_Frequency_Control::tau_min_minutes && abs(RadioClock_Frequency_Control::get_current_deviation()) >= deviation_to_trigger_readjust) ||
                            RadioClock_Frequency_Control::elapsed_minutes >= RadioClock_Frequency_Control::tau_max_minutes) {
                        RadioClock_Frequency_Control::adjust();

                        // enqueue write to eeprom
                        RadioClock_Frequency_Control::data_pending = true;
                        // restart calibration next second
                        RadioClock_Frequency_Control::calibration_state.running = false;
                    }
                } else {
                    // unqualified
                    if (RadioClock_Frequency_Control::elapsed_minutes >= RadioClock_Frequency_Control::tau_max_minutes) {
                        // running unqualified for more than tau minutes
                        //   --> the current calibration attempt is doomed
                        RadioClock_Frequency_Control::calibration_state.running = false;
                    }
                    // else running but unqualified --> wait for better state
                }
            } else {
                // (calibration_state.running == false) --> waiting
                if (RadioClock_Frequency_Control::calibration_state.qualified) {
                    RadioClock_Frequency_Control::elapsed_centiseconds_mod_60000 = 0;
                    RadioClock_Frequency_Control::elapsed_minutes = 0;
                    RadioClock_Frequency_Control::start_minute_mod_10 = decoded_time.minute.digit.lo;
                    RadioClock_Frequency_Control::calibration_state.running = true;
                }
                // else waiting but unqualified --> nothing to do
            }
        }
    }

}

namespace MSF_1_Khz_Generator {
    void isr_handler() {
        RadioClock_1_Khz_Generator::isr_handler();
        MSF_Clock_Controller::process_1_kHz_tick_data(RadioClock_1_Khz_Generator::the_input_provider());
    }
}

/*
#if defined(__AVR_ATmega32U4__)
ISR(TIMER3_COMPA_vect) {
    MSF_1_Khz_Generator::isr_handler();
}
#else
ISR(TIMER2_COMPA_vect) {
    MSF_1_Khz_Generator::isr_handler();
}
#endif
*/
