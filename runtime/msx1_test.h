/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <stdint.h>
typedef struct { uint64_t cycles,beam,samples,psg_writes,joy_reads,key_reads; } msx1_statistics;
void msx1_stats(msx1_statistics *out);
#ifdef MSX1_TEST
uint8_t msx1_test_in(uint8_t port);
void msx1_test_out(uint8_t port,uint8_t value);
uint8_t msx1_test_read(uint16_t address);
void msx1_test_write(uint16_t address,uint8_t value);
void msx1_test_sprite(unsigned index,uint8_t out[4]);
#endif
