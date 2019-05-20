/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EDTT_IF_H
#define EDTT_IF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void edtt_if_clean_up(void);
void edtt_if_connect(unsigned int dev_nbr, bool term_on_edtt_close, uint16_t n_devs);
void edtt_read(uint8_t *bufptr, size_t size);
void edtt_write(uint8_t *bufptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif
