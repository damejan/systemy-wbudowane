/*
    projekt zaliczeniowy
    
    ustawienia transmisji w dsm-51:
        * Port: COM1
        * Liczba cykli na jeden bit: 192 (4800 bps)
        * Liczba bitów między bajtami: 2 
*/
#include<mcs51/8051.h>

__code unsigned char numbers[11] = {
    0b00111111, 0b00000110, 0b01011011,
    0b01001111, 0b01100110, 0b01101101,
    0b01111101, 0b00000111, 0b01111111,
    0b01101111, 0b00000000
};

// wyświetlacz 7seg
__xdata unsigned char * CSDS = (__xdata unsigned char *) 0xFF30; // bufor wyboru wskaźnika
__xdata unsigned char * CSDB = (__xdata unsigned char *) 0xFF38; // bufor danych wskaźnika
unsigned char selected_display; // wybrany wyświetlacz
unsigned char displayed_digits[6] = {0, 0, 0, 0, 0, 0}; // cyfry jakie mają się wyświetlać na wyświetlaczach
unsigned char display_counter; // licznik który będzie inkrementowany wraz z odświeżeniem wyświetlacza(gdy wskażnik przesunie się obok)

// flagi przycisków dla klawiatury multipleksowanej
__bit mux_kbd_flag_enter;
__bit mux_kbd_flag_esc;
__bit mux_kbd_flag_right;
__bit mux_kbd_flag_left;
__bit mux_kbd_flag_up;
__bit mux_kbd_flag_down;

__bit matrix_kbd_key_is_pressed; // flaga wciśniętego przycisku dla klawiatury matrycowej
__xdata unsigned char * CSKB1 = (__xdata unsigned char *) 0xFF22; // bufor klawiatury matrycowej z klawiszami 8...Enter
unsigned char matrix_kbd_buffer; 

unsigned char section_pointer; // wskażnik na konkretną sekcje(sekundy, godziny, minuty)
unsigned char mux_keyboard_buffer; // bufor dla klawiatury multiplexowanej

unsigned short timer0_int_counter; // licznik programowy który liczy przerwania timer'a 0

unsigned char seconds; // licznik sekund
unsigned char minutes; // licznik minut
unsigned char hours; // licznik godzin
__bit one_second_flag; // flaga ustawiana gdy licznik programowy osiągnie pewną wartość(upłynie sekunda)
__bit edit_mode; // tryb edycji
__bit clear_section_flag; // flaga ustawiana i czyszczona naprzemiennie, w trybie edycji używana do efektu migania sekcji

// nowe wartoci z trybu edycji
unsigned char new_seconds;
unsigned char new_minutes;
unsigned char new_hours;

// tansmisja szeregowa
__bit receive_flag;
__bit send_flag;
unsigned char receive_buffer[14];
unsigned char receive_buffer_index;
unsigned char send_buffer[9];
unsigned char send_buffer_index;

// wyświetlacz lcd
__xdata unsigned char * LCDWC = (__xdata unsigned char *) 0xFF80; // zapis rozkazów
__xdata unsigned char * LCDWD = (__xdata unsigned char *) 0xFF81; // zapis danych
__xdata unsigned char * LCDRC = (__xdata unsigned char *) 0xFF82; // odczyt stanu
__xdata __at(0x4000) unsigned char lcd_history[10][16]; // historia w zewnętrznym RAM
unsigned char history_iterator;
unsigned char lcd_history_iterator;
unsigned char lcd_tmp_iter;

// ogólne
void enter_edit_mode();
void general_init(); // inicjalizacja zmiennych ogólnych, takich jak, godziny, minuty, itp.
void refresh_digits(); // aktualizacja cyfr w tablicy cyfr do wyświetlania

// ustawianie nowego czasu w trybie edycji
void update_digits_up();
void update_digits_down();

// timery
void timer_init();
void timer0_int() __interrupt(1);

// wyświetlacz 7seg
void display_refresh();
void display_init();

// klawiatura multiplexoana
void mux_keyboard_refresh();
void mux_keyboard_react();

// klawiatura matrycoea
void matrix_keyboard_refresh();

