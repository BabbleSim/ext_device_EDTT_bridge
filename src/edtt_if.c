/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "bs_tracing.h"
#include "bs_utils.h"
#include "bs_oswrap.h"
#include "bs_pc_base.h"
#include "bs_pc_base_fifo_user.h"
#include "edtt_if.h"

/**
 * Interface towards the EDTT tool
 */

static bool terminate_on_edtt_close;

#define TO_EDTT  0
#define TO_BRIDGE 1
static int fifo[2] = { -1, -1};
static char *fifo_paths[2] = { NULL ,NULL };

extern pb_dev_state_t state;

void edtt_if_clean_up(void)
{
  for (int dir = TO_EDTT ; dir <= TO_BRIDGE ; dir ++){
    if ( fifo_paths[dir] ){
      if ( fifo[dir] != -1 ){
        close(fifo[dir]);
        remove(fifo_paths[dir]);
      }
      free(fifo_paths[dir]);
    }
  }
  if ( pb_com_path != NULL ) {
    rmdir(pb_com_path);
  }
}

void edtt_if_connect_over_FIFO(unsigned int dev_nbr){

  signal(SIGPIPE, SIG_IGN);

  for (int dir = TO_EDTT ; dir <= TO_BRIDGE ; dir ++){
    fifo_paths[dir] = (char*) bs_calloc(pb_com_path_length + 30, sizeof(char));
  }
  sprintf(fifo_paths[TO_EDTT], "%s/Device%i.ToPTT",  pb_com_path, dev_nbr);
  sprintf(fifo_paths[TO_BRIDGE], "%s/Device%i.ToBridge", pb_com_path, dev_nbr);

  if ((pb_create_fifo_if_not_there(fifo_paths[TO_EDTT]) != 0)
      || (pb_create_fifo_if_not_there(fifo_paths[TO_BRIDGE]) != 0)) {
    bs_trace_error_line("Couldnt create FIFOs for EDTT IF\n");
  }

  if ((fifo[TO_BRIDGE] = open(fifo_paths[TO_BRIDGE], O_RDONLY )) == -1) {
     bs_trace_error_line("Couldn't create FIFOs for EDTT IF\n");
  }

  if ((fifo[TO_EDTT] = open(fifo_paths[TO_EDTT], O_WRONLY )) == -1) {
     bs_trace_error_line("Couldn't create FIFOs for EDTT IF\n");
  }
}


void edtt_if_connect(unsigned int d_nbr, bool term_on_edtt_close, uint16_t n_devs){
  terminate_on_edtt_close = term_on_edtt_close;
  edtt_if_connect_over_FIFO(d_nbr);

  /* Start by telling the EDTTool how many devices we are connected to */
  edtt_write((uint8_t*)&n_devs, sizeof(n_devs));
}

static void edtt_if_abrupt_exit(){
  if ( terminate_on_edtt_close ){
    pb_dev_terminate(&state);
  } else {
    pb_dev_disconnect(&state);
  }
  bs_trace_exit_line("Abruptly disconnected from EDTT\n");
}

/**
 * Block until we receive size bytes into buf from the EDTTool
 */
void edtt_read(uint8_t *buf, size_t size){
  int pending_to_read = size;
  uint8_t *read_bufptr = buf;

  while ( pending_to_read > 0 ) {//the writes will most likely be atomic (unless they are more than PIPE_BUF), but just to be sure, lets loop until we get them all
    //blocking read
    int received_bytes = read(fifo[TO_BRIDGE], read_bufptr, pending_to_read);
    if ( received_bytes == EOF || received_bytes == 0 ) { //The FIFO was closed by the EDTTool
      bs_trace_warning_time_line("EDTT_IF: FIFO suddenly closed\n");
      edtt_if_abrupt_exit();
    } else if ( received_bytes == -1 ) {
      bs_trace_error_time_line("Unexpected error\n");
    }

    if ( received_bytes > 0 ) {
      pending_to_read -= received_bytes;
      read_bufptr +=received_bytes;
    }
  }
  return;
}

void edtt_write(uint8_t *bufptr, size_t size){
  if ( write(fifo[TO_EDTT], bufptr, size) != size ){
    //the other end of the pipe was closed
    edtt_if_abrupt_exit();
  }
}
