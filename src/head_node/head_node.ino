// Mega pins
// MOSI: 51 or ICSP-4
// MISO: 50 or ICSP-1
// SCK:  52 or ICSP-3


#include <SPI.h>
#include <Wire.h>
#include "pins_arduino.h"
#include "ktane.h"

game_rand_t game_rand;
game_info_t game_info;

unsigned long game_length; // Length of game (ms)
unsigned long start_time;  // Time of game start (ms)


enum slave_state_t {
    STATE_DISCONNECTED,
    STATE_READY,
    STATE_RUN,
    STATE_NEEDY,
    STATE_SOLVED
};


// Pins
// Pins for SS of each slave
int slave_pins[NUM_MODULES] = {10, 11};

void setup (void) {
    // Put SCK, MOSI, SS pins into output mode
    // also put SCK, MOSI into LOW state, and SS into HIGH state.
    // Then put SPI hardware into Master mode and turn SPI on
    SPI.begin();
    SPI.setClockDivider(SPI_CLOCK_DIV32);

    for(int i = 0; i < NUM_MODULES; i++){
        pinMode(slave_pins[i], OUTPUT);
        digitalWrite(slave_pins[i], HIGH);
    }

    Serial.begin(9600);
    Serial.println("HEAD v0.01 alpha");
}

void gen_rand() {
    // TODO: seed random better (repeated readings? low-order bits?)
    randomSeed(analogRead(A0));

    for (int i = 0; i < SN_LEN; i++) {
        game_rand.sn[i] = random(2) ? random('0', '9'+1) 
                                    : random('A', 'Z'+1);
    }
    for (int i = 0; i < MODEL_LEN; i++) {
        game_rand.model[i] = random(2) ? random('0', '9'+1)
                                       : random('A', 'Z'+1);
    }
    game_rand.indicators = random(256);
    game_rand.print_rand();
}

void transfer_rand(int slave_idx) {
    Serial.print("Transferring to ");
    Serial.println(slave_idx);

    // Copy rand data to buffer so as not to mutilate it
    game_rand_t out_buf;
    memcpy(&out_buf, &game_rand, sizeof(game_rand_t));

    digitalWrite(slave_pins[slave_idx], LOW);
    delay(10);
    SPI.transfer(CMD_INIT);
    SPI.transfer(&out_buf, sizeof(game_rand_t));
    digitalWrite(slave_pins[slave_idx], HIGH);
    game_rand.print_rand();
}

byte transfer_info(int slave_idx) {
    // Copy game info to buffer so as not to mutilate it
    game_info_t out_buf;
    memcpy(&out_buf, &game_info, sizeof(game_info_t));

    digitalWrite(slave_pins[slave_idx], LOW);
    delay(10);
    byte rsp = SPI.transfer(CMD_INFO);
    SPI.transfer(&out_buf, sizeof(game_info_t));
    digitalWrite(slave_pins[slave_idx], HIGH);
    return rsp;
}

void update_game_time() {
    unsigned long elapsed = millis() - start_time;
    if (elapsed >= game_length) {
        game_info.game_time = 0;
    } else {
        game_info.game_time = game_length - elapsed;
    }
}

byte transfer_byte(byte b, byte pin) {
    digitalWrite(slave_pins[pin], LOW);
    delay(10);
    byte rsp = SPI.transfer(b);
    digitalWrite(slave_pins[pin], HIGH);
    return rsp;
}

void loop(void) {
    Serial.println("Starting run");

    slave_state_t slave_state[NUM_MODULES] = {};
    byte num_strikes[NUM_MODULES] = {};

    gen_rand();
    game_info.strikes = 0;
    game_length = 300000; // 5 minutes

    Serial.println("Establishing connections");
    // Try to establish connection with each module a few times a second
    while (true) {
        delay(2000);

        for (int i = 0; i < NUM_MODULES; i++) {
            Serial.print("Sending PING to slave ");
            Serial.println(i);

            byte rsp_raw = transfer_byte(CMD_PING, i);
            Serial.print("Received response ");
            Serial.println(rsp_raw, HEX);

            byte rsp_state = rsp_raw & (~STRIKE_MASK);
            byte rsp_strikes = rsp_raw & STRIKE_MASK;

            if (rsp_state == RSP_READY) {
                if (slave_state[i] == STATE_DISCONNECTED) {
                    slave_state[i] = STATE_READY;

                    Serial.println("Sending INIT");
                    transfer_rand(i);
                }
            } else {
                slave_state[i] = STATE_DISCONNECTED;
            }
        }

        // Break loop
        if (Serial.available() > 0 && Serial.read() == 's') {
            Serial.println("Breaking loop");
            break;
        }
    }

    Serial.println("Starting game");
    start_time = millis();
    update_game_time();
    bool game_ended = false;

    while (true) {
        Serial.print("Updating modules ");
        game_info.print_info();

        // Send each slave updates on the game state
        game_ended = true; // Set to false if a module is unsolved
        for(int i = 0; i < NUM_MODULES; i++){
            update_game_time();

            byte rsp_raw = transfer_info(i);

            byte rsp_state = rsp_raw & (~STRIKE_MASK);
            byte rsp_strikes = rsp_raw & STRIKE_MASK;

            if (rsp_strikes > num_strikes[i]){
                game_info.strikes++;
                num_strikes[i]++;

                Serial.print("Module ");
                Serial.print(i);
                Serial.println(" striked");
            }

            if (rsp_state == RSP_SOLVED) {
                if (slave_state[i] != STATE_SOLVED) {
                    Serial.print("Module solved ");
                    Serial.println(i);
                }
                slave_state[i] = STATE_SOLVED;
            } else if (rsp_state == RSP_NEEDY) {
                if (slave_state[i] != STATE_NEEDY) {
                    Serial.print("Module is needy ");
                    Serial.println(i);
                }
                slave_state[i] = STATE_NEEDY;
            } else if (rsp_state == RSP_READY or rsp_state == RSP_ACTIVE) {
                game_ended = false; 
            } else {
                Serial.print(i);
                Serial.print(": Weird response: 0x");
                Serial.println(rsp_state, HEX);
            }
        }

        if (game_ended) {
            Serial.println("Solved!");
            break;
        }

        if (game_info.strikes >= 3) {
            game_info.strikes = 3;
            Serial.println("Striked out");
            break;
        }

        if (game_info.game_time == 0) {
            Serial.println("Timed out");
            break;
        }

        delay(1000);
    }
    Serial.println("Game over, informing slaves");

    // Tell all slaves game is over
    for(int i = 0; i < NUM_MODULES; i++){
        transfer_byte(game_ended ? CMD_WON : CMD_LOST, i);
    }

    Serial.println("Done");

    delay(10000);
}