// transmisja szeregowa
void int_serial() __interrupt(4) __using(1);
void serial_init();
void serial_refresh();
void serial_parse_get_edit();
void serial_parse_send();

// wyświetlacz lcd
void lcd_wait_busy();
void lcd_init();
void lcd_next_line();
void lcd_clear();
void lcd_mov_cursor_right();
// void lcd_refresh();
void write_ok();
void write_err();
void fill_with_spaces();
void write_to_display();

void main() 
{
    general_init();
    serial_init();
    display_init();
    timer_init();
    lcd_init();

    while(1) {
        if(one_second_flag) {
            one_second_flag = 0;
            refresh_digits();
            P1_7 = !P1_7; 
        }

        mux_keyboard_react();
        serial_refresh();
        matrix_keyboard_refresh();
    }
}

void general_init()
{
    seconds = 0;
    minutes = 0;
    hours = 0;
    new_seconds = 0;
    new_minutes = 0;
    new_hours = 0;
    edit_mode = 0;
    section_pointer = 0;
    matrix_kbd_key_is_pressed = 0;
    history_iterator = 0;
    lcd_history_iterator = 0;
    lcd_tmp_iter = 0;

    EA = 1; // globalna zgoda na przerwaniaal

}

void enter_edit_mode()
{
    if(!edit_mode) {
        edit_mode = 1;
        new_seconds = seconds;
        new_minutes = minutes;
        new_hours = hours;
    }
}

void refresh_digits()
{
    if(!edit_mode) {
        ++seconds;
        if(seconds == 60) {
            ++minutes;
            seconds = 0;
        }

        if(minutes == 60) {
            ++hours;
            minutes = 0;
        }

        if(hours == 24) {
            hours = 0;
        }
        displayed_digits[0] = seconds % 10; // cyfra jedności
        displayed_digits[1] = seconds / 10; // cyfra dziesiątek

        displayed_digits[2] = minutes % 10;
        displayed_digits[3] = minutes / 10;

        displayed_digits[4] = hours % 10;
        displayed_digits[5] = hours / 10;
    } else {
        // tryb edycji
        displayed_digits[0] = new_seconds % 10; // cyfra jedności
        displayed_digits[1] = new_seconds / 10; // cyfra dziesiątek

        displayed_digits[2] = new_minutes % 10;
        displayed_digits[3] = new_minutes / 10;

        displayed_digits[4] = new_hours % 10;
        displayed_digits[5] = new_hours / 10;
    }
}

void update_digits_up()
{
    if(section_pointer == 0) {
        if(new_seconds < 59) {
            ++new_seconds;
        }
    }
    if(section_pointer == 1) {
        if(new_minutes < 59) {
            ++new_minutes;
        }
    }
    if(section_pointer == 2) {
        if(new_hours < 23) {
            ++new_hours;
        }
    }
}

void update_digits_down()
{
    if(section_pointer == 0) {
        if(new_seconds != 0) {
            --new_seconds;
        }
    }
    if(section_pointer == 1) {
        if(new_minutes != 0) {
            --new_minutes;
        }
    }
    if(section_pointer == 2) {
        if(new_hours != 0) {
            --new_hours;
        }
    }
}

