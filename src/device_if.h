/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EDTT_DEVICE_IF_H
#define EDTT_DEVICE_IF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void deviceif_connection_clean_up(void);
void deviceif_connect(uint16_t n_devs, unsigned int dev_nbrs[]);
void deviceif_write(uint8_t dev_nbr, uint8_t* bufptr, size_t size);
int deviceif_read(uint8_t dev_nbr, uint8_t* bufptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif
