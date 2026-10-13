#pragma once
/* Generic TX transaction description. Bit values are MSB-first, right-aligned.
 * Header phases always use one data line. Data uses one or four lines.
 * The driver owns any protocol opcodes/addresses; the CPU transport does not.
 * Header + payload execute as one hardware transaction; zero header lengths
 * mean continuation data under the caller's still-asserted CS. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t struct_size;
    uint16_t command;
    uint8_t command_bits, address_bits;
    uint64_t address;
    uint8_t dummy_bits, data_lines;
    uint16_t reserved;
} risc_spi_frame_v1;
static inline bool risc_spi_frame_valid(const risc_spi_frame_v1 *f,size_t count) {
    return f && f->struct_size>=sizeof(*f) && !f->reserved &&
        f->command_bits<=16 && f->address_bits<=64 && f->dummy_bits<=32 &&
        (f->data_lines==1 || f->data_lines==4) && count<=512 &&
        (count || f->command_bits || f->address_bits) &&
        (f->command_bits==16 || (f->command>>f->command_bits)==0) &&
        (f->address_bits==64 || (f->address>>f->address_bits)==0);
}
