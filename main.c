#include <avr/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include "I2C.h"
#include "ssd1306.h"
#include "string.h"

#define BAUD 19200
#define MYUBRRF F_CPU/8/BAUD-1 // Beregner baud rate for UART (full duplex)
#define L 10  // Bufferlængde til UART-data

// Globale flag og variabler
volatile int t_flag = 0;
volatile int rx_flag = 0;
volatile int hours = 0, minutes = 0, seconds = 0;
volatile int button_flag = 0;
char data[L];

// Initialiserer UART og ekstern interrupt (INT4) 
void uart_init(unsigned int ubrr) {
    UCSR0A = (1<<U2X0); // Double speed mode
    UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0); // Aktiver RX, TX og RX interrupt
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00); // 8 data bits, 1 stop bit
    
    DDRE &= ~(1<<PE4); // Sæt PE4 som input
    PINE |= (1<<PE4);  // Aktivér pull-up modstand
    EIMSK |= (1<<INT4); // Aktiver INT4
    EICRB |= (1<<ISC41); // Trigger INT4 på stigende flanke 

    UBRR0H = (unsigned char)(ubrr>>8);  // Sæt baud rate high byte
    UBRR0L = (unsigned char)ubrr; // Sæt baud rate low byte

    // Initialiser Timer1 til 1ms interrupt
    TCCR1B = (1<<WGM12) | (1<<CS11) | (1<<CS10); // CTC mode, prescaler 64
    OCR1A = 249; // 1ms interrupt
    TIMSK1 = (1<<OCIE1A); // Aktiver timer interrupt 
}

// Interrupt Service Routine (ISR) for INT4 (knaptryk)
ISR(INT4_vect) {
    button_flag = 1; // Sæt flag når knappen trykkes
}

// ISR for UART-modtagelse
ISR(USART0_RX_vect) {
    static int i = 0; 
    char received_char = UDR0;

    if (received_char == '\n' || i >= L - 1) {  // Stop ved newline eller buffergrænse
        data[i] = '\0';  // Afslut string korrekt
        i = 0;           // Nulstil indeks
        rx_flag = 1;     // Flag at vi har modtaget en hel besked
    } else {
        data[i++] = received_char;
    }
}

// ISR for Timer1 (1ms tidsopdatering)
ISR(TIMER1_COMPA_vect) {
    static int count = 0;
    if (count < 999) { // 1 sekund = 1000 ms
        count++;
    } else {
        t_flag = 1; // Signalér at et sekund er gået
        count = 0;
    }
}

// Funktion til at sende en enkelt karakter via UART
void putchUSART0(char tx) {
    while (!(UCSR0A & (1<<UDRE0))); // Vent på tom buffer
    UDR0 = tx;
}

// Funktion til at sende en string via UART
void putsUSART0(char *s) {
    while (*s) {
        putchUSART0(*s++);
    }
}

// Funktion til at sende en array af karakterer via UART
void putStrUSART0(char array[]) {
    for (int i = 0; i < L; i++) {
        putchUSART0(array[i]);
    } 
}

// Opdaterer tid og holder styr på sekunder, minutter og timer
void calculate_time() {
    seconds++;
    if (seconds == 60) {
        seconds = 0;
        minutes++;
        if (minutes == 60) {
            minutes = 0;
            hours++;
            if (hours == 24) {
                hours = 0;
            }
        }
    }
}

// Viser tiden på OLED-displayet
void DisplayTime(int hours, int minutes, int seconds) {
    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    sendStrXY(buffer, 0, 0); // Skriver tid på display
}

volatile int time_initialized = 0; // Markerer om tiden er indstillet

// Håndterer brugerinput fra UART
void UserInterface() {
    if (rx_flag) { // Hvis vi har modtaget en hel besked
        rx_flag = 0;  // Nulstil flag

        // Parser brugerinput (HHMMSS)
        int h, m, s;
        if (sscanf(data, "%2d%2d%2d", &h, &m, &s) == 3) {
            if (h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
                hours = h;
                minutes = m;
                seconds = s;
                time_initialized = 1; 

                DisplayTime(hours, minutes, seconds); // Opdater display

                putsUSART0("Time set successfully: ");
                putsUSART0(data);
                putsUSART0("\n");
            } else {
                putsUSART0("Invalid time. Use HHMMSS format.\n");
            }
        } else {
            putsUSART0("Invalid input format. Use HHMMSS.\n");
        }

        memset(data, 0, L); // Ryd buffer
    }
}

// Hovedprogram
int main(void) {  
    uart_init(MYUBRRF);
    sei(); // Aktiver global interrupt
   
    _i2c_address = 0x78; // I2C adresse for SSD1306
    
    I2C_Init();  // Initialiser I2C
    InitializeDisplay(); // Initialiser display
    clear_display(); // Ryd display

    while (1) {
    
        if(button_flag){
            _delay_ms(100); // Debounce-knappen
            button_flag = 0; // Nulstil flag
            UserInterface(); // Håndter brugerinput
        }

        if (time_initialized && t_flag == 1) { // Kun opdater tid, hvis den er indstillet
            t_flag = 0;
            calculate_time();
            DisplayTime(hours, minutes, seconds); // Opdater display
        }
    }
}
