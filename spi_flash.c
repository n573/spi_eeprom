/**
 * @author  Nathan Cauwet
 * @file    spi_flash.c
 * @brief   Adapted from spi_flash example. Specified for AT93C86A EEPROM
 * @date    14 April 2025
 * @details EEPROM is big-endian (MSB first). 
 * \details Command does not have to line up with MSB because it includes SB (StartBit) 
 * \details SPI Mode 0 or 3 (I noticed no difference in results)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include <string.h>

#define EEPROM_CMD_READ   0b110  // Read command
#define EEPROM_CMD_WRITE  0b101  // Write command
#define EEPROM_CMD_ERASE  0b111  // Erase command
#define EEPROM_CMD_WEN    0b10011  // Write Enable command
#define EEPROM_CMD_WDS    0b10000  // Write Disable command

static uint8_t dump_flag = 0;

static inline void delay_250ns() {
    /// @note B-Series uses 133MHz clock rather than 125MHz, adjust accordingly
    // setting loop to 4 iterations yields 316ns @ 125MHz clock
    // setting loop 3 iterations yields 240ns
    /// @attention these results establish that one iteration of this loop is about 80ns (@125MHz)
    /// \\ at 133MHz this should come out to about 85ns per iteration
    // for (int i = 0; i <= 3; i++) {
    for (int i = 0; i <= 4; i++) {
        asm volatile("nop");
    }
}
static inline void delay_500ns() {
    for (int i = 0; i < 10; i++) {
        asm volatile("nop");
    }
}

static inline void cs_select(uint cs_pin) {
    gpio_put(cs_pin, 1);
    // sleep_us(1); // Ensure CS setup time (tCSS) is met
    delay_250ns();
}

static inline void cs_deselect(uint cs_pin) {
    gpio_put(cs_pin, 0);
    // sleep_us(1); // Ensure CS hold time (tCSH) is met
    delay_250ns();
}

void eeprom_write_enable(spi_inst_t *spi, uint cs_pin) {
    cs_deselect(cs_pin);
    cs_select(cs_pin);
    uint16_t cmd = EEPROM_CMD_WEN << 11; // Command is 5 bits, padded to 16 bits
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
    if (!dump_flag) {
        cs_deselect(cs_pin);
        cs_select(cs_pin);
    }

    // Construct the read command: 3-bit command + 10-bit address
    uint16_t cmd = (EEPROM_CMD_READ << 10) | (addr & 0x03FF);
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};
    uint8_t databuf[3] = {0}; // 3 bytes to accommodate the dummy bit and 16 data bits

    // Send the read command
    spi_write_blocking(spi, cmdbuf, 2);

    // Read 17 bits (2 bytes + 1 extra bit for the dummy bit)
    spi_read_blocking(spi, 0, databuf, 3);

    // Combine the 17 bits into a 16-bit value
    // Shift the first two bytes left by 1 to discard the dummy bit
    *data = ((uint16_t)(databuf[0] << 9)) | ((uint16_t)(databuf[1] << 1)) | ((databuf[2] >> 7) & 0x01);

    // Debug output (optional)
    #ifdef DEBUG
    if (!dump_flag) {
        // printf("databuf[0]: 0x%02X, databuf[1]: 0x%02X, databuf[2]: 0x%02X\n", databuf[0], databuf[1], databuf[2]);
        printf("Read data (after alignment): 0x%04X\n", *data);
    }
    #endif

    if (!dump_flag) {
        cs_deselect(cs_pin);
        cs_select(cs_pin); // Ensure repeatability
    }
}

void eeprom_write(spi_inst_t *spi, uint cs_pin, uint16_t addr, uint16_t data) {
    cs_deselect(cs_pin);
    // sleep_us(1);
    cs_select(cs_pin);

    // Combine the 3-bit command, 10-bit address, and 16-bit data into a 29-bit value
    uint32_t cmd = ((uint32_t)EEPROM_CMD_WRITE << 26) | // 3-bit command
                   ((addr & 0x03FF) << 16) |           // 10-bit address
                   (data & 0xFFFF);                    // 16-bit data
// #define DEBUG
    // Debug: Print the cmd value
    #ifdef DEBUG
    printf("cmd: 0x%08X\n", cmd);
    #endif
    // Split the 32-bit cmd into 4 bytes for SPI transmission
    uint8_t cmdbuf[4] = {
        (cmd >> 24) & 0xFF,
        (cmd >> 16) & 0xFF,
        (cmd >> 8) & 0xFF,
        cmd & 0xFF
    };

    // Debug: Print the cmdbuf values
    #ifdef DEBUG
    printf("cmdbuf: %02X %02X %02X %02X\n", cmdbuf[0], cmdbuf[1], cmdbuf[2], cmdbuf[3]);
    #endif

    // Perform the SPI write operation
    spi_write_blocking(spi, cmdbuf, 4);
    // sleep_ms(10); // Wait for the maximum write cycle time to complete
    // sleep_ms(4); // Wait for the typical write cycle time to complete
    sleep_ms(7); // Wait for between the typical and the maximum write cycle time to complete
    cs_deselect(cs_pin);
    cs_select(cs_pin); // for repeatability (so that the previous write does not affect the next write)
}
void eeprom_write_buf(spi_inst_t *spi, uint cs_pin, uint16_t start_addr, const uint16_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Debug: Print the address and data being written
        // printf("Writing to Addr: 0x%03X, Data: 0x%04X\n", start_addr + i, buf[i]);

        // Write the data to the EEPROM
        eeprom_write(spi, cs_pin, start_addr + i, buf[i]);
        cs_select(cs_pin);
        // Wait for the write cycle to complete
        // sleep_ms(5); // Ensure write cycle time is met
    }
}

void eeprom_erase(spi_inst_t *spi, uint cs_pin, uint16_t addr) {
    cs_deselect(cs_pin);
    // eeprom_write_enable(spi, cs_pin);
    cs_select(cs_pin);
    uint16_t cmd = (EEPROM_CMD_ERASE << 13) | ((addr & 0x03FF) << 3); // 3-bit command + 10-bit address + 3 dummy bits
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};
    spi_write_blocking(spi, cmdbuf, 2);
    cs_deselect(cs_pin);
    // sleep_ms(7); // Wait for erase cycle to complete
    sleep_ms(4); // wait for typical write time for the erase cycle to complete
}

void eeprom_dump(spi_inst_t *spi, uint cs_pin) {
    uint16_t data;
    dump_flag = 1; // Set the dump flag to avoid redundant CS toggling in eeprom_read
    printf("\nEEPROM Memory Dump:\n");
    printf("Addr  | Data\n");
    printf("------+-------\n");

    for (uint16_t addr = 0; addr <= 0x03FF; addr++) {
        //! Handle chip select here since dump_flag is active
        cs_deselect(cs_pin);
        cs_select(cs_pin);
        // Use eeprom_read to read data from the EEPROM
        eeprom_read(spi, cs_pin, addr, &data);

        // Print the data in a formatted manner
        if (addr % 16 == 0) {
            printf("\n%04X  | ", addr);
        }
        printf("%04X ", data);
    }

    printf("\n");
    dump_flag = 0; // Reset the dump flag
}
void eeprom_copy(spi_inst_t *spi, uint cs_pin, uint16_t* eeprom_buffer) {
    uint16_t data;
    dump_flag = 1; // Set the dump flag to avoid redundant CS toggling in eeprom_read

    for (uint16_t addr = 0; addr <= 0x03FF; addr++) {
        //! Handle chip select here since dump_flag is active
        cs_deselect(cs_pin);
        cs_select(cs_pin);
        // Use eeprom_read to read data from the EEPROM
        eeprom_read(spi, cs_pin, addr, &data);
        eeprom_buffer[addr] = data;
        // Print the data in a formatted manner
        // if (addr % 16 == 0) {
        //     printf("\n%04X  | ", addr);
        // }
        // printf("%04X ", data);
    }

    // printf("\n");
    dump_flag = 0; // Reset the dump flag
    printf("EEPROM Memory Saved to buffer\r\n");
}

void eeprom_paste(spi_inst_t *spi, uint cs_pin, const uint16_t* eeprom_buffer) {
    for (uint16_t addr = 0; addr <= 0x03FF; addr++) {
        // Write data from buffer to EEPROM
        eeprom_write(spi, cs_pin, addr, eeprom_buffer[addr]);
    }
    
    printf("Buffer contents written to EEPROM\r\n");
}

/*
Usage: const char *message = "Hello, EEPROM!";
       eeprom_write_string(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x100, message);
*/
void eeprom_write_string(spi_inst_t *spi, uint cs_pin, uint16_t start_addr, const char *str) {
    uint16_t word;
    size_t i = 0;

    while (str[i] != '\0') {
        // Combine two characters into a 16-bit word (big-endian)
        word = (str[i] << 8) | (str[i + 1] != '\0' ? str[i + 1] : 0);
        eeprom_write(spi, cs_pin, start_addr++, word);

        // Move to the next pair of characters
        i += 2;
    }
}

