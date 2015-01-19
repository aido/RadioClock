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

#ifndef dcf77_h
#define dcf77_h

#include "radioclock.h"

namespace DCF77_Clock {
    typedef struct DCF77_Clock_time_t : RadioClock_Clock::time_t {
        bool uses_summertime;
        bool timezone_change_scheduled;
        bool leap_second_scheduled;
    } time_t;

    // Once the clock has locked to the DCF77 signal
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

    // determine quality of the DCF77 signal lock
    uint8_t get_overall_quality_factor();

    // determine the short term signal quality
    // 0xff = not available
    // 0..25 = extraordinary poor
    // 25.5 would be the expected value for 100% noise
    // 26 = very poor
    // 50 = best possible, every signal bit matches with the local clock
    uint8_t get_prediction_match();
}

namespace DCF77 {
    typedef enum {
        long_tick  = 3,
        short_tick = 2,
        undefined  = 1,
        sync_mark  = 0
    } tick_t;

    typedef struct {
        uint8_t byte_0;  // bit 16-20  // flags
        uint8_t byte_1;  // bit 21-28  // minutes
        uint8_t byte_2;  // bit 29-36  // hours, bit 0 of day
        uint8_t byte_3;  // bit 37-44  // day + weekday
        uint8_t byte_4;  // bit 45-52  // month + bit 0-2 of year
        uint8_t byte_5;  // bit 52-58  // year + parity
    } serialized_clock_stream;

    typedef struct DCF77_time_data_t : RadioClock::time_data_t {
        bool uses_summertime                : 1;  // false -> wintertime, true, summertime
        bool abnormal_transmitter_operation : 1;  // typically false
        bool timezone_change_scheduled      : 1;
        bool leap_second_scheduled          : 1;

        bool undefined_minute_output                        : 1;
        bool undefined_uses_summertime_output               : 1;
        bool undefined_abnormal_transmitter_operation_output: 1;
        bool undefined_timezone_change_scheduled_output     : 1;
    } time_data_t;

    typedef void (*output_handler_t)(const DCF77::time_data_t &decoded_time);
}

namespace DCF77_1_Khz_Generator {
   void isr_handler();
}

namespace DCF77_Frequency_Control {
    void process_1_Hz_tick(const DCF77::time_data_t &decoded_time);
}

namespace DCF77_Encoder {
    // What *** exactly *** is the semantics of the "Encoder"?
    // It only *** encodes *** whatever time is set
    // It does never attempt to verify the data

    void reset(DCF77::time_data_t &now);

    void get_serialized_clock_stream(const DCF77::time_data_t &now, DCF77::serialized_clock_stream &data);

    uint8_t weekday(const DCF77::time_data_t &now);  // sunday == 0
    BCD::bcd_t bcd_weekday(const DCF77::time_data_t &now);  // sunday == 7

    DCF77::tick_t get_current_signal(const DCF77::time_data_t &now);

    // This will advance the second. It will consider the control
    // bits while doing so. It will NOT try to properly set the
    // control bits. If this is desired "autoset" must be called in
    // advance.
    void advance_second(DCF77::time_data_t &now);

    // The same but for the minute
    void advance_minute(DCF77::time_data_t &now);

    // This will set the weekday by evaluating the date.
    void autoset_weekday(DCF77::time_data_t &now);

    // This will set the control bits, as a side effect it sets the weekday
    // It will generate the control bits exactly like DCF77 would.
    // Look at the leap second and summer / wintertime transistions
    // to understand the subtle implications.
    void autoset_control_bits(DCF77::time_data_t &now);

    bool verify_leap_second_scheduled(const DCF77::time_data_t &now, const bool assume_leap_second = false);

    void debug(const DCF77::time_data_t &clock);
    void debug(const DCF77::time_data_t &clock, const uint16_t cycles);

    // Bit      Bezeichnung     Wert    Pegel   Bedeutung
    // 0        M                       0       Minutenanfang (

    // 1..14    n/a                             reserviert

    // 15       R                               Reserveantenne aktiv (0 inaktiv, 1 aktiv)
    // 16       A1                              Ankündigung Zeitzonenwechsel (1 Stunde vor dem Wechsel für 1 Stunde, d.h ab Minute 1)
    // 17       Z1               2              Zeitzonenbit Sommerzeit (MEZ = 0, MESZ = 1); also Zeitzone = UTC + 2*Z1 + Z2
    // 18       Z2               1              Zeitzonenbit Winterzeit (MEZ = 1, MESZ = 0); also Zeitzone = UTC + 2*Z1 + Z2
    // 19       A2                              Ankündigung einer Schaltsekunde (1 Stunde vor der Schaltsekunde für 1 Stunde, d.h. ab Minute 1)

    // 20       S                       1       Startbit für Zeitinformation

    // 21                        1              Minuten  1er
    // 22                        2              Minuten  2er
    // 23                        4              Minuten  4er
    // 24                        8              Minuten  8er
    // 25                       10              Minuten 10er
    // 26                       20              Minuten 20er
    // 27                       40              Minuten 40er
    // 28       P1                              Prüfbit 1 (gerade Parität)

    // 29                        1              Stunden  1er
    // 30                        2              Stunden  2er
    // 31                        4              Stunden  4er
    // 32                        8              Stunden  8er
    // 33                       10              Stunden 10er
    // 34                       20              Stunden 20er
    // 35       P2                              Prüfbit 2 (gerade Parität)

