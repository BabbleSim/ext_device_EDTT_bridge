/*
 * Copyright 2019 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include "edtt_args.h"
#include "bs_tracing.h"
#include "bs_oswrap.h"

char executable_name[] = "bs_device_EDTT_bridge";

void component_print_post_help(){
  fprintf(stdout,
"\n"
"Bridge device for the EDTT, it connects on a FIFO to the EDTT transport driver,\n"
"and thru 2 sets of FIFOs to 2 EDTT devices\n"
"\n"
"It does the following:\n"
" * Ensures the simulated devices are stalled while the EDTTool decides what\n"
"   it wants next\n"
" * It pipes the send and recv requests from the EDTTool to the devices\n"
"  * Send requests are sent in no time to the devices\n"
"  * Receive requests:\n"
"    * Are done in no time if the data is already available. If it is not, the\n"
"      simulation will be advanced <recv_wait_us> ms at a time until the device\n"
"      has produced the requested data in its EDTT IF\n"
"    * The receive timeout is handled by this bridge\n"
"    * The time in which the read has been actually finalized (or timeout\n"
"      occurred) is sent back to the EDTT (the EDTT driver knows the\n"
"      simulation time too)\n"
" * It handles the wait requests from the EDTT driver by letting the simulation\n"
"   advance by that amount of time\n"
"\n"
"Effectively it either blocks the simulator or the EDTTool so that only one\n"
"executes at a time, locksteping them to ensure that simulations are fully\n"
"reproducible and that the simulator or the scripts can be paused for debugging"
"\n");
}

edtt_bridge_args_t *args_g;

static void cmd_recv_wait_found(char *argv, int offset){
  args_g->recv_wait_us = args_g->recv_wait_us_f;
}

static void cmd_D_found(char *argv, int offset){
  if (args_g->EDTT_device_numbers != NULL ) {
    bs_trace_error_line("The number of devices (-D) can only be specified once: %s\n", argv);
  }
  args_g->EDTT_device_numbers = (uint*)bs_calloc( args_g->nbr_devices, sizeof(uint) );
  for (int i = 0; i < args_g->nbr_devices ; i++){
    args_g->EDTT_device_numbers[i] = UINT_MAX;
  }
}

static void cmd_trace_lvl_found(char * argv, int offset){
  bs_trace_set_level(args_g->verb);
}

static void cmd_gdev_nbr_found(char * argv, int offset){
  bs_trace_set_prefix_dev(args_g->global_device_nbr);
}

/**
 * Check the arguments provided in the command line: set args based on it or defaults,
 * and check they are correct
 */
void edttbridge_argparse(int argc, char *argv[], edtt_bridge_args_t *args)
{
  int i;

  args_g = args;

  bs_args_struct_t args_struct[] = {
      ARG_TABLE_S_ID,
      ARG_TABLE_P_ID_2G4,
      ARG_TABLE_DEV_NBR,
      ARG_TABLE_GDEV_NBR,
      /*manual,mandatory,switch,option,     name ,               type,       destination,         callback,             , description*/
      { false,  true  , false,  "D",        "number_devices",    'u', (void*)&args->nbr_devices,  cmd_D_found,         "Number of devices the bride will connect to"},
      ARG_TABLE_VERB,
      ARG_TABLE_COLOR,
      ARG_TABLE_NOCOLOR,
      ARG_TABLE_FORCECOLOR,
      /*manual,mandatory,switch,option,     name ,               type,       destination,         callback,             , description*/
      { false,  false , false, "RxWait",  "recv_wait_us",       'f', (void*)&args->recv_wait_us_f, cmd_recv_wait_found,"(10e3) while there is no enough data for a read, the simulation will be advanced in this steps"},
      { false,  false , true, "AutoTerminate","AutoTerminate",  'b', (void*)&args->terminate_on_edtt_close, NULL,      "Terminate the simulation when EDTT disconnects"},
      { true ,  true  , false,"dev<nbr>","dev_number",          'u',            NULL,               NULL,              "Simulation device number for the EDTT enable device number <nbr> to connect to"},
      ARG_TABLE_ENDMARKER
  };

  static char default_phy[] ="2G4";

  bs_args_set_defaults(args_struct);
  args->verb   = 2;
  bs_trace_set_level(args->verb);
  args->recv_wait_us = 10000; //(10ms) we will let the simulation advance by this amount of time each time the device does not have yet anything for us
  args->nbr_devices = 0;
  args->EDTT_device_numbers = NULL;

  for (i=1; i<argc; i++){
    if ( !bs_args_parse_one_arg(argv[i], args_struct) ){
      int offset;
      unsigned int index;
      if ( ( offset = bs_is_multi_opt(argv[i], "dev", &index, 1) ) ) {
        if ( args->EDTT_device_numbers == 0 ) {
          bs_trace_error_line("cmdarg: tried to set a device (%i) before setting the number of devices (-D=<nbr>) (%s)\n\n""\n",index, args->nbr_devices, argv[i]);
        }
        if ( index >= args->nbr_devices ) {
          bs_trace_error_line("cmdarg: tried to set a device %i >= %i number of avaliable devices (%s)\n\n""\n",index, args->nbr_devices, argv[i]);
        }
        bs_read_optionparam(&argv[i][offset], (void*)&(args->EDTT_device_numbers[index]), 'u', "dev_number");
      }
      else {
        bs_args_print_switches_help(args_struct);
        bs_trace_error_line("Incorrect command line option %s\n",argv[i]);
      }
    }
  } 

  if (args->device_nbr == UINT_MAX) {
    bs_args_print_switches_help(args_struct);
    bs_trace_error_line("The command line option <device number> needs to be set\n");
  }
  if (args->global_device_nbr == UINT_MAX) {
    args->global_device_nbr = args->device_nbr;
    bs_trace_set_prefix_dev(args->global_device_nbr);
  }
  if (!args->s_id) {
    bs_args_print_switches_help(args_struct);
    bs_trace_error_line("The command line option <simulation ID> needs to be set\n");
  }
  if (!args->p_id) {
    args->p_id = default_phy;
  }

  if (args->nbr_devices == 0){
    bs_trace_error_line("You must provide a number of devices to connect to\n");
  }
  for (int i = 0; i < args->nbr_devices ; i++){
    if (args->EDTT_device_numbers[i] == UINT_MAX){
      bs_trace_error_line("device number %i was not provided\n", i);
    }
  }

}
