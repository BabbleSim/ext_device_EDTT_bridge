Bridge device for the [EDTT (Embedded device test tool)](https://github.com/EDTTool)

It connects to the EDTT transport driver,
and thru 2 sets of FIFOs to 2 EDTT enabled devices.

It does the following:

* Ensures the simulated devices are stalled while the EDTTool decides what
  it wants next
* It pipes the send and recv requests from the EDTTool to the devices

    * Send requests are sent in no time to the devices
    * Receive requests:

         * Are done in no time if the data is already available. If it is not, the
           simulation will be advanced `<recv_wait_us>` ms at a time until the device
           has produced the requested data in its EDTT IF
         * The receive timeout is handled by this bridge
         * The time in which the read has been actually finalized (or timeout
           occurred) is sent back to the EDTT (the EDTT driver knows the
           simulation time too)

* It handles the wait requests from the EDTT driver by letting the simulation
  advance by that amount of time

Effectively it either blocks the simulator or the EDTTool so that only one
executes at a time, locksteping them to ensure that simulations are fully
reproducible and that the simulator or the scripts can be paused for debugging

For more information please refer to the EDTT documentation, and specifically
to teh documentation abou the EDTT_transport_bsim.

Note: All this bridge functionality could actually be implemented directly in
the EDTTool driver
