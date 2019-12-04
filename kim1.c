#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h> // For FIONREAD
#include <termios.h>
#include <stdbool.h>

uint8_t ram[1024];
uint8_t riot003rom[1024];
uint8_t riot003ram[64];
uint8_t riot002rom[1024];
uint8_t riot002ram[64];

extern void reset6502();
extern void exec6502(uint32_t);
extern void step6502();
extern uint16_t pc, a, x, y;

uint8_t riot003read(uint16_t);
uint8_t riot002read(uint16_t);
void riot003write(uint16_t, uint8_t);
void riot002write(uint16_t, uint8_t);

uint8_t display[6];
uint8_t display_changed;
long display_changed_time;

uint8_t char_pending;

uint8_t single_step;

int kbhit(void) {
    static bool initflag = false;
    static const int STDIN = 0;

    if (!initflag) {
        // Use termios to turn off line buffering
        struct termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initflag = true;
    }

    int nbbytes;
    ioctl(STDIN, FIONREAD, &nbbytes);  // 0 is STDIN
    return nbbytes;
}

long current_time_millis() {
    struct timeval tv;
    return tv.tv_sec + 1000 + tv.tv_usec / 1000;
}

void load_roms() {
    FILE *in;

    if ((in = fopen("6530-002.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot003rom, 1, sizeof(riot003rom), in);
    fclose(in);

    if ((in = fopen("6530-003.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot002rom, 1, sizeof(riot002rom), in);
    fclose(in);

}

void check_pc() {
    int digit;
    if (pc == 0x1f56) {
        digit = (x >> 1) & 0xf;
        if (display[x] != a) {
            display_changed = 1;
            display[x] = a;
            display_changed_time = current_time_millis();
        }
    } else if (pc == 0x1f6a) {
        a = char_pending;
        char_pending = 0x15;
        pc = 0x1f90;
    }
}

void handle_kb() {
    char ch;

    ch = getchar();
    if ((ch >= '0') && (ch <= '9')) {
        char_pending = ch - '0';
    } else if ((ch >= 'a') && (ch <= 'f')) {
        char_pending = 10 + ch - 'a';
    } else if (ch == 1) {
        char_pending = 0x10;
    } else if (ch == 4) {
        char_pending = 0x11;
    } else if (ch == 16) {
        char_pending = 0x14;
    } else if (ch == '+') {
        char_pending = 0x12;
    } else if (ch == 7) {
        char_pending = 0x13;
    } else if (ch == 18) {
        printf("RESET\n");
        reset6502();
    } else if (ch == 0x1b) {
        if (single_step) {
            printf("Single step OFF\n");
        }
        single_step = 0;
    } else if (ch == 0x1d) {
        if (!single_step) {
            printf("Single step ON\n");
        }
        single_step = 1;
    } else if (ch == 3) {
        exit(0);
    }
}

char display_map[128] = {
/*              0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f */
/* 0x00 */    '?', '?', '?', '?', '?', '?', '1', '7', '?', '?', '?', '?', '?', '?', '?', '?', 
/* 0x10 */    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', 
/* 0x20 */    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '3', 
/* 0x30 */    '?', '?', '?', '?', '?', '?', '?', '?', '?', 'c', '?', '?', '?', '?', '?', '0', 
/* 0x40 */    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', 
/* 0x50 */    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '2', '?', '?', 'd', '?', 
/* 0x60 */    '?', '?', '?', '?', '?', '?', '4', '?', '?', '?', '?', '?', '?', '5', '?', '9', 
/* 0x70 */    '?', 'f', '?', '?', '?', '?', '?', 'a', '?', 'e', '?', '?', 'b', '6', '?', '8', 
};

char get_display_char(uint8_t dc) {
    dc = dc | 0x80;
    return display_map[dc];
}
void show_display() {
    printf("%c%c%c%c %c%c\n",
            get_display_char(display[0]),
            get_display_char(display[1]),
            get_display_char(display[2]),
            get_display_char(display[3]),
            get_display_char(display[4]),
            get_display_char(display[5]));
}

int main(int argc, char *argv[]) {
    load_roms();

    reset6502();
    for (;;) {
        step6502();
        printf("PC = %4x\n", pc);
        check_pc();
        if (display_changed) {
            if (current_time_millis() - display_changed_time > 500) {
                show_display();
                fflush(stdout);
                display_changed = 0;
            }
        }
        if (kbhit()) {
            handle_kb();
        }
    }
}

uint8_t read6502(uint16_t address) {
    if ((address >= 0x1c00) && (address < 0x2000)) {
        return riot003rom[address-0x1c00];
    } else if ((address >= 0x1800) && (address < 0x1c00)) {
        return riot002rom[address-0x1800];
    } else if (address < 0x400) {
        return ram[address];
    } else if (address >= 0xff00) {
        return riot003rom[address - 0xfc00];
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        return riot003ram[address - 0x1780];
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        return riot002ram[address - 0x17c0];
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        return riot003read(address);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        return riot002read(address);
    } else {
        printf("Unknown address\n");
    }
}

void write6502(uint16_t address, uint8_t value) {
    if (address < 0x400) {
        ram[address] = value;
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        riot003ram[address - 0x1780] = value;
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        riot002ram[address - 0x17c0] = value;
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        riot003write(address, value);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        riot002write(address, value);
    } else {
        printf("Write %08x to %x\n", value, address);
    }
}

uint8_t riot003read(uint16_t address) {
    return 0;
}

uint8_t riot002read(uint16_t address) {
    return 0;
}

void riot003write(uint16_t address, uint8_t value) {
}

void riot002write(uint16_t address, uint8_t value) {
}