void mux_keyboard_refresh()
{
    // S7On = 1;

    // enter
    // wskazanie interesującego wskaźnika przycisku
    *CSDS = 0b00000001;
    
    if(P3_5) { // sprawdzamy stan P3.5 do którego podłączona jest klawiatura multiplexowana
        // jeśli enter nie jest wciśnięty a P3_5 jest zapalony
        // ale w mux_keyboard_buffer niema zapalonego bitu odpwoedzialnego za enter
        if(mux_keyboard_buffer != 0b00000001) {
            // to ustawiamy flagę że enter jest wciśnięty
            mux_kbd_flag_enter = 1;
            // i dodajemy bit entera do bufora
            mux_keyboard_buffer |= 0b00000001;
        }
        // możliwe że w mux_keyboard_buffer jest już zapalony bit od entera, 
        // a to znaczy że enter jest wciśnięty i cały czas trzymany, w takim przypadku nic nie robimy

    } else { // jeżeli P3.5 nie był jednak zapalony
        // jeżeli pierwszy bit nie jest 0, to znaczy że w mux_keyboard_buffer jest wciśnięty enter, ale p3.5 jest zgaszone
        if(mux_keyboard_buffer != 0b11111110) {
            // czyli musimy zgasić flagę
            mux_kbd_flag_enter = 0;
            // i skasować ostatni bit bufora
            mux_keyboard_buffer &= 0b11111110;
        }
        
        // wychodzimy z całego warunku i będzie sprawdzany następny przycisk
    }

    // esc 
    *CSDS = 0b00000010;
    
    if(P3_5) {
        if(mux_keyboard_buffer != 0b00000010) {
            mux_kbd_flag_esc = 1;
            mux_keyboard_buffer |= 0b00000010;
        }
    } else {
        if(mux_keyboard_buffer != 0b11111101) {
            mux_kbd_flag_esc = 0;
            mux_keyboard_buffer &= 0b11111101;
        }
    }

    // right 
    *CSDS = 0b00000100;
    
    if(P3_5) {
        if(mux_keyboard_buffer != 0b00000100) {
            mux_kbd_flag_right = 1;
            mux_keyboard_buffer |= 0b00000100;
        }
    } else {
        if(mux_keyboard_buffer != 0b11111011) {
            mux_kbd_flag_right = 0;
            mux_keyboard_buffer &= 0b11111011;
        }
    }

    // left 
    *CSDS = 0b00100000;
    
    if(P3_5) {
        if(mux_keyboard_buffer != 0b00100000) {
            mux_kbd_flag_left = 1;
            mux_keyboard_buffer |= 0b00100000;
        }
    } else {
        if(mux_keyboard_buffer != 0b11011111) {
            mux_kbd_flag_left = 0;
            mux_keyboard_buffer &= 0b11011111;
        }
    }

    // up 
    *CSDS = 0b00001000;
    
    if(P3_5) {
        if(mux_keyboard_buffer != 0b00001000) {
            mux_kbd_flag_up = 1;
            mux_keyboard_buffer |= 0b00001000;
        }
    } else {
        if(mux_keyboard_buffer != 0b11110111) {
            mux_kbd_flag_up = 0;
            mux_keyboard_buffer &= 0b11110111;
        }
    }

    // down 
    *CSDS = 0b00010000;
    
    if(P3_5) {
        if(mux_keyboard_buffer != 0b00010000) {
            mux_kbd_flag_down = 1;
            mux_keyboard_buffer |= 0b00010000;
        }
    } else {
        if(mux_keyboard_buffer != 0b11101111) {
            mux_kbd_flag_down = 0;
            mux_keyboard_buffer &= 0b11101111;
        }
    }


    // po przetestowaniu wszystkich przycików przywracamy orginalny wskaźnik na wyświetlacz
    // *CSDS = selected_display;
    // S7On = 0;
}

void mux_keyboard_react()
{
    // enter
    if(mux_kbd_flag_enter) { // jeśli wykryto flage enter, czyli klawisz enter jest wcisnięty
        mux_kbd_flag_enter = 0; // gasimy flage
        // i sprawdzamy bufor czy nie jest ustawiony bit od entera
        // gdybyśmy opierali się na samych flagach mogłoby to dać diwny efekt po wciśnieću 2 przycików naraz
        if(mux_keyboard_buffer == 0b00000001) {
            seconds = new_seconds;
            minutes = new_minutes;
            hours = new_hours;
            edit_mode = 0;
        }
    }
    
    //esc
    if(mux_kbd_flag_esc) {
        mux_kbd_flag_esc = 0;
        if(mux_keyboard_buffer == 0b00000010) {
            edit_mode = 0;
        }
    }

    //right
    if(mux_kbd_flag_right) {
        mux_kbd_flag_right = 0;
        if(mux_keyboard_buffer == 0b00000100) {
            enter_edit_mode();
            if(section_pointer != 0) {
                --section_pointer;
            }
        }
    }

    //left
    if(mux_kbd_flag_left) {
        mux_kbd_flag_left = 0;
        if(mux_keyboard_buffer == 0b00100000) {
            enter_edit_mode();
            if(section_pointer < 2) {
                ++section_pointer;
            }
        }
    }

    //up
    if(mux_kbd_flag_up) {
        mux_kbd_flag_up = 0;
        if(mux_keyboard_buffer == 0b00001000) {
            update_digits_up();
        }
    }

    //down
    if(mux_kbd_flag_down) {
        mux_kbd_flag_down = 0;
        if(mux_keyboard_buffer == 0b00010000) {
            update_digits_down();
        }
    }
}

