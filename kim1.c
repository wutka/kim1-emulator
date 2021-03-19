#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h> // For FIONREAD
#include <termios.h>
#include <stdbool.h>
#include <time.h>
#include <memory.h>
#include <ctype.h>
#include <unistd.h>

uint8_t ram[65536];

typedef struct TIMER {
    uint16_t timer_mult;
    uint16_t tick_accum;
    uint8_t start_value;
    uint8_t timer_count;
    uint8_t timeout;
#ifdef REAL_TIMER
    uint64_t starttime;
#endif
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
extern void nmi6502();
extern volatile uint16_t pc;
extern volatile uint8_t a, x, y, status;
extern volatile uint32_t clockticks6502;

void load_roms();
int kbhit(bool);
int reset_term();
long current_time_millis();
void do_step();
void check_pc();
void handle_kb();
void show_display();
uint8_t riot003read(uint16_t);
uint8_t riot002read(uint16_t);
void riot003write(uint16_t, uint8_t);
void riot002write(uint16_t, uint8_t);
void update_timer(TIMER *, uint32_t);
void reset_timer(TIMER *, int, uint8_t);
void read_string(char *, int);

uint8_t read6502(uint16_t);
void write6502(uint16_t, uint8_t);

uint8_t display[6];
uint8_t display_changed;
long display_changed_time;

uint8_t char_pending;

uint8_t single_step;

struct timespec last_tick_time;

char input_line[512];

uint8_t sending_serial;
uint8_t serial_out_count;
uint8_t serial_out_byte;
uint8_t serial_out_bit_ready;

uint8_t serial_in_byte;
uint8_t kim1_serial_mode;

#define SERIAL_IN_QUEUE_SIZE 1024
uint8_t serial_in_queue[SERIAL_IN_QUEUE_SIZE];
int serial_in_queue_start = 0;
int serial_in_queue_end = 0;

int max_ram = 1024;

char paper_tape_filename[1024];
FILE *paper_tape_file = NULL;
int auto_tape = 1;
int reading_paper_tape = 0;
int writing_paper_tape = 0;

uint8_t trace;
int main(int argc, char *argv[]) {
    uint32_t curr_ticks;
    struct timespec tv, nsleep;
    uint8_t enable_SST_NMI;

    for (int i=1; i < argc; i++) {
        if (!strcmp(argv[i], "-ram") || !strcmp(argv[i], "--ram")) {
            if (i >= argc-1) {
                printf("Must specify ram size (1k,2k,3k,4k,5k or full)\n");
                exit(1);
            }
            if (!strcmp(argv[i+1], "full")) {
                max_ram = 65536;
            } else if (isdigit(argv[i+1][0]) && (argv[i+1][1] == 'k' || (argv[i+1][1] == 'K'))) {
                int ram_size = argv[i+1][0] - '0';
                if ((ram_size < 1) || (ram_size > 5)) {
                    printf("Ram size must be between 1k and 5k\n");
                    exit(1);
                }
                max_ram = 1024 * ram_size;
            }
            i++;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-help") ||
            !strcmp(argv[i], "--h") || !strcmp(argv[i], "--help")) {
            printf("Usage:  kim1 [-ram size] [-autotape y/n]\n  where size = 1k, 2k, 3k, 4k, or 5k\n");
            printf("\nThe ram size currently specifies the amount of memory available below\n");
            printf("the ROM. The ROM starts at 17E7, which is just below 6K, so for now\n");
            printf("it is limited to 5k, leaving about 1000 bytes unavailable.\n");
            printf("The autotape option controls whether the emulator prompts you for a\n");
            printf("filename when you load or save a paper tape. For the save, do the\n");
            printf("normal routine of putting the length at 17F7-F8, and jumping to the\n");
            printf("start address, it will prompt for a save filename when you hit Q.\n");
            exit(0);
        } else if (!strcmp(argv[i], "-autotape")) {
            if (i >= argc-1) {
                printf("Must specify y or n for autotape\n");
                exit(1);
            }
            if ((argv[i+1][0] == 'y') || (argv[i+1][0] == 'Y')) {
                auto_tape = 1;
            } else if ((argv[i+1][0] == 'n') || (argv[i+1][0] == 'N')) {
                auto_tape = 0;
            } else {
                printf("Must specify y or n for autotape\n");
                exit(1);
            }
        }
    }
    // Initialize the RIOT chips
    memset(&riot002, 0, sizeof(RIOT));
    memset(&riot003, 0, sizeof(RIOT));

    // No character pending
    char_pending = 0x15;

    sending_serial = 0;
    kim1_serial_mode = 0;

    // Load the 2 ROM files
    load_roms();

    // Set the vectors that the KIM-1 ROM uses
    write6502(0x17fa, 0);
    write6502(0x17fb, 0x1c);
    write6502(0x17fe, 0);
    write6502(0x17ff, 0x1c);

    // Turn single step off
    single_step = 0;

    // Reset the CPU
    reset6502();

    clock_gettime(CLOCK_REALTIME, &last_tick_time);

    trace = 0;

    for (;;) {


        if (trace) {
            printf("pc=%04x  status=%02x  a=%02x  x=%02x  y=%02x   sbd=%02x\n", pc, status, a, x, y, riot002.sbd);
        }

        // Try to simulate a 1MHz clock speed
        clock_gettime(CLOCK_REALTIME, &tv);
        if (tv.tv_sec == last_tick_time.tv_sec) {
            if (tv.tv_nsec - last_tick_time.tv_nsec < 1000) {
                nsleep.tv_sec = 0;
                nsleep.tv_nsec = tv.tv_nsec - last_tick_time.tv_nsec;
                nanosleep(&nsleep, NULL);
            }
        }

        // This seems like a hack but it's basically how the hardware does it
        enable_SST_NMI = single_step && (pc < 0x1c00);

        do_step();

        if (single_step && enable_SST_NMI) {
            nmi6502();
        }

        // Check where the CPU is
        check_pc();

        // If the display has changed, update it, but no faster than every 100ms
        // since we don't need to see the result of every keystroke
        if (display_changed && !kim1_serial_mode) {
            if (current_time_millis() - display_changed_time > 100) {
                show_display();
                fflush(stdout);
                display_changed = 0;
            }
        }
        
        // If a key has been hit, process it
        if (kbhit(false)) {
            handle_kb();
        }
    }
}

int serial_in_queue_ready() {
    return serial_in_queue_start != serial_in_queue_end;
}

void serial_in_queue_put(uint8_t b) {
    serial_in_queue[serial_in_queue_end] = b;
    serial_in_queue_end = (serial_in_queue_end + 1) % SERIAL_IN_QUEUE_SIZE;
}

uint8_t serial_in_queue_get() {
    uint8_t b;
    if (serial_in_queue_start == serial_in_queue_end) return 0;
    b = serial_in_queue[serial_in_queue_start];
    serial_in_queue_start = (serial_in_queue_start + 1) % SERIAL_IN_QUEUE_SIZE;
    return b;
}

void do_step() {
    // Reset the CPU tick count so we can get the number of ticks
    // this instruction took
    clockticks6502 = 0;
    step6502();
    clock_gettime(CLOCK_REALTIME, &last_tick_time);

    // Update the 6530 timers
    update_timer(&riot002.timer, clockticks6502);
    update_timer(&riot003.timer, clockticks6502);
}

void set_raw() {
    static const int STDIN = 0;

    // Use termios to turn off line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ECHO;
    term.c_iflag &= ~ICRNL;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
}

int kbhit(bool init) {
    static bool initflag = false;
    static const int STDIN = 0;

    // If raw mode hasn't been turned on yet, turn it on
    if (init || !initflag) {
        set_raw();
        initflag = true;
    }

    // Return the number of bytes available to read
    int nbbytes;
    ioctl(STDIN, FIONREAD, &nbbytes);  // 0 is STDIN
    return nbbytes;
}

int reset_term() {
    static const int STDIN = 0;

    // Use termios to turn on line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag |= ICANON;
    term.c_lflag |= ECHO;
    term.c_iflag |= ICRNL;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
}

long current_time_millis() {
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

uint64_t current_time_nanos() {
    struct timespec tv;
    uint64_t ct;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000000000 + tv.tv_nsec;
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

/* check_pc is a hack to make the simulator a little smoother.
 * It traps the call to display digits, but only late into the
 * processing so programs like Wumpus that display non-standard
 * values can still work. */
void check_pc() {
    int digit, n, tap_ch;
    char ch;
    char *filename;
    if (pc == 0x1f56) {
        digit = 9 - (x >> 1);
        if (display[digit] != a) {
            if (!display_changed) {
                display_changed_time = current_time_millis();
            }
            display_changed = 1;
            display[digit] = a;
        }
        pc = 0x1f5e;
    } else if ((pc == 0x1f79) || (pc == 0x1f90)) {
 // If we get to the place where a character has been read,
 // clear out the pending keyboard character.
        char_pending = 0x15;
    } else if (pc == 0x1e5a) {
        if (serial_in_queue_ready()) {
            pc = 0x1e85;
            a = serial_in_queue_get();
            y = 0xff;
        } else if (reading_paper_tape) {
            if ((tap_ch = fgetc(paper_tape_file)) != EOF) {
                pc = 0x1e85;
                a = (uint8_t) tap_ch;
                y = 0xff;
            } else {
                fclose(paper_tape_file);
                reading_paper_tape = 0;
                printf("Tape loaded.\n");
            }
        }
    } else if (pc == 0x1e04) {
        if (!auto_tape) {
            return;
        }
        reset_term();
        for (;;) {
            printf("Read from file: ");
            fflush(stdout);
            n = read(0, paper_tape_filename, sizeof(paper_tape_filename)-1);
            while ((n > 0) && ((paper_tape_filename[n-1] == 10) || (paper_tape_filename[n-1] == 13))) {
                paper_tape_filename[--n] = 0;
            }
            if (n <= 0) {
                pc = 0x1c6a;
                break;
            }
            paper_tape_filename[n] = 0;
            if (!strcmp(paper_tape_filename, "-")) {
                break;
            }
            if (strlen(paper_tape_filename) > 0) {
                paper_tape_file = fopen(paper_tape_filename, "r");
                if (paper_tape_file == NULL) {
                    perror("fopen");
                    fflush(stderr);
                    continue;
                }
                reading_paper_tape = 1;
            }
            break;
        }
        set_raw();
    } else if (pc == 0x1e01) {
        reset_term();
        for (;;) {
            printf("Write to file: ");
            fflush(stdout);
            n = read(0, paper_tape_filename, sizeof(paper_tape_filename)-1);
            while ((n > 0) && ((paper_tape_filename[n-1] == 10) || (paper_tape_filename[n-1] == 13))) {
                paper_tape_filename[--n] = 0;
            }
            if (n <= 0) {
                pc = 0x1c6a;
                break;
            }
            paper_tape_filename[n] = 0;
            if (!strcmp(paper_tape_filename, "-")) break;
            if (strlen(paper_tape_filename) > 0) {
                paper_tape_file = fopen(paper_tape_filename, "w");
                if (paper_tape_file == NULL) {
                    perror("fopen");
                    fflush(stderr);
                    continue;
                }
                writing_paper_tape = 1;
            }
            break;
        }
        set_raw();
    } else if (pc == 0x1d77) {
        if (writing_paper_tape) {
            printf("Tape saved.\n");
            fclose(paper_tape_file);
            writing_paper_tape = 0;
        }
    }
}

/* Handle local keyboard interaction. Keys are converted to the keycodes
 * that the KIM-1 ROM expects. They keys are made to match the ones for
 * the KIM-UNO simulator, plus 'l' to load a binary filename. */
void handle_kb() {
    char ch;
    int len;
    uint16_t addr;
    FILE *loadfile;

    ch = getchar();

    if (kim1_serial_mode) {
        if (ch == 9) {
            kim1_serial_mode = 0;
            printf("Exiting KIM-1 Serial Mode\n");
            display_changed = 1;
        } else if (ch == 8) {
            serial_in_queue_put(0x7f);
        } else {
            serial_in_queue_put(ch);
        }
        return;
    }

    if ((ch >= '0') && (ch <= '9')) {
        char_pending = ch - '0';
    } else if ((ch >= 'a') && (ch <= 'f')) {
        char_pending = 10 + ch - 'a';
    } else if (ch == 1) {           // Ctrl-A
        printf("Address Mode\n");
        char_pending = 0x10;
    } else if (ch == 4) {           // Ctrl-D
        printf("Data Mode\n");
        char_pending = 0x11;
    } else if (ch == 16) {          // Ctrl-P
        printf("PC\n");
        display_changed=1;
        char_pending = 0x14;
    } else if (ch == '+') {
        char_pending = 0x12;
    } else if (ch == 7) {           // Ctrl-G
        printf("GO\n");
        char_pending = 0x13;
    } else if (ch == 18) {          // Ctrl-R
        printf("RESET\n");
        reset6502();
    } else if (ch == 20) {          // Ctrl-T
        nmi6502();
    } else if (ch == 0x1b) {        // Ctrl-[
        printf("Single step OFF\n");
        single_step = 0;
    } else if (ch == 0x1d) {        // Ctrl-]
        printf("Single step ON\n");
        single_step = 1;
    } else if (ch == 'l') {
        reset_term();
        printf("Enter filename: ");
        fgets(input_line, sizeof(input_line)-1, stdin);
        len = strlen(input_line);
        if ((len > 0) && (input_line[len-1] == '\n')) {
            input_line[len-1] = 0;
        }
        printf("Enter load address: ");
        addr = 0;
        for (;;) {
            ch = getchar();
            if ((ch >= '0') && (ch <= '9')) {
                addr = ((addr << 4) | (ch - '0')) & 0xffff;
            } else if ((ch >= 'a') && (ch <= 'f')) {
                addr = ((addr << 4) | (ch - 'a' + 10)) & 0xffff;
            } else if ((ch >= 'A') && (ch <= 'F')) {
                addr = ((addr << 4) | (ch - 'A' + 10)) & 0xffff;
            } else if ((ch == '\n') || (ch == '\r')) {
                break;
            }
        }
        if (addr >= max_ram) {
            printf("Load address is not in RAM");
            kbhit(true);
            return;
        }
        if ((loadfile = fopen(input_line, "rb")) == NULL) {
            printf("Unable to open file %s\n", input_line);
            kbhit(true);
            return;
        }
        fread(&ram[addr], 1, max_ram-addr, loadfile);
        printf("File loaded at %04x\n", addr);
        fflush(stdout);
        kbhit(true);
        reset6502();
        return;
    } else if (ch == 9) {
        kim1_serial_mode = 1;
        printf("Entering KIM-1 Serial Mode\n");
    } else if (ch == 'x') {
        reset_term();
        exit(0);
    } else {
        if (ch >= 0x20) {
            printf("Unknown char %c\n", ch);
        } else {
            printf("Unknown char %02x\n", ch);
        }
    }
}

/* The display map converts patterns of LEDs to their closest letter. It should support all
 * the characters in the Wumpus game. */
char display_map[128] = {
/*              0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f */
/* 0x00 */    ' ', '~', '~', '>', 'i', '~', '1', '7', '~', '~', '~', '~', '~', '~', '~', '~', 
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

/* Callback from the fake6502 library, handle reads from RAM or the RIOT chips */
uint8_t read6502(uint16_t address) {
    if ((address >= 0x1c00) && (address < 0x2000)) {
        return riot002.rom[address-0x1c00];
    } else if ((address >= 0x1800) && (address < 0x1c00)) {
        return riot003.rom[address-0x1800];
    } else if ((address >= 0x9c00) && (address < 0xa000)) {
        return riot002.ram[address - 0x9c00];
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
    } else if (address < max_ram) {
        return ram[address];
    } else {
        return 0;
    }
}

/* Callback from the fake6502 library, handle writes to RAM or the RIOT chips */
void write6502(uint16_t address, uint8_t value) {
    if ((address >= 0x1780) && (address < 0x17c0)) {
        riot003.ram[address - 0x1780] = value;
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        riot002.ram[address - 0x17c0] = value;
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        riot003write(address, value);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        riot002write(address, value);
    } else if (address < max_ram) {
        ram[address] = value;
    } else {
        printf("Write %02x to %04x\n", value, address);
    }
}

/* Handle reads from the 003 RIOT chip, which mostly do nothing */
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
        if (riot003.timer.timeout) {
            reset_timer(&riot003.timer, riot003.timer.timer_mult, riot003.timer.start_value);
            riot003.timer.timeout = 0;
            riot003.timer.timer_count = 255;
            return 0;
        } else {
            return riot003.timer.timer_count;
        }
    } else if (address == 0x1707) {
        if (riot003.timer.timeout) {
            return 0x80;
        } else {
            return 0;
        }
    }
}

// key_bits holds the bit patterns for a key depressed on
// each keyboard row (3 rows, 7 keys).
uint8_t key_bits[7] = { 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0xfe };

uint8_t riot002read(uint16_t address) {
    uint8_t sv, nextval;
    if (address == 0x1740) {
        sv = (riot002.sbd >> 1) & 0xf;
        // Return the correct key_bits if the current key depressed
        // belongs to the right scan row, otherwise 0xff, meaning
        // nothing on that row is depressed.
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
            if (kim1_serial_mode) {
                return 0;
            }
            return 0xff;
        } else {
            return 0x80;
        }
    } else if (address == 0x1741) {
        return riot002.padd;
    } else if (address == 0x1742) {
        if (sending_serial) {
            serial_out_bit_ready = 1;
        }
        return riot002.sbd;
    } else if (address == 0x1743) {
        return riot002.pbdd;
    } else if ((address == 0x1746) || (address == 0x174e)) {
        if (riot002.timer.timeout) {
            reset_timer(&riot002.timer, riot002.timer.timer_mult, riot002.timer.start_value);
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
            reset_timer(&riot003.timer, 1, value);
            break;

        case 0x1705:
            reset_timer(&riot003.timer, 8, value);
            break;

        case 0x1706:
            reset_timer(&riot003.timer, 64, value);
            break;

        case 0x1707:
            reset_timer(&riot003.timer, 1024, value);
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
            if (!sending_serial && ((value & 1) == 0)) {
                sending_serial = 1;
                serial_out_count = 0;
                serial_out_byte = 0;
                serial_out_bit_ready = 0;
            } else if (sending_serial && serial_out_bit_ready) {
                if (serial_out_count == 8) {
                    if (writing_paper_tape) {
                        if (serial_out_byte != 0) {
                            fwrite(&serial_out_byte, 1, 1, paper_tape_file);
                        }
                    } else {
                        printf("%c", serial_out_byte);
                        fflush(stdout);
                    }
                    sending_serial = 0;
                }
                serial_out_byte = ((serial_out_byte >> 1) & 0x7f) | ((value & 1) << 7);
                serial_out_count++;
                serial_out_bit_ready = 0;
            }
            break;

        case 0x1743:
            riot002.pbdd = value;
            break;

        case 0x1744:
            reset_timer(&riot002.timer, 1, value);
            break;

        case 0x1745:
            reset_timer(&riot002.timer, 8, value);
            break;

        case 0x1746:
            reset_timer(&riot002.timer, 64, value);
            break;

        case 0x1747:
            reset_timer(&riot002.timer, 1024, value);
            break;
    }
}

#ifdef REAL_TIMER
void reset_timer(TIMER *timer, int scale, uint8_t start_value) {
    timer->timer_mult = scale;
    timer->timeout = 0;
    timer->start_value = start_value;
    timer->timer_count = start_value;
    timer->starttime = current_time_nanos();
}

// This assumes a 1-MHz clock speed for the 6530 chip

void update_timer(TIMER *timer, uint32_t ticks) {
    uint64_t curr_time = current_time_nanos();
    if ((timer->timer_mult == 0) || timer->timeout) {
        return;
    }
    if ((curr_time -timer->starttime)/ 1000 >= timer->start_value * timer->timer_mult) {
        timer->timeout = 1;
        timer->timer_count = 0;
    } else {
        timer->timer_count = timer->start_value - (curr_time - timer->starttime) / 1000;
    }
}

#else
void reset_timer(TIMER *timer, int scale, uint8_t start_value) {
    timer->timer_mult = scale;
    timer->tick_accum = 0;
    timer->start_value = start_value;
    timer->timer_count = start_value;
    timer->timeout = 0;
}

// This assumes a 1-MHz clock speed for the 6530 chip (i.e. using the CPU ticks to
// count down the timer).
void update_timer(TIMER *timer, uint32_t ticks) {
    int num_timer_ticks;
    if (timer->timer_mult == 0) {
        return;
    }
    if (timer->timeout) {
        return;
    }
    timer->tick_accum += ticks;
    if (timer->tick_accum > timer->timer_mult) {
        num_timer_ticks = timer->tick_accum / timer->timer_mult;
        if (timer->timer_mult == 1) {
            timer->tick_accum = 0;
        } else {
            timer->tick_accum = timer->tick_accum % timer->timer_mult;
        }
        if (num_timer_ticks >= timer->timer_count) {
            timer->timer_count = 0;
            timer->timeout = 1;
        } else {
            timer->timer_count -= num_timer_ticks;
        }
    }
}
#endif
