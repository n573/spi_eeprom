
/**
 * @author  Nathan Cauwet
 * @file    spi_flash.c
 * @brief   Adapted from spi_flash example. Specified for AT93C86A EEPROM
 * @date    14 April 2025
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"

#define EEPROM_CMD_READ   0b110  // Read command
#define EEPROM_CMD_WRITE  0b101  // Write command
#define EEPROM_CMD_ERASE  0b111  // Erase command
// #define EEPROM_CMD_WEN    0b100  // Write Enable command
#define EEPROM_CMD_WEN    0b10011  // Write Enable command
#define EEPROM_CMD_WDS    0b10000  // Write Disable command

static uint8_t dump_flag = 0;

/* static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
} */
static inline void cs_select(uint cs_pin) {
    gpio_put(cs_pin, 1);
    sleep_us(1); // Ensure CS setup time (tCSS) is met
}

static inline void cs_deselect(uint cs_pin) {
    gpio_put(cs_pin, 0);
    sleep_us(1); // Ensure CS hold time (tCSH) is met
}

void eeprom_write_enable(spi_inst_t *spi, uint cs_pin) {
    cs_deselect(cs_pin);
    cs_select(cs_pin);
    // uint16_t cmd = EEPROM_CMD_WEN << 4; // Command is 10 bits, padded to 16 bits
    uint16_t cmd = EEPROM_CMD_WEN << 11; // Command is 5 bits, padded to 16 bits
    // Debug: Print the bits of the command
    /* printf("cmd bits (EWEN): ");
    for (int i = 15; i >= 0; i--) {
        printf("%d", (cmd >> i) & 1);
        if (i % 4 == 0) printf(" ");
    }
    printf("\n"); */

    // uint16_t cmd = 0b1001100000000<<3;
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF}; // Split 16-bit command into two bytes
    spi_write_blocking(spi, cmdbuf, 2);         // Send the two bytes
    cs_deselect(cs_pin);
}
void eeprom_write_disable(spi_inst_t *spi, uint cs_pin) {
    cs_deselect(cs_pin);
    cs_select(cs_pin);
    uint16_t cmd = EEPROM_CMD_WDS << 11; // Command is 5 bits, padded to 16 bits
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF}; // Split 16-bit command into two bytes
    spi_write_blocking(spi, cmdbuf, 2);         // Send the two bytes
    cs_deselect(cs_pin);
}

void eeprom_read(spi_inst_t *spi, uint cs_pin, uint16_t addr, uint16_t *data) {
    cs_deselect(cs_pin);
    cs_select(cs_pin);
    uint16_t cmd = (EEPROM_CMD_READ << 13) | ((addr & 0x03FF) << 3); // 3-bit command + 10-bit address + 3 dummy bits
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};
    uint8_t databuf[2] = {0};
    spi_write_blocking(spi, cmdbuf, 2);
    /// Do not use spi_read_write_blocking because the address must be sent before receiving data
    spi_read_blocking(spi, 0, databuf, 2);
    *data = (databuf[0] << 7) | databuf[1]>>1; //< big-endian -- changed shift
    // *data = (databuf[0] << 8) | databuf[1]; //< big-endian
    // *data = (databuf[0] << 7) | databuf[1]; //< big-endian
    if(!dump_flag) {
        printf("databuf[0]: 0x%02X, databuf[1]: 0x%02X\n", databuf[0], databuf[1]);
    }
    cs_deselect(cs_pin);
}