void matrix_keyboard_refresh()
{
    matrix_kbd_buffer = ~(*CSKB1); // zanegowany stan klawiatury ponieważ tak łatwiej jest mi ją badać

    if(matrix_kbd_buffer == 0b00100000 && !matrix_kbd_key_is_pressed) { // strzałka w dół
        if(lcd_history_iterator < history_iterator) {
            ++lcd_history_iterator;
            write_to_display();
        }
    } else if(matrix_kbd_buffer == 0b00010000 && !matrix_kbd_key_is_pressed) { // strzałka w górę
        if(lcd_history_iterator > 2) {
            --lcd_history_iterator;
            write_to_display();
        }
    }

    // zabezpieczenie przed ciągłym trzymaniem przycisku
    if(matrix_kbd_buffer > 0) {
        matrix_kbd_key_is_pressed = 1;
    } else {
        matrix_kbd_key_is_pressed = 0;
    }
}

void display_refresh()
{
    P1_6 = 1; // 1 wyłącza wyświetlacze, 0 włącza
    
    // sprawdzenie multiplexowanej klawiatury
    mux_keyboard_refresh();

    *CSDS = selected_display;
    
    if(edit_mode && clear_section_flag) {
        if(section_pointer == 0) {
            if((display_counter == 0 || display_counter == 1)) {
                *CSDB = numbers[10];
            } else {
                *CSDB = numbers[displayed_digits[display_counter]];
            }
        } else if(section_pointer == 1) {
            if((display_counter == 2 || display_counter == 3)) {
                *CSDB = numbers[10];
            } else {
                *CSDB = numbers[displayed_digits[display_counter]];
            }
        } else if(section_pointer == 2) {
            if((display_counter == 4 || display_counter == 5)) {
                *CSDB = numbers[10];
            } else {
                *CSDB = numbers[displayed_digits[display_counter]];
            }
        }
        
    } else {
        if(display_counter == 2 || display_counter == 4) {
            *CSDB = numbers[displayed_digits[display_counter]] | 0b10000000;
        } else {
            *CSDB = numbers[displayed_digits[display_counter]];
        }
    }
    
    P1_6 = 0;
    
    selected_display = selected_display << 1;
    ++display_counter;

    if(selected_display == 0b01000000) {
        selected_display = 0b00000001;
        display_counter = 0;
    }
}

void display_init()
{
    selected_display = 0b00000001;
    display_counter = 0;
    display_refresh();
}

void timer_init()
{
    one_second_flag = 0;
    timer0_int_counter = 0;

    // konfiguracja timerów: używany Timer 0, 13 bitowy
    // TMOD = 0b01110000;
    TMOD = 0b00100000;
    
    // włączenie obu timer'ów
    TR0 = 1;
    TR1 = 1;

    TH0 = 226; // będzie 960 przerwań na sekundę
    TH1 = 250; // 4800bps dla transmisji szeregowej

    ET0 = 1; // zgoda na przerwania od Timer0
}

void serial_init()
{
    SCON = 0b01010000;
    receive_flag = 0;
    send_flag = 0;
    ES = 1; // zgoda na przerwania od transmisji szeregowej
}

