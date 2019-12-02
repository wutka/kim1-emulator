#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t ram[1024];
uint8_t riot1rom[1024];
uint8_t riot1ram[64];
uint8_t riot2rom[1024];
uint8_t riot2ram[64];

extern void reset6502();
extern void exec6502(uint32_t);

uint8_t riot1read(uint16_t);
uint8_t riot2read(uint16_t);
void riot1write(uint16_t, uint8_t);
void riot2write(uint16_t, uint8_t);

void load_roms() {
    FILE *in;

    if ((in = fopen("6530-002.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot1rom, 1, sizeof(riot1rom), in);
    fclose(in);

    if ((in = fopen("6530-003.bin", "rb")) == NULL) {
        fprintf(stderr, "Can't open 6530-002.bin\n");
        exit(1);
    }
    fread(riot2rom, 1, sizeof(riot2rom), in);
    fclose(in);

}

int main(int argc, char *argv[]) {
    load_roms();
    reset6502();
    for (;;) {
        exec6502(1);
    }
}

uint8_t read6502(uint16_t address) {
    printf("Read %x\n", address);
    if ((address >= 0x1c00) && (address < 0x2000)) {
        return riot1rom[address-0x1c00];
    } else if ((address >= 0x1800) && (address < 0x1c00)) {
        return riot2rom[address-0x1800];
    } else if (address < 0x400) {
        return ram[address];
    } else if (address >= 0xff00) {
        return riot1rom[address - 0xfc00];
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        return riot1ram[address - 0x1780];
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        return riot2ram[address - 0x17c0];
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        return riot1read(address);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        return riot2read(address);
    } else {
        printf("Unknown address\n");
    }
}

void write6502(uint16_t address, uint8_t value) {
    if (address < 0x400) {
        ram[address] = value;
    } else if ((address >= 0x1780) && (address < 0x17c0)) {
        riot1ram[address - 0x1780] = value;
    } else if ((address >= 0x17c0) && (address < 0x1800)) {
        riot2ram[address - 0x17c0] = value;
    } else if ((address >= 0x1700) && (address < 0x1740)) {
        riot1write(address, value);
    } else if ((address >= 0x1740) && (address < 0x1780)) {
        riot2write(address, value);
    } else {
        printf("Write %08x to %x\n", value, address);
    }
}

uint8_t riot1read(uint16_t address) {
    return 0;
}

uint8_t riot2read(uint16_t address) {
    return 0;
}

void riot1write(uint16_t address, uint8_t value) {
}

void riot2write(uint16_t address, uint8_t value) {
}

