#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h> // For FIONREAD
#include <termios.h>
#include <stdbool.h>
#include <time.h>
#include <memory.h>

uint8_t ram[1024];

typedef struct TIMER {
    uint16_t timer_mult;
    uint8_t tick_accum;
    uint8_t timer_count;
    uint8_t timeout;
} TIMER;

typedef struct RIOT {
    uint8_t rom[1024];
    uint8_t ram[64];
    uint8_t padd, sad;
    uint8_t pbdd, sbd;
    TIMER timer;
} RIOT;

RIOT riot003;
RIOT riot002;

extern void reset6502();
extern void exec6502(uint32_t);
extern void step6502();
extern volatile uint16_t pc;
extern volatile uint8_t a, x, y, status;
extern volatile uint32_t clockticks6502;

void load_roms();
int kbhit();
long current_time_millis();
void check_pc();
void handle_kb();
void show_display();
uint8_t riot003read(uint16_t);
uint8_t riot002read(uint16_t);
void riot003write(uint16_t, uint8_t);
void riot002write(uint16_t, uint8_t);
void update_timer(TIMER *, uint32_t);
void reset_timer(TIMER *, int);
void write6502(uint16_t, uint8_t);

uint8_t display[6];
uint8_t display_changed;
long display_changed_time;

uint8_t char_pending;

uint8_t single_step;

struct timespec last_tick_time;