void eeprom_read_string(spi_inst_t *spi, uint cs_pin, uint16_t start_addr, char *str, size_t max_len) {
    uint16_t word;
    size_t i = 0;

    while (i < max_len - 1) { // Reserve space for the null terminator
        // Read a 16-bit word from the EEPROM
        eeprom_read(spi, cs_pin, start_addr++, &word);

        // Extract the two characters from the 16-bit word (big-endian)
        str[i++] = (char)(word >> 8); // High byte
        if (i < max_len - 1) {
            str[i++] = (char)(word & 0xFF); // Low byte
        }

        // Stop if a null terminator is encountered
        if ((char)(word >> 8) == '\0' || (char)(word & 0xFF) == '\0') {
            break;
        }
    }

    // Ensure the string is null-terminated
    str[i] = '\0';
}

void print_buffer(uint16_t eeprom_buffer[0x400]) {
    for(int i = 0; i < 0x400; i++) {
        // Print the data in a formatted manner
        if (i % 16 == 0) {
            printf("\n%04X  | ", i);
        }
        printf("%04X ", eeprom_buffer[i]);
    }
}

/* NON-WORKING FUNCTIONS */
void eeprom_sequential_read_length(spi_inst_t *spi, uint cs_pin, uint16_t start_addr, uint16_t *buf, size_t length) {
    if (length == 0) return;

    cs_deselect(cs_pin);
    delay_250ns();
    // Ensure CS pin is high for the entire transaction
    cs_select(cs_pin);

    // Construct the read command for the first address
    uint16_t cmd = (EEPROM_CMD_READ << 10) | (start_addr & 0x03FF);
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};

    // Send the read command
    spi_write_blocking(spi, cmdbuf, 2);

    // Allocate a buffer for the entire read operation
    uint8_t databuf[3 + (length - 1) * 2]; // First read (3 bytes) + subsequent reads (2 bytes each)

    // Perform a single SPI read for all data
    spi_read_blocking(spi, 0, databuf, sizeof(databuf));

    // Handle the first read (with dummy bit)
    buf[0] = ((uint16_t)(databuf[0] << 9)) | ((uint16_t)(databuf[1] << 1)) | ((databuf[2] >> 7) & 0x01);

    // Handle subsequent reads (16 bits each)
    for (size_t i = 1; i < length; i++) {
        buf[i] = ((uint16_t)(databuf[3 + (i - 1) * 2] << 8)) | (databuf[3 + (i - 1) * 2 + 1]<<1)&0xFF | (databuf[3 + (i - 1) * 2 + 1]>>7)&0x01;
        // buf[i] <<= 1;
        // buf[i] >>=7;
    }

    // Ensure CS pin is low after the transaction
    cs_deselect(cs_pin);
}
void eeprom_sequential_read_range(spi_inst_t *spi, uint cs_pin, uint16_t start_addr, uint16_t end_addr, uint16_t *buf) {
    if (start_addr > end_addr) return;

    size_t length = end_addr - start_addr + 1;

    cs_deselect(cs_pin);
    delay_250ns();
    // Ensure CS pin is low for the entire transaction
    cs_select(cs_pin);

    // Construct the read command for the first address
    uint16_t cmd = (EEPROM_CMD_READ << 10) | (start_addr & 0x03FF);
    uint8_t cmdbuf[2] = {cmd >> 8, cmd & 0xFF};
    uint8_t databuf[3 + (length - 1) * 2]; // Buffer for the first read (3 bytes) + subsequent reads (2 bytes each)

    // Send the read command
    spi_write_blocking(spi, cmdbuf, 2);

    // Read all the data in one SPI transaction
    spi_read_blocking(spi, 0, databuf, sizeof(databuf));

    // Handle the first read (with dummy bit)
    buf[0] = ((uint16_t)(databuf[0] << 9)) | ((uint16_t)(databuf[1] << 1)) | ((databuf[2] >> 7) & 0x01);

    // Handle subsequent reads (16 bits each)
    for (size_t i = 1; i < length; i++) {
        buf[i] = ((uint16_t)(databuf[3 + (i - 1) * 2] << 8)) | databuf[3 + (i - 1) * 2 + 1];
        // buf[i] <<= 1;
    }

    // Ensure CS pin is high after the transaction
    cs_deselect(cs_pin);
}

