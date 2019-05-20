/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Interface towards the simulated devices EDTT IF
 *
 * The transport uses 2 FIFOs.
 * There is only 1 FIFO in each direction.
 *
 * Note that when a process write() to its end of the FIFO, the data is
 * immediately available (as soon as the write() succeeds) in the other side.
 * So in this case, process A does a write(), we switch context to
 * process B which does immediately a O_NONBLOCK read(),
 * process B gets that just written data.
 * (This behavior is key)
 *
 * According to: http://pubs.opengroup.org/onlinepubs/009695399/functions/write.html :
 * "Writes can be serialized with respect to other reads and writes. If a read() of file data can be proven (by any means)
 *  to occur after a write() of the data, it must reflect that write(), even if the calls are made by different processes."
 *
 * More vagely, in TLPI chaper 44.2 :
 * "As with any file descriptor, we can use the read() and write() system calls to per-
 * form I/O on the pipe. Once written to the write end of a pipe, data is immediately
 * available to be read from the read end. "
 *
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
#include "bs_pc_base_fifo_user.h"

#define TO_DEVICE  0
#define TO_BRIDGE 1
static int *FIFOs;
static char **FIFOnames;
static int *simdevice_numbers;
static int n_devices;

static void alloc_bufs(uint16_t n_devs) {
  FIFOs = (int*) bs_malloc(sizeof(int)*2*n_devs);
  FIFOnames = (char **) bs_malloc(sizeof(char*)*2*n_devs);
  simdevice_numbers = (int*) bs_malloc(sizeof(int)*n_devs);
  for (int i = 0; i < 2*n_devs ; i++) {
    FIFOs[i] = -1;
  }
  for (int i = 0; i < n_devs ; i++) {
    simdevice_numbers[i] = -1;
  }
}

void deviceif_connection_clean_up(void) {
  for (int d = 0; d < n_devices ; d ++ ) {
    for (int dir = TO_DEVICE ; dir <= TO_BRIDGE ; dir ++){
      if ( FIFOnames[d*2 + dir] ){
        if ( FIFOs[d*2 + dir] != -1 ){
          close(FIFOs[d*2 + dir]);
          remove(FIFOnames[d*2 + dir]);
        }
        free(FIFOnames[d*2 + dir]);
      }
    }
  }
  if ( pb_com_path != NULL ) {
    rmdir(pb_com_path);
  }
}

static void connect_over_FIFOs(uint16_t n_devs, unsigned int dev_nbrs[]) {

  signal(SIGPIPE, SIG_IGN);

  n_devices = n_devs;

  for (int d = 0; d < n_devs ; d ++ ){
    simdevice_numbers[d] = dev_nbrs[d];
    for (int dir = TO_DEVICE ; dir <= TO_BRIDGE ; dir ++){
      FIFOnames[d*2 + dir] = (char*) bs_calloc(pb_com_path_length + 30, sizeof(char));
    }
    sprintf(FIFOnames[d*2 + TO_DEVICE], "%s/Device%i.PTTin",  pb_com_path, dev_nbrs[d]);
    sprintf(FIFOnames[d*2 + TO_BRIDGE], "%s/Device%i.PTTout", pb_com_path, dev_nbrs[d]);

    if ((pb_create_fifo_if_not_there(FIFOnames[d*2 + TO_DEVICE]) != 0)
        || (pb_create_fifo_if_not_there(FIFOnames[d*2 + TO_BRIDGE]) != 0)) {
      bs_trace_error_line("Could not create FIFOs for device EDTT IF\n");
    }

    if ((FIFOs[d*2 + TO_BRIDGE] = open(FIFOnames[d*2 + TO_BRIDGE], O_RDONLY )) == -1) {
       bs_trace_error_line("Could not create FIFOs for device EDTT IF\n");
    }

    //we want the read end to be non bloking (if the device didn't produce anything yet, we need to let it run a bit)
    int flags;
    flags = fcntl(FIFOs[d*2 + TO_BRIDGE], F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(FIFOs[d*2 + TO_BRIDGE], F_SETFL, flags);

    if ((FIFOs[d*2 + TO_DEVICE] = open(FIFOnames[d*2 + TO_DEVICE], O_WRONLY )) == -1) { //we will block here until the device opens its end
       bs_trace_error_line("Could not create FIFOs for device EDTT IF\n");
    }

    //we want the  write end to be non bloking (if for whatever reason we fill up the FIFO, we would deadlock as the device is stalled => better to catch it in the write function)
    flags = fcntl(FIFOs[d*2 + TO_DEVICE], F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(FIFOs[d*2 + TO_DEVICE], F_SETFL, flags);
  }
}

void deviceif_connect(uint16_t n_devs, unsigned int dev_nbs[]) {
  alloc_bufs(n_devs);
  connect_over_FIFOs(n_devs, dev_nbs);
}

void deviceif_write(uint8_t d, uint8_t* bufptr, size_t size) {
  if ( d >= n_devices ) {
    bs_trace_error_line("device_nbr >= n_devices (%i>= %i)\n", d, n_devices );
  }

  if ( write(FIFOs[d*2 + TO_DEVICE], bufptr, size) != size ){
    bs_trace_error_line("EDTT IF to device %i filled up (FIFO size needs to be increased)\n", simdevice_numbers[d]);
  }
}

/**
 * Attempt to read from a device (<d>) <size> bytes
 * return how many bytes could be read
 * If the read would block, it will return less than size
 * if the FIFO is disconnected the device will be ended
 */
int deviceif_read(uint8_t d, uint8_t* bufptr, size_t size) {
  if ( d >= n_devices ) {
    bs_trace_error_line("device_nbr >= n_devices (%i>= %i)\n", d, n_devices );
  }

  int total_read = 0;
  int pending_to_read = size;
  uint8_t *read_bufptr = bufptr;

  while ( pending_to_read > 0 ) {
    int received_bytes = read(FIFOs[d*2 + TO_BRIDGE], read_bufptr, pending_to_read);
    if ( ( received_bytes == -1 ) && (errno == EAGAIN) ) { //Nothing yet there
      return total_read; //whatever we read so far
    } else if ( received_bytes == EOF ) { //The FIFO was closed by the device
      bs_trace_error_line("DEVICE_IF: device (%i) FIFO closed\n",simdevice_numbers[d]);
    } else if ( received_bytes == -1 ) {
      bs_trace_error_line("Unexpected error\n");
    }

    if ( received_bytes > 0 ) {
      pending_to_read -= received_bytes;
      read_bufptr += received_bytes;
      total_read += received_bytes;
    }
  }
  return total_read;
}
