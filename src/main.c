/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "bs_tracing.h"
#include "edtt_args.h"
#include "edtt_if.h"
#include "device_if.h"
#include "bs_pc_base.h"

/**
 * Bridge device for the EDTT,
 * it connects to the EDTT transport driver,
 * and thru 2 sets of FIFOs to 2 EDTT enabled devices.
 *
 * It does the following:
 * * Ensures the simulated devices are stalled while the EDTTool decides what
 *   it wants next
 * * It pipes the send and recv requests from the EDTTool to the devices
 *  * Send requests are sent in no time to the devices
 *  * Receive requests:
 *    * Are done in no time if the data is already available. If it is not, the
 *      simulation will be advanced <recv_wait_us> ms at a time until the device
 *      has produced the requested data in its EDTT IF
 *    * The receive timeout is handled by this bridge
 *    * The time in which the read has been actually finalized (or timeout
 *      occurred) is sent back to the EDTT (the EDTT driver knows the
 *      simulation time too)
 *  * Receive with wait notify requests:
 *    * Same as normal receive requests, except that whenever the bridge waits it
 *      will first notify the EDTT bridge via a wait notification message
 * * It handles the wait requests from the EDTT driver by letting the simulation
 *   advance by that amount of time
 *
 * Effectively it either blocks the simulator or the EDTTool so that only one
 * executes at a time, locksteping them to ensure that simulations are fully
 * reproducible and that the simulator or the scripts can be paused for debugging
 *
 * Note: All this bridge functionality could actually be implemented directly in the EDTTool driver
 */

bs_time_t Now = 0; //Simulation time
bs_time_t get_time(){
  return Now;
}

static edtt_bridge_args_t args;
static bool terminate_on_edtt_close;
static bs_time_t read_wait_time;
pb_dev_state_t state;

uint8_t main_clean_up() {
  edtt_if_clean_up();
  deviceif_connection_clean_up();
  pb_dev_terminate(&state);
  if (args.EDTT_device_numbers != NULL ) {
    free(args.EDTT_device_numbers);
    args.EDTT_device_numbers = NULL;
  }
  return 0;
}