    // 36                        1              Tag  1er
    // 37                        2              Tag  2er
    // 38                        4              Tag  4er
    // 39                        8              Tag  8er
    // 40                       10              Tag 10er
    // 41                       20              Tag 20er

    // 42                        1              Wochentag 1er (Mo = 1, Di = 2, Mi = 3,
    // 43                        2              Wochentag 2er (Do = 4, Fr = 5, Sa = 6,
    // 44                        4              Wochentag 4er (So = 7)

    // 45                        1              Monat  1er
    // 46                        2              Monat  2er
    // 47                        4              Monat  4er
    // 48                        8              Monat  8er
    // 49                       10              Monat 10er

    // 50                        1              Jahr  1er
    // 51                        2              Jahr  2er
    // 52                        4              Jahr  4er
    // 53                        8              Jahr  8er
    // 54                       10              Jahr 10er
    // 55                       20              Jahr 20er
    // 56                       40              Jahr 40er
    // 57                       80              Jahr 80er

    // 58       P3                              Prüftbit 3 (gerade Parität)

    // 59       sync                            Sync Marke, kein Impuls (übliches Minutenende)
    // 59                               0       Schaltsekunde (sehr selten, nur nach Ankündigung)
    // 60       sync                            Sync Marke, kein Impuls (nur nach Schaltsekunde)

    // Falls eine Schaltsekunde eingefügt wird, wird bei Bit 59 eine Sekundenmarke gesendet.
    // Der Syncimpuls erfolgt dann in Sekunde 60 statt 59. Üblicherweise wird eine 0 als Bit 59 gesendet

    // Üblicherweise springt die Uhr beim Wechsel Winterzeit nach Sommerzeit von 1:59:59 auf 3:00:00
    //                               beim Wechsel Sommerzeit nach Winterzeit von 2:59:59 auf 2:00:00

    // Die Zeitinformation wird immer 1 Minute im Vorraus übertragen. D.h. nach der Syncmarke hat
    // man die aktuelle Zeit

    // http://www.dcf77logs.de/SpecialFiles.aspx

    // Schaltsekunden werden in Deutschland von der Physikalisch-Technischen Bundesanstalt festgelegt,
    // die allerdings dazu nur die international vom International Earth Rotation and Reference Systems
    // Service (IERS) festgelegten Schaltsekunden übernimmt. Im Mittel sind Schaltsekunden etwa alle 18
    // Monate nötig und werden vorrangig am 31. Dezember oder 30. Juni, nachrangig am 31. März oder
    // 30. September nach 23:59:59 UTC (also vor 1:00 MEZ bzw. 2:00 MESZ) eingefügt. Seit der Einführung
    // des Systems 1972 wurden ausschließlich die Zeitpunkte im Dezember und Juni benutzt.
}

namespace DCF77_Naive_Bitstream_Decoder {
    void set_bit(const uint8_t second, const uint8_t value, DCF77::time_data_t &now);
}

namespace DCF77_Flag_Decoder {
    void setup();
    void process_tick(const uint8_t current_second, const uint8_t tick_value);

    void reset_after_previous_hour();
    void reset_before_new_day();

    bool get_uses_summertime();
    bool get_abnormal_transmitter_operation();
    bool get_timezone_change_scheduled();
    bool get_leap_second_scheduled();

    void debug();
}

namespace DCF77_Decade_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Year_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Month_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Day_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);	
}

namespace DCF77_Weekday_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Hour_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Minute_Decoder {
    void process_tick(const uint8_t current_second, const uint8_t tick_value);
}

namespace DCF77_Second_Decoder {
    void set_convolution_time(const DCF77::time_data_t &now); 
    uint8_t get_second();
	void process_single_tick_data(const DCF77::tick_t tick_data);
    uint8_t get_prediction_match();

    void debug();
	}

namespace DCF77_Local_Clock {
	void setup();
    void process_1_Hz_tick(const DCF77::time_data_t &decoded_time);
    void process_1_kHz_tick();
    void debug();

    // blocking till start of next second
    void get_current_time(DCF77::time_data_t &now);

    // non-blocking, reads current second
    void read_current_time(DCF77::time_data_t &now);
}

namespace DCF77_Clock_Controller {
    void process_1_kHz_tick_data(const uint8_t sampled_data);

    void process_single_tick_data(const DCF77::tick_t tick_data);

    void flush(const DCF77::time_data_t &decoded_time);
    void set_output_handler(const DCF77_Clock::output_handler_t output_handler);

    typedef struct DCF77_Clock_Controller_clock_quality_t : RadioClock_Controller::clock_quality_t {
        uint8_t uses_summertime_quality;
        uint8_t timezone_change_scheduled_quality;
        uint8_t leap_second_scheduled_quality;
    } clock_quality_t;

    void get_quality(clock_quality_t &clock_quality);

    uint8_t get_overall_quality_factor();
    uint8_t get_prediction_match();

    // blocking, will unblock at the start of the second
    void get_current_time(DCF77::time_data_t &now);

    // non-blocking reads current second
    void read_current_time(DCF77::time_data_t &now);

    void debug();
}

namespace DCF77_Demodulator {
    void detector(const uint8_t sampled_data);
}
#endif