void eeprom_write(spi_inst_t *spi, uint cs_pin, uint16_t addr, uint16_t data) {
    cs_deselect(cs_pin);
    sleep_us(1);
    cs_select(cs_pin);

    // Combine the 3-bit command, 10-bit address, and 16-bit data into a 29-bit value
    uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 27) | // 3-bit command
                   ((addr & 0x03FF) << 17) |           // 10-bit address
                   (data & 0xFFFF);                    // 16-bit data

    cmd <<= 1;
    // Debug: Print the cmd value
    printf("cmd: 0x%08X\n", cmd);

    // Split the 32-bit cmd into 4 bytes for SPI transmission
    uint8_t cmdbuf[4] = {
        (cmd >> 24) & 0xFF,
        (cmd >> 16) & 0xFF,
        (cmd >> 8) & 0xFF,
        cmd & 0xFF
    };

    // Debug: Print the cmdbuf values
    // printf("cmdbuf: %02X %02X %02X %02X\n", cmdbuf[0], cmdbuf[1], cmdbuf[2], cmdbuf[3]);

    // Perform the SPI write operation
    spi_write_blocking(spi, cmdbuf, 4);
    sleep_ms(10); // Wait for the maximum write cycle time to complete
    cs_deselect(cs_pin);
}
/* void eeprom_write(spi_inst_t *spi, uint cs_pin, uint16_t addr, uint16_t data) {
    cs_deselect(cs_pin);
    sleep_us(1);
    cs_select(cs_pin);
    // Combine the 3-bit command, 10-bit address, and 16-bit data into a 29-bit value
    // uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 26) | ((addr & 0x03FF) << 16) | (data & 0xFFFF);
    // uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 29) | ((addr & 0x03FF) << 16) | (data & 0xFFFF);
    // uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 28) | ((addr & 0x03FF) << 16) | (data & 0xFFFF);
    
    //! TRYING THIS BECAUSE OF SHIFTING ERROR
    uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 26) | ((addr & 0x03FF) << 16);
    // Shift the 29-bit value to align with the MSB of the 32-bit cmd
    cmd <<= 3;
    // cmd <<= 2;
    cmd = cmd | (data & 0xFFFF);

    // Debug: Print the bits of the command
    // printf("cmd bits: ");
    // for (int i = 31; i >= 0; i--) {
    //     printf("%d", (cmd >> i) & 1);
    //     if (i % 4 == 0) printf(" ");
    // }
    // printf("\n");

    // Split the 32-bit cmd into 4 bytes for SPI transmission
    uint8_t cmdbuf[4] = {
        (cmd >> 24) & 0xFF, // Upper 8 bits
        (cmd >> 16) & 0xFF, // Next 8 bits
        (cmd >> 8) & 0xFF,  // Next 8 bits
        cmd & 0xFF          // Lower 8 bits
    };
    printf("cmdbuf: %02X %02X %02X %02X\n", cmdbuf[0], cmdbuf[1], cmdbuf[2], cmdbuf[3]);

    // Perform the SPI write operation
    cs_select(cs_pin);
    spi_write_blocking(spi, cmdbuf, 4);
    sleep_ms(10); // Wait for the maximum write cycle time to complete
    cs_deselect(cs_pin);
} */

void eeprom_erase(spi_inst_t *spi, uint cs_pin, uint16_t addr) {
    cs_deselect(cs_pin);
    // eeprom_write_enable(spi, cs_pin);
    cs_select(cs_pin);
    uint16_t cmd = (EEPROM_CMD_ERASE << 13) | ((addr & 0x03FF) << 3); // 3-bit command + 10-bit address + 3 dummy bits
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};
    spi_write_blocking(spi, cmdbuf, 2);
    cs_deselect(cs_pin);
    sleep_ms(7); // Wait for erase cycle to complete
}

void eeprom_dump(spi_inst_t *spi, uint cs_pin) {
    uint16_t data;
    dump_flag = 1;
    printf("\nEEPROM Memory Dump:\n");
    printf("Addr  | Data\n");
    printf("------+-------\n");
    
    for (uint16_t addr = 0; addr <= 0x03FF; addr++) {
        eeprom_read(spi, cs_pin, addr, &data);
        if (addr % 16 == 0) {
            printf("\n%04X  | ", addr);
        }
        printf("%04X ", data);
    }
    printf("\n");
    dump_flag = 0;
}