int main() {
    stdio_init_all();
    sleep_ms(5000);

    printf("\nEEPROM example\n");

    //#define TP 14 // KB0
    #ifdef TP
    gpio_init(TP);
    gpio_set_dir(TP, GPIO_OUT);
    gpio_set_function(TP, GPIO_FUNC_SIO);
    gpio_put(TP, 1);
    gpio_put(TP,0);
    delay_250ns();
    gpio_put(TP, 1);
    delay_250ns();
    gpio_put(TP,0);
    #endif

    spi_init(spi_default, 1000 * 1000);
    // spi_init(spi_default, 500 * 1000);
    // spi_init(spi_default, 250 * 1000);
    // spi_set_format(spi_default, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); //< SPI Mode 0
    // spi_set_format(spi_default, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); //< SPI Mode 3
    spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); //< SPI Mode 0
    // spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); //< SPI Mode 3
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
    /* eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x00);
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

    #define DEBUG_COUNT 0
    for(int i=0; i<DEBUG_COUNT; i++) {
        eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11, 0xFEED);
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11+i, (0xF00D + i));
        eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x11+i, &data);
        printf("Read data at 0x%02x: 0x%04X\n", (0x11+i), data);
    }

    for(int i=0; i<=0xF; i++) {
        eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3F0+i);
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3F0+i, (0x7C00 + (i<<1))); // should increment by 2s
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x3E0+i, (0x8B00 + i)); // should increment by 1s
    } */
    #ifdef WRITE
    eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x220);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x220, 0xF1C2);
    
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x0FF, 0x1234);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x0FF, 1234); // Note: Sending decimal number
    #endif

    // eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);

    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x220, &data);
    printf("Read data at 0x220: 0x%04X\n", data);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x0FF, &data);
    printf("Read data at 0x0FF: %d\n", data);
    
    #define TEST_ALL
    #ifdef TEST_ALL
    for(int i=0; i<=0x3FF; i++) {
        //! Write the value of an address to the address to figure out what is being shifted where
        eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, i, i);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, i, 0x3FF-i);
    }
    // eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);
    #endif
    
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

    /* for(int i=0; i<=4; i++) {
        eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x100+i);
    } */

    // uint16_t data_buf[] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
    uint16_t data_buf[] = {0xFEED, 0x5731, 0xDEAD, 0xBEEF, 0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
    #ifdef WRITE
    eeprom_write_buf(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x100, data_buf, sizeof(data_buf) / sizeof(data_buf[0]));
    #endif

    uint16_t read_data;
    for (size_t i = 0; i < sizeof(data_buf) / sizeof(data_buf[0]); i++) {
        eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x100 + i, &read_data);
        printf("Read data at 0x%03X: 0x%04X\n", 0x100 + i, read_data);
    }
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x001, 0xBEEF);
    // eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);

    /* eeprom_erase(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000);
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0xABCD); //< returns ABCC
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0xDCBA);     //< returns DCBA (correct)
    // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0xFFFF);
    eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0x1111);
    eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, &read_data);
    printf("Read data at 0x%03X: 0x%04X\n", 0x000, read_data); */
    // dump_flag=1; //< so the debug prints dont get looped

    // char* str = "HELLO";
    // char* read_str;
    char read_str[5];
    char str[5] = {'H', 'i', ' ', 'N', 'C'};
    printf("Sending %s\t", str);
    #ifdef WRITE
    eeprom_write_string(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x300, str);
    #endif
    eeprom_read_string(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x300, read_str, 8); //max_len=8
    printf("Read string @ 0x300+: %s\n", read_str);

    char* str_lit = "Hello World";
    // char* read_str2 = 0;
    char read_str2[20];
    printf("Sending %s\t", str_lit);
    #ifdef WRITE
    eeprom_write_string(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x400, str_lit);
    #endif
    eeprom_read_string(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x400, read_str2, 15); //max_len=15
    printf("Read string @ 0x400+: %s\n", read_str2);

    uint16_t buffer[10];
    uint16_t start_addr = 0x101;

    puts("\nValues to check: \n");
    for(int i=0; i<0xF; i++) {
        eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, start_addr-1+i, &read_data);
        printf("At 0x%03X | 0x%04X\n", start_addr-1+i, read_data);
    }

    #ifdef SEQ_READ
    /**
     * In short, the problem with using the sequential read operation here is due to a mismatch 
     * of how the RP2040 SPI engine operates and how the AT93C86A EEPROM expects the SPI engine to operate. 
     * AT93C86A expects a continuous clock whereas RP2040 applies SCK in bursts of 8 or 16 (depending on format selected)
     */
    puts("\n");
    uint16_t buffer2[16];
    eeprom_sequential_read_range(spi_default, PICO_DEFAULT_SPI_CSN_PIN, start_addr-1, 0x10F, buffer2);
    for (size_t i = 0; i <= 0x10F - 0x100; i++) {
        printf("Data at 0x%03X: 0x%04X\n", 0x100 + i, buffer2[i]);
    }
    puts("\n");
    eeprom_sequential_read_length(spi_default, PICO_DEFAULT_SPI_CSN_PIN, start_addr, buffer, 8);
    for (size_t i = 0; i < 8; i++) {
        printf("Data at 0x%03X: 0x%04X\n", start_addr + i, buffer[i]);
    }
    puts("\n");
    sleep_us(5);
    uint8_t num2read = 4;
    #define WRITE
    #ifdef WRITE
    uint16_t write_buf[num2read];
    for(int i=0; i<num2read; i++) {
        write_buf[i] = 0xAAAA;
    }
    eeprom_write_buf(spi_default, PICO_DEFAULT_SPI_CSN_PIN, start_addr, write_buf, num2read);
    #endif
    eeprom_sequential_read_length(spi_default, PICO_DEFAULT_SPI_CSN_PIN, start_addr, buffer, num2read);
    for (size_t i = 0; i < num2read; i++) {
        printf("Data at 0x%03X: 0x%04X\n", start_addr + i, buffer[i]);
    }
    #endif

    // eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);
    // uint16_t save_buffer[0x3FF];
    uint16_t save_buffer[0x400];
    eeprom_copy(spi_default, PICO_DEFAULT_SPI_CSN_PIN, save_buffer);
    print_buffer(save_buffer);
    /* printf("\nBuffer Contents:\n");
    printf("Addr  | Data\n");
    printf("------+-------\n");
    for(int i = 0; i < 0x400; i++) {
        if (i % 16 == 0) {
            printf("\n%04X  | ", i);
        }
        printf("%04X ", save_buffer[i]);
    }
    printf("\n"); */

    for(int i=0; i<1024; i++) {
        // save_buffer[i] *= -1; // negate all values
        save_buffer[i] *= 2; // x2 all values
    }
    print_buffer(save_buffer);
    // eeprom_paste(spi_default, PICO_DEFAULT_SPI_CSN_PIN, save_buffer);
    eeprom_write_buf(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0, save_buffer, 1024);
    eeprom_dump(spi_default, PICO_DEFAULT_SPI_CSN_PIN);

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

        #ifdef TP
        gpio_put(TP, 1);
        sleep_us(1);
        gpio_put(TP,0);
        delay_250ns();
        gpio_put(TP, 1);
        delay_250ns();
        gpio_put(TP,0);
        #endif
        
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x001, 0xBEEF);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0xDEAD);
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0x7777);
        
        
        // eeprom_write(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, 0xABCD);
        // eeprom_read(spi_default, PICO_DEFAULT_SPI_CSN_PIN, 0x000, &read_data);
        // printf("Read data at 0x%03X: 0x%04X\n", 0x000, read_data);
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