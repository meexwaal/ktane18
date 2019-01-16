// Uno pins
// MOSI: 11 or ICSP-4
// MISO: 12 or ICSP-1
// SCK:  13 or ICSP-3
// SS (slave): 10

#include <Wire.h>
#include "pins_arduino.h"
#include "ktane.h"

#define SS_ISR_PIN 2

game_rand_t game_rand;
game_info_t game_info;
volatile int pos;


// How many strikes the module has had
byte strikes;

// Status to send to head node
byte my_status; // Arduino has the worst reserved keywords

// Time left on the timer (in ms)
unsigned long millis_left;
volatile int time_idx;

enum state_t {
    STATE_UNREADY,     // Game need some set up
    STATE_READY,       // Game hasn't started
    STATE_RUN,         // Game running
    STATE_READ_INIT,   // Reading init data from master (e.g. SN)
    STATE_READ_INFO    // Reading info from master (time + strikes)
};

state_t state;
volatile bool interrupt_called;
bool print_info;

struct interrupt_debug_t {
    byte recv;
    void init() {
        recv = -1;
    }

    void print_interrupt() {
        Serial.print("Received SPI byte ");
        Serial.println(recv, BIN);
    }
} int_debug;


void get_miso() {
    if (digitalRead(SS) == LOW) {
        pinMode(MISO, OUTPUT);
    } else {
        pinMode(MISO, INPUT);
    }
}


void setup (void) {
    Serial.begin(9600);
    Serial.println("MODULE v0.01 alpha");

    // Don't grab MISO until selectesd
    pinMode(MISO, INPUT);
    pinMode(SS, INPUT);
    pinMode(SS_ISR_PIN, INPUT);

    // turn on SPI in slave mode
    SPCR |= _BV(SPE);

    // turn on interrupts
    SPCR |= _BV(SPIE);

    // Set MISO to OUTPUT when SS goes LOW
    // Docs say only pins 2 and 3 are usable
    attachInterrupt(digitalPinToInterrupt(SS_ISR_PIN), get_miso, CHANGE);
}

// Update state and SPDR
void set_state_spdr(state_t new_state){
    state = new_state;
    // Use spdr_new b/c reading SPDR changes it
    byte spdr_new = RSP_DEBUG;

    switch(state){
        case STATE_UNREADY:
            spdr_new = RSP_UNREADY;
            break;
        case STATE_READY:
            spdr_new = RSP_READY;
            break;
        case STATE_RUN:
            spdr_new = RSP_ACTIVE;
            break;
        case STATE_READ_INIT:
            spdr_new = RSP_READY;
            break;
        case STATE_READ_INFO:
            spdr_new = RSP_ACTIVE;
            break;
    }
    spdr_new |= strikes;

    SPDR = spdr_new;
}

// SPI interrupt routine
// Serial communication can mess up interrupts, so store debug info elsewhere
ISR (SPI_STC_vect) {
    byte c = SPDR;
    SPDR = RSP_DEBUG; // We should ensure that RSP_DEBUG never stays in SPDR
                      // because we should always update it to the correct value

    int_debug.recv = c;
    if (state == STATE_UNREADY) {
        set_state_spdr(STATE_UNREADY);
    } else if (state == STATE_READY) {
        if (c == CMD_INIT) {
            set_state_spdr(STATE_READ_INIT);
            pos = 0;
        } else if (c == CMD_INFO) {
            set_state_spdr(STATE_READ_INFO);
            pos = 0;
        } else if (c == CMD_PING) {
            set_state_spdr(STATE_READY);
        }
    } else if (state == STATE_READ_INIT){
        // Copy over bytes to game_rand struct
        ((byte *)&game_rand)[pos] = c;
        pos++;

        if (pos == sizeof(game_rand_t)) {
            pos = 0;
            set_state_spdr(STATE_READY);
            print_info = true;
        } else {
            set_state_spdr(STATE_READ_INIT);
        }
    } else if (state == STATE_READ_INFO){
        // Copy over bytes to game_info struct
        ((byte *)&game_info)[pos] = c;
        pos++;

        if (pos == sizeof(game_info_t)) {
            pos = 0;
            set_state_spdr(STATE_RUN);
            print_info = true;
        } else {
            set_state_spdr(STATE_READ_INFO);
        }
    }

    interrupt_called = true;
}

// Each iteration of loop will be one game
void loop (void) {
    Serial.println("Starting run");

    // Set up for each run
    strikes = 0;
    pos = 0;
    state = STATE_UNREADY;
    interrupt_called = false;
    print_info = false;

    // If necessary, wait until game is ready and in a valid physical state

    // Done with set up, let master know
    set_state_spdr(STATE_READY);

    Serial.println("Done with setup, polling for start command");
    // Wait for start command
    while (state != STATE_RUN) {
        if (interrupt_called) {
            Serial.println("Interrupt called");

            int_debug.print_interrupt();
            int_debug.init();
            interrupt_called = false;

            if (print_info) {
                game_rand.print_rand();
                game_info.print_info();
                print_info = false;
            }
        }
    }

    Serial.println("Starting game");

    // Play the game
    while(1){
        if (Serial.available() and Serial.read() == 'x'){
            Serial.println("Striking");
            strikes++;
        }

        if (interrupt_called) {
            int_debug.print_interrupt();
            int_debug.init();
            interrupt_called = false;

            if (print_info) {
                game_rand.print_rand();
                print_info = false;
            }
        }
    }
}
