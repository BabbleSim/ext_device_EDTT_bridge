/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BS_EDTT_BRIDGE_ARGS_H
#define BS_EDTT_BRIDGE_ARGS_H

#include <stdint.h>
#include "bs_types.h"
#include "bs_cmd_line_typical.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  ARG_S_ID
  ARG_P_ID
  ARG_DEV_NBR
  ARG_GDEV_NBR
  unsigned int nbr_devices;
  ARG_VERB
  bs_time_t recv_wait_us;
  double recv_wait_us_f;
  int terminate_on_edtt_close;
  unsigned int *EDTT_device_numbers;
} edtt_bridge_args_t;

void edttbridge_argparse(int argc, char *argv[], edtt_bridge_args_t *args);

#ifdef __cplusplus
}
#endif

#endif