void serial_parse_get_edit()
{
    if(receive_buffer_index == 5) {
        if(receive_buffer[0] == 'G' && receive_buffer[1] == 'E' && receive_buffer[2] == 'T') {
            // P1_5 = !P1_5;
            receive_flag = 0;
            receive_buffer_index = 0;
            send_buffer[0] = (hours / 10) + 48; // cyfra dziesiątek
            send_buffer[1] = (hours % 10) + 48; // cyfra jedności
            send_buffer[2] = '.';
            send_buffer[3] = (minutes / 10) + 48;
            send_buffer[4] = (minutes % 10) + 48;
            send_buffer[5] = '.';
            send_buffer[6] = (seconds / 10) + 48;
            send_buffer[7] = (seconds % 10) + 48;
            send_buffer[8] = '\n';
            
            write_ok();
            
            send_flag = 1;
        } else {
            receive_buffer_index = 0;
            write_err();
        }
    }
    
    if(receive_buffer_index == 6) {
        if(receive_buffer[0] == 'E' && receive_buffer[1] == 'D' && receive_buffer[2] == 'I'  && receive_buffer[3] == 'T') {
            // P1_5 = 0;
            receive_flag = 0;
            receive_buffer_index = 0;
            enter_edit_mode();
            write_ok();
        } else {
            receive_buffer_index = 0;
            write_err();
        }
    }
}

void serial_parse_send()
{
    if(receive_buffer[0] == 'S' && receive_buffer[1] == 'E' && receive_buffer[2] == 'T') {
        receive_flag = 0;
        receive_buffer_index = 0;
        if(receive_buffer[3] == ' ' && receive_buffer[6] == '.' && receive_buffer[9] == '.') {
            // pierwsza cyfra godziny, musi się zawierać w przedziale od 0 do 2
            // druga cyfra godziny, musi się zawierać w przedziale od 0 do 9
            if((receive_buffer[4] > 47 && receive_buffer[4] < 51) && (receive_buffer[5] > 47 && receive_buffer[5] < 58)) {
                new_hours = ((receive_buffer[4]-48)*10) + (receive_buffer[5]-48);
                if(new_hours < 24) {
                    hours = new_hours;
                } else {
                    write_err();
                    return;
                }
            } else {
                write_err();
                return;
            }

            // podobnie minuty i sekundy
            if((receive_buffer[7] > 47 && receive_buffer[7] < 54) && (receive_buffer[8] > 47 && receive_buffer[8] < 58)) {
                new_minutes = ((receive_buffer[7]-48)*10) + (receive_buffer[8]-48);
                if(new_minutes < 60) {
                    minutes = new_minutes;
                } else {
                    write_err();
                    return;
                }
            } else {
                write_err();
                return;
            }

            if((receive_buffer[10] > 47 && receive_buffer[10] < 54) && (receive_buffer[11] > 47 && receive_buffer[11] < 58)) {
                new_seconds = ((receive_buffer[10]-48)*10) + (receive_buffer[11]-48);
                if(new_seconds < 60) {
                    seconds = new_seconds;
                } else {
                    write_err();
                    return;
                }
            } else {
                write_err();
                return;
            }

            write_ok();
        } else {
            write_err();
        }
    } else {
        receive_buffer_index = 0;
        write_err();
    }
}

void serial_refresh()
{
    if(receive_flag) {
        // P1_5 = 0;
        receive_flag = 0;

        // *LCDWD = receive_buffer[receive_buffer_index-3];
        
        if(receive_buffer_index < 15) {
            if(receive_buffer[receive_buffer_index-2] == 13 && receive_buffer[receive_buffer_index-1] == 10) {
                // receive_flag = 0;
                // P1_5 = 0;
                // unsigned char tmp_iter;
                for(lcd_tmp_iter = 0; lcd_tmp_iter < receive_buffer_index-2; lcd_tmp_iter++) {
                    // *LCDWD = receive_buffer[lcd_tmp_iter];
                    lcd_history[history_iterator][lcd_tmp_iter] = receive_buffer[lcd_tmp_iter];
                }

                if(receive_buffer_index == 5 || receive_buffer_index == 6) {
                    // P1_5 = 1;
                    serial_parse_get_edit();
                } else if(receive_buffer_index == 14) {
                    // P1_5 = 1;
                    serial_parse_send();
                } else {
                    receive_buffer_index = 0;
                    write_err();
                    // write_to_display();
                    // P1_5 = 0;
                }
                
                ++history_iterator;
                if(history_iterator == 10) {
                    history_iterator = 0;
                }
                lcd_history_iterator = history_iterator;
                write_to_display();

            }
        } else {
            receive_buffer_index = 0;
        }
        // P1_5 = 0;
    }

    if(send_flag) {
        send_flag = 0;
        // P1_5 = 0;
        if(send_buffer_index < 9) {
            SBUF = send_buffer[send_buffer_index];
            ++send_buffer_index;
        } else {
            send_buffer_index = 0;
            // receive_buffer_index = 0;
        }
    }
}