int main(int argc, char *argv[]) {
    uint32_t curr_ticks;
    struct timespec tv, nsleep;

    memset(&riot002, 0, sizeof(RIOT));
    memset(&riot003, 0, sizeof(RIOT));

    char_pending = 0x15;
    load_roms();
    write6502(0x17fa, 0);
    write6502(0x17fb, 0x1c);
    write6502(0x17fe, 0);
    write6502(0x17ff, 0x1c);

    reset6502();

    clock_gettime(CLOCK_REALTIME, &last_tick_time);

    for (;;) {


//        printf("pc=%04x  status=%02x  a=%02x  x=%02x  y=%02x\n", pc, status, a, x, y);

        clock_gettime(CLOCK_REALTIME, &tv);
        if (tv.tv_sec == last_tick_time.tv_sec) {
            if (tv.tv_nsec - last_tick_time.tv_nsec < 1000) {
                nsleep.tv_sec = 0;
                nsleep.tv_nsec = tv.tv_nsec - last_tick_time.tv_nsec;
                nanosleep(&nsleep, NULL);
            }
        }
        clockticks6502 = 0;

        step6502();

        clock_gettime(CLOCK_REALTIME, &last_tick_time);

        update_timer(&riot002.timer, clockticks6502);
        update_timer(&riot003.timer, clockticks6502);

        check_pc();

        if (display_changed) {
            if (current_time_millis() - display_changed_time > 100) {
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

int kbhit(void) {
    static bool initflag = false;
    static const int STDIN = 0;

    if (!initflag) {
        // Use termios to turn off line buffering
        struct termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        term.c_lflag &= ~ECHO;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initflag = true;
    }

    int nbbytes;
    ioctl(STDIN, FIONREAD, &nbbytes);  // 0 is STDIN
    return nbbytes;
}

long current_time_millis() {
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

void load_roms() {
    FILE *in;

    if ((in = fopen("6530-002.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot002.rom, 1, sizeof(riot002.rom), in);
    fclose(in);

    if ((in = fopen("6530-003.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot003.rom, 1, sizeof(riot003.rom), in);
    fclose(in);

}

void check_pc() {
    int digit;
    if (pc == 0x1f56) {
        digit = 9 - (x >> 1);
        if (display[digit] != a) {
            display_changed = 1;
            display[digit] = a;
            display_changed_time = current_time_millis();
        }
        pc = 0x1f5e;
    } else if (pc == 0x1c90) {
        char_pending = 0x15;
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
        printf("Address Mode\n");
        char_pending = 0x10;
    } else if (ch == 4) {
        printf("Data Mode\n");
        char_pending = 0x11;
    } else if (ch == 16) {
        printf("PC\n");
        char_pending = 0x14;
    } else if (ch == '+') {
        char_pending = 0x12;
    } else if (ch == 7) {
        printf("GO\n");
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
    } else {
        if (ch >= 0x20) {
            printf("Unknown char %c\n", ch);
        } else {
            printf("Unknown char %02x\n", ch);
        }
    }
}

char display_map[128] = {
/*              0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f */
/* 0x00 */    '~', '~', '~', '>', 'i', '~', '1', '7', '~', '~', '~', '~', '~', '~', '~', '~', 
/* 0x10 */    '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', 'w', '~', '~', '~', 
/* 0x20 */    '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', 
/* 0x30 */    '~', '~', '~', '~', '~', '~', '~', 'm', 'l', 'c', '~', '~', '~', 'g', 'u', '0', 
/* 0x40 */    '-', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '~', '3', 
/* 0x50 */    'r', '~', '~', '?', 'n', '~', '~', '~', '~', '~', '~', '2', 'o', '~', 'd', '~', 
/* 0x60 */    '~', '~', '~', '~', '~', '~', '4', '~', '~', '~', '~', '~', '~', '5', 'y', '9', 
/* 0x70 */    '~', 'f', '~', 'p', '~', '~', 'h', 'a', 't', 'e', '~', '~', 'b', '6', '~', '8', 
};

char get_display_char(uint8_t dc) {
    char ch;
    dc = dc & 0x7f;
    ch = display_map[dc];
    if (ch == '?') {
        printf("Display char for %02x (%02x) is unknown\n", dc, dc | 0x80);
    }
    return ch;
}
void show_display() {
    printf("%c%c%c%c %c%c\n",
            get_display_char(display[5]),
            get_display_char(display[4]),
            get_display_char(display[3]),
            get_display_char(display[2]),
            get_display_char(display[1]),
            get_display_char(display[0]));
}

uint8_t read6502(uint16_t address) {
    if ((address >= 0x1c00) && (address < 0x2000)) {
        return riot002.rom[address-0x1c00];
    } else if ((address >= 0x1800) && (address < 0x1c00)) {
        return riot003.rom[address-0x1800];
    } else if (address < 0x400) {
        return ram[address];
    } else if (address >= 0xff00) {
        return riot002.rom[address - 0xfc00];
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        return riot003.ram[address - 0x1780];
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        return riot002.ram[address - 0x17c0];
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        return riot003read(address);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        return riot002read(address);
    } else {
        return 0;
    }
}

void write6502(uint16_t address, uint8_t value) {
    if (address < 0x400) {
        ram[address] = value;
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        riot003.ram[address - 0x1780] = value;
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        riot002.ram[address - 0x17c0] = value;
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        riot003write(address, value);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        riot002write(address, value);
    } else {
        printf("Write %08x to %x\n", value, address);
    }
}

uint8_t riot003read(uint16_t address) {
    if (address == 0x1700) {
        return riot003.sad;
    } else if (address == 0x1701) {
        return riot003.padd;
    } else if (address == 0x1702) {
        return riot003.sad;
    } else if (address == 0x1703) {
        return riot003.pbdd;
    } else if ((address == 0x1706) || (address == 0x170e)) {
        if (riot002.timer.timeout) {
            riot002.timer.timeout = 0;
            riot002.timer.timer_count = 255;
            return 0;
        } else {
            return riot002.timer.timer_count;
        }
    } else if (address == 0x1707) {
        if (riot002.timer.timeout) {
            return 0x80;
        } else {
            return 0;
        }
    }
}

uint8_t key_bits[7] = { 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0xfe };

uint8_t riot002read(uint16_t address) {
    uint8_t sv;
    if (address == 0x1740) {
        sv = (riot002.sbd >> 1) & 0xf;
        if (sv == 0) {
            if (char_pending <= 6) {
                return key_bits[char_pending];
            } else {
                return 0xff;
            }
        } else if (sv == 1) {
            if ((char_pending >= 7) && (char_pending <= 13)) {
                return key_bits[char_pending-7];
            } else {
                return 0xff;
            }
        } else if (sv == 2) {
            if ((char_pending >= 14) && (char_pending <= 20)) {
                return key_bits[char_pending-14];
            } else {
                return 0xff;
            }
        } else if (sv == 3) {
            return 0xff;
        } else {
            return 0x80;
        }
    } else if (address == 0x1741) {
        return riot002.padd;
    } else if (address == 0x1742) {
        return riot002.sbd;
    } else if (address == 0x1743) {
        return riot002.pbdd;
    } else if ((address == 0x1746) || (address == 0x174e)) {
        if (riot002.timer.timeout) {
            riot002.timer.timeout = 0;
            riot002.timer.timer_count = 255;
            return 0;
        } else {
            return riot002.timer.timer_count;
        }
    } else if (address == 0x1747) {
        if (riot002.timer.timeout) {
            return 0x80;
        } else {
            return 0;
        }
    }
    return 0;
}

void riot003write(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x1700:
            riot003.sad = value;
            break;
        
        case 0x1701:
            riot003.padd = value;
            break;

        case 0x1702:
            riot003.sbd = value;
            break;

        case 0x1703:
            riot003.pbdd = value;
            break;

        case 0x1704:
            reset_timer(&riot003.timer, 1);
            break;

        case 0x1705:
            reset_timer(&riot003.timer, 8);
            break;

        case 0x1706:
            reset_timer(&riot003.timer, 64);
            break;

        case 0x1707:
            reset_timer(&riot003.timer, 1024);
            break;
    }
}

void riot002write(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x1740:
            riot002.sad = value;
            break;
        
        case 0x1741:
            riot002.padd = value;
            break;

        case 0x1742:
            riot002.sbd = value;
            break;

        case 0x1743:
            riot002.pbdd = value;
            break;

        case 0x1744:
            reset_timer(&riot002.timer, 1);
            break;

        case 0x1745:
            reset_timer(&riot002.timer, 8);
            break;

        case 0x1746:
            reset_timer(&riot002.timer, 64);
            break;

        case 0x1747:
            reset_timer(&riot002.timer, 1024);
            break;
    }
}

void reset_timer(TIMER *timer, int scale) {
    timer->timer_mult = scale;
    timer->tick_accum = 0;
    timer->timer_count = 255;
    timer->timeout = 0;
}

void update_timer(TIMER *timer, uint32_t ticks) {
    int num_timer_ticks;
    if (timer->timer_mult == 0) {
        return;
    }
    timer->tick_accum += ticks;
    if (ticks > timer->timer_mult) {
        num_timer_ticks = ticks / timer->timer_mult;
        if (timer->timer_mult == 1) {
            timer->tick_accum = 0;
        } else {
            timer->tick_accum = ticks % timer->timer_mult;
        }
        if (num_timer_ticks >= timer->timer_count) {
            timer->timer_count = 0;
            timer->timeout = 0;
        } else {
            timer->timer_count -= num_timer_ticks;
        }
    }
}