int receive_and_process_command_from_edtt(){
  /*
   * The protocol with the EDTTool is as follows:
   *  1 byte commands are sent from the EDTTool
   *  The commands are: SEND, RCV, RCV_WAIT_NOTIFY, WAIT, DISCONNECT
   *  SEND is followed by:
   *    1 byte : device idx
   *    2 bytes: (uint16_t) number of bytes
   *    N bytes: payload to forward
   *  RCV is followed by:
   *    1 byte : device idx
   *    8 bytes: timeout time (simulated absolute time)
   *    2 bytes: (uint16_t) number of bytes
   *  RCV_WAIT_NOTIFY is followed by:
   *    1 byte : device idx
   *    8 bytes: timeout time (simulated absolute time)
   *    2 bytes: (uint16_t) number of bytes
   *  WAIT:
   *    8 bytes: (uint64_t) absolute time stamp until which to wait (not the wait duration, but the end of the wait)
   *  DISCONNECT: nothing
   *
   *  After receiving a command (and its payload) this bridge device will respond:
   *  to a SEND: nothing
   *  to a RCV:
   *    1 byte : reception done (0) or timeout (1)
   *    8 bytes: timestamp when the reception or timeout actually happened
   *    0/N bytes: (0 bytes if timeout, N bytes as requested otherwise)
   *  to a RCV_WAIT_NOTIFY:
   *    0 or more WAIT_NOTIFICATION followed by:
   *      8 bytes: (uint64_t) absolute time stamp until which the wait will run (not the wait duration, but the end of the wait)
   *    After the receive is complete (or timed out):
   *      1 byte : reception done (0) or timeout (1)
   *      8 bytes: timestamp when the reception or timeout actually happened
   *      0/N bytes: (0 bytes if timeout, N bytes as requested otherwise)
   *  to a WAIT: 1 byte (0) when wait is done
   *  to a DISCONNECT: nothing
   *
   */
#define DISCONNECT 0
#define WAIT 1
#define SEND 2
#define RCV  3
#define RCV_WAIT_NOTIFY 4

#define WAIT_NOTIFICATION 0xF0
#define UNKNOWN_COMMAND 0xFF

  uint8_t command = DISCONNECT;

  bs_trace_raw_time(9, "main: Awaiting EDTTool command\n");
  edtt_read(&command, 1);
  switch (command) {
    case DISCONNECT:
    { //End the simulation
      bs_trace_raw_time(8, "main: EDTT asked us to disconnect\n");
      if (terminate_on_edtt_close) {
        pb_dev_terminate(&state);
      }
      return 1;
      break;
    }
    case WAIT:
    { //Let the simulator run until this time is reached
      pb_wait_t wait_s; //64bits = 8bytes
      edtt_read((uint8_t*)&wait_s.end, sizeof(wait_s.end));
      bs_trace_raw_time(8, "main: EDTT asked to wait for  %"PRItime"us\n", wait_s.end);
      if (wait_s.end > Now) {
        if (pb_dev_request_wait_block(&state, &wait_s) != 0) {
          bs_trace_exit_line("Scheduler killed us while running a Wait\n");
        }
        Now = wait_s.end;
      } else {
        bs_trace_warning_manual_time_line(Now,"Wait into the past (%"PRItime") ignored\n", wait_s.end);
      }
      uint8_t reply = 0;
      edtt_write(&reply, 1);
      break;
    }
    case SEND:
    { //Forward the message without delay to the device
      uint8_t device_idx;
      uint16_t number_of_bytes = 0;
      edtt_read(&device_idx, sizeof(device_idx));
      edtt_read((uint8_t*)&number_of_bytes, sizeof(number_of_bytes));
      if (number_of_bytes > 0) {
        uint8_t buffer[number_of_bytes];
        edtt_read(buffer, number_of_bytes);
        bs_trace_raw_time(8, "main: (%i) EDTT asked to send %i bytes\n",device_idx, number_of_bytes);
        deviceif_write(device_idx, buffer, number_of_bytes);
      }
      break;
    }
    case RCV:
    case RCV_WAIT_NOTIFY:
    {
      uint8_t device_idx;
      uint16_t number_of_bytes = 0;
      bs_time_t timeout;
      edtt_read(&device_idx, sizeof(device_idx));
      edtt_read((uint8_t*)&timeout, sizeof(bs_time_t));
      edtt_read((uint8_t*)&number_of_bytes, sizeof(number_of_bytes));
      bs_trace_raw_time(8, "main: (%i) EDTT asked to rcv %i bytes with timeout @%"PRItime"\n",device_idx, number_of_bytes, timeout);
      uint8_t buffer_m[number_of_bytes + 9];
      uint8_t *buffer = buffer_m + 9;
      int pending_to_read = number_of_bytes;
      uint16_t readsofar = 0;
      while (Now < timeout) {
        int read = deviceif_read(device_idx, &buffer[readsofar], pending_to_read);
        pending_to_read -= read;
        readsofar += read;
        if (pending_to_read > 0) {
          //we wait for a small amount of time to let the device produce more
          pb_wait_t Wait_struct;
          Wait_struct.end = Now + read_wait_time;
          if (command == RCV_WAIT_NOTIFY) {
            uint8_t notify_buffer[sizeof(bs_time_t) + 1];
            notify_buffer[0] = WAIT_NOTIFICATION;
            memcpy(&notify_buffer[1], &Wait_struct.end, sizeof(Wait_struct.end));
            edtt_write(notify_buffer, sizeof(notify_buffer));
          }
          if ( pb_dev_request_wait_block(&state, &Wait_struct) != 0 ) {
            bs_trace_exit_line("Disconnected by Phy during wait\n");
          }
          //bs_trace_raw_time(9, "main: Not enough data, waiting\t");
          Now += read_wait_time;
        } else { //if pending_to_read > 0
          break;
        }
      }
      uint8_t *message = buffer_m;
      memcpy(&message[1], &Now, sizeof(bs_time_t));

      if (pending_to_read == 0) { //succeeded
        bs_trace_raw_time(9, "main: (%i) All %i bytes received forwarding\n",device_idx, number_of_bytes);
        message[0] = 0;
        edtt_write(buffer_m, 9 + number_of_bytes);
      } else { //timed out
        bs_trace_raw_time(9, "main: (%i) receive timedout\n",device_idx);
        message[0] = 1;
        edtt_write(buffer_m, 9);
      }
      break;
    }
    default:
    {
      uint8_t reply = UNKNOWN_COMMAND;
      edtt_write(&reply, sizeof(reply)); //Before dying, let's tell the EDTT of the incompatibility
      bs_trace_error_line("Can't understand command %u;"
                          "Most likely the EDTT version you are using requires a newer bridge\n",
                          command);
      break;
    }
  }

  return 0;
}


int main(int argc, char *argv[]) {

  /*
   * Let's ensure that even if we are redirecting to a file, we get stdout
   * and stderr line buffered (default for console)
   * Note that glibc ignores size. But just in case we set a reasonable
   * number in case somebody tries to compile against a different library
   */
  setvbuf(stdout, NULL, _IOLBF, 512);
  setvbuf(stderr, NULL, _IOLBF, 512);
  bs_trace_register_cleanup_function( main_clean_up );
  bs_trace_register_time_function( get_time );

  edttbridge_argparse(argc, argv, &args);
  terminate_on_edtt_close = args.terminate_on_edtt_close;
  read_wait_time = args.recv_wait_us;

  bs_trace_raw(9,"main: Connecting to scheduler...\n");
  pb_dev_init_com(&state, args.device_nbr, args.s_id, args.p_id);

  bs_trace_raw(9,"main: Connecting to devices...\n");
  deviceif_connect(args.nbr_devices, args.EDTT_device_numbers);

  bs_trace_raw(9,"main: Connecting to EDTT (Embedded Device Test Tool)...\n");
  edtt_if_connect(args.global_device_nbr, args.terminate_on_edtt_close, args.nbr_devices);
  bs_trace_raw(9,"main: Connected\n");

  while ( receive_and_process_command_from_edtt() == 0 ) { }

  pb_dev_disconnect(&state);

  return main_clean_up();
}