void lcd_wait_busy()
{
    while(((*LCDRC) & 0b10000000) == 0b10000000);
}

void lcd_init()
{
    *LCDWC = 0b00001110;
    // *LCDWC = 0b00001101;
    lcd_wait_busy();
    *LCDWC = 0b00111000;
    lcd_wait_busy();
    *LCDWC = 0b00000110;
    lcd_wait_busy();
    *LCDWC = 0b00000001;
    lcd_wait_busy();
}

void lcd_next_line()
{
    *LCDWC = 0b11000000;
    lcd_wait_busy();
}

void lcd_mov_cursor_right()
{
    *LCDWC = 0b00010100;
    lcd_wait_busy();
}

void lcd_clear()
{
    *LCDWC = 0b00000001;
    lcd_wait_busy();
}

void write_to_display()
{
    if(lcd_history_iterator > 1) {
        lcd_clear();
        for(lcd_tmp_iter = 0; lcd_tmp_iter < 16; lcd_tmp_iter++) {
            *LCDWD = lcd_history[lcd_history_iterator-2][lcd_tmp_iter];
            lcd_wait_busy();
        }
        lcd_next_line();
        for(lcd_tmp_iter = 0; lcd_tmp_iter < 16; lcd_tmp_iter++) {
            *LCDWD = lcd_history[lcd_history_iterator-1][lcd_tmp_iter];
            lcd_wait_busy();
        }
    } else {
        for(lcd_tmp_iter = 0; lcd_tmp_iter < 16; lcd_tmp_iter++) {
            *LCDWD = lcd_history[lcd_history_iterator-1][lcd_tmp_iter];
            lcd_wait_busy();
        }
        lcd_next_line();
    }
    lcd_tmp_iter = 0;
}

void fill_with_spaces()
{
    while(lcd_tmp_iter < 16) {
        // *LCDWD = ' ';
        // lcd_wait_busy();
        lcd_history[history_iterator][lcd_tmp_iter] = ' ';
        lcd_tmp_iter++;
    }
    lcd_tmp_iter = 0;
}

void write_ok()
{

    lcd_history[history_iterator][lcd_tmp_iter] = ' ';
    ++lcd_tmp_iter;
    lcd_history[history_iterator][lcd_tmp_iter] = 'O';
    ++lcd_tmp_iter;
    lcd_history[history_iterator][lcd_tmp_iter] = 'K';
    ++lcd_tmp_iter;
    fill_with_spaces();
    // ++history_iterator;
}

void write_err()
{
    lcd_history[history_iterator][lcd_tmp_iter] = ' ';
    ++lcd_tmp_iter;
    lcd_history[history_iterator][lcd_tmp_iter] = 'E';
    ++lcd_tmp_iter;
    lcd_history[history_iterator][lcd_tmp_iter] = 'R';
    ++lcd_tmp_iter;
    lcd_history[history_iterator][lcd_tmp_iter] = 'R';
    ++lcd_tmp_iter;
    fill_with_spaces();
    // ++history_iterator;
}

void timer0_int() __interrupt(1) 
{
    TH0 = 226;

    display_refresh();
    ++timer0_int_counter;
    if(timer0_int_counter == 960) {
        one_second_flag = 1;
        clear_section_flag = !clear_section_flag;
        timer0_int_counter = 0;
    }
}

void int_serial() __interrupt(4) __using(1)
{
    // P1_5 = 0;
    if(TI) {
        TI = 0;
        send_flag = 1;
        return;
    }

    if(RI) {
        receive_buffer[receive_buffer_index] = SBUF;
        ++receive_buffer_index;

        RI = 0;
        receive_flag = 1;
        return;
    }
}