int main() {
    stdio_init_all();
    sleep_ms(5000);

    printf("EEPROM example\n");

    spi_init(spi_default, 1000 * 1000);
    // spi_init(spi_default, 500 * 1000);
    // spi_init(spi_default, 250 * 1000);
    // spi_set_format(spi_default, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); //< SPI Mode 0
    // spi_set_format(spi_default, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); //< SPI Mode 3
    // spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); //< SPI Mode 0
    spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); //< SPI Mode 3
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);

    eeprom_write_enable(spi_default, PICO_DEFAULT_SPI_CSN_PIN);
    /// @note Once in the EWEN state, programming remains enabled until an EWDS instruction is executed 
    ///\ or VCC power is removed from the part.

    uint16_t data;

    // Erase word at address 0x10
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00);
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x10);
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x20);
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11);

    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00, 0xBABA);
    // Write word to address 0x10
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x10, 0xDEAD);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x20, 0xBEEF);
    
    // Read at 0x00
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00, &data);
    printf("Read data at 0x00: 0x%04X\n", data);
    // Read word from address 0x10
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x10, &data);
    printf("Read data at 0x10: 0x%04X\n", data);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x20, &data);
    printf("Read data at 0x20: 0x%04X\n", data);
    
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x10);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x10, &data);
    printf("Read data at 0x10 after erase: 0x%04X\n", data);

    #define DEBUG_COUNT 3
    for(int i=0; i<DEBUG_COUNT; i++) {
        eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11, 0xFEED);
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11, (0xF000 + i));
        eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11, &data);
        printf("Read data at 0x11: 0x%04X\n", data);
    }

    for(int i=0; i<=0xF; i++) {
        eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3F0+i);
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3F0+i, (0x0C00 + (i<<1))); // should increment by 2s
    }
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x220);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x220, 0xF1C2);

    eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);

    /* char nc[2] = {'N', 'C'};
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xAA);
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xAA, *nc);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xAA, 'N');
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xAA, nc[0]);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xAA, &data);
    printf("Read data at 0xAA: %c\n", data);
    printf("Read data at 0xAA: %04x\n", data); */
    /* eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3FF);
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3FF, 0xAAAA);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3FF, 0xABBA);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3FF, &data);
    printf("Read data at 0x3FF: 0x%04X\n", data);
    
    // For scope debug
    // eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xA0);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xA0, 0x1234); */
    // eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0xA0, &data);
    // printf("Read data at 0xA0: 0x%04X\n", data);

    while (1) {
        sleep_ms(1000);
        tight_loop_contents();
        // cs_select(PICO_DEFAULT_SPI_CSN_PIN);
        // sleep_ms(10);
        // cs_deselect(PICO_DEFAULT_SPI_CSN_PIN);
        
        // eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00, &data);
        // printf("Read data at 0x10: 0x%04X\n", data);
        // eeprom_write_enable(spi_default, PICO_DEFAULT_SPI_CSN_PIN);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00, 0xABCD);
        // eeprom_write_enable(spi_default, PICO_DEFAULT_SPI_CSN_PIN);
        // eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3FF, &data);
        // printf("Read data at 0x3FF: 0x%04X\n", data);
    }

    return 0;
}


/* Timing Info from Copilot

1. SPI Clock Frequency
The maximum SPI clock frequency (fSCK) is typically 2 MHz for the AT93C86A. Ensure that the SPI clock is configured to not exceed this limit. In your code, the SPI is initialized with spi_init(spi_default, 1000 * 1000);, which sets the clock to 1 MHz, so it is within the acceptable range.
2. Chip Select (CS) Timing
CS Setup Time (tCSS): The minimum time the CS pin must be held low before the first clock edge. Typically 250 ns.
CS Hold Time (tCSH): The minimum time the CS pin must be held low after the last clock edge. Typically 250 ns.
CS High Time (tCS): The minimum time the CS pin must be held high between operations. Typically 1 µs.
3. Clock Timing
Clock High Time (tCH): The minimum time the clock signal must remain high. Typically 250 ns.
Clock Low Time (tCL): The minimum time the clock signal must remain low. Typically 250 ns.
4. Data Timing
Data Setup Time (tDSU): The minimum time data must be stable before the clock edge. Typically 50 ns.
Data Hold Time (tDH): The minimum time data must remain stable after the clock edge. Typically 50 ns.
5. Write Cycle Time
Write Cycle Time (tWC): The time required to complete a write operation. Typically 5 ms. This is why your code includes a sleep_ms(5); after write and erase operations.
6. Erase Timing
The erase cycle time is also typically 5 ms, similar to the write cycle time.
7. Power-Up Timing
Power-Up to Ready Time (tPU): The time required after power-up before the chip is ready for operation. Typically 1 ms.
8. Timing in Your Code
The asm volatile("nop \n nop \n nop"); in the cs_select and cs_deselect functions may be used to introduce small delays to meet the setup and hold timing requirements for the CS pin. However, these delays may need to be adjusted based on the actual clock speed and timing requirements.
9. SPI Mode
The AT93C86A operates in SPI Mode 0 or Mode 3:
Clock polarity (CPOL) = 0 or 1.
Clock phase (CPHA) = 0.
Ensure that these timing requirements are met in your implementation to avoid communication errors with the EEPROM chip.

*/