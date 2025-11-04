.. _bluetooth_shell:

Bluetooth Shell
###############

.. contents::
   :local:
   :depth: 2

The Bluetooth Shell sample provides an interactive shell interface for testing and debugging Bluetooth® LE functionality.
It enables access to Bluetooth shell commands for advertising, scanning, connecting, GATT operations, and more.

Overview
********

This sample demonstrates how to use the Bluetooth shell module to interactively control Bluetooth LE operations.
You can use it to test Bluetooth functionality, debug connections, discover services, and read/write characteristics without writing application code.

The sample enables the following Bluetooth shell commands:

* Bluetooth initialization and management
* Advertising (legacy and extended)
* Scanning with filters (name, address, RSSI)
* Connection management
* GATT service and characteristic discovery
* Reading and writing GATT characteristics
* Subscribing to notifications and indications
* Dynamic GATT database management

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

.. include:: /includes/tfm.txt

For testing a complete client-server workflow, you need two development kits with this sample installed, one acting as a server (peripheral) and one as a client (central).

Building and running
********************

.. |sample path| replace:: :file:`samples/bluetooth/shell`

.. include:: /includes/build_and_run_ns.txt

Testing
*******

This section describes how to test the Bluetooth Shell sample with two boards in a client-server setup.

Example complete workflow
==========================

The following procedure demonstrates a complete workflow using one board as a server (peripheral) and another as a client (central).

**Board 1 - Server (Peripheral):**

1. Initialize Bluetooth::

      uart:~$ bt init
      Bluetooth initialized

2. Start advertising::

      uart:~$ bt advertise on discov

   Wait for the "Advertising started" message.

**Board 2 - Client (Central):**

1. Initialize Bluetooth::

      uart:~$ bt init
      Bluetooth initialized

2. Start scanning for devices::

      uart:~$ bt scan on

   Look for advertisements showing the server's address and name.
   You should see output like::

      [DEVICE]: XX:XX:XX:XX:XX:XX (random) -47 dBm
      Bluetooth Shell

3. Note the server's address (for example, ``DE:AD:ED:66:71:59 random``).

4. Stop scanning::

      uart:~$ bt scan off

5. Connect to the server::

      uart:~$ bt connect DE:AD:ED:66:71:59 random

   Wait for the "Connection successful" message.

6. Verify the connection::

      uart:~$ bt connections

7. Discover primary services::

      uart:~$ gatt discover-primary

   This shows all available services.
   Example output::

      Service found: UUID: 0x180f, handle: 0x0001, end_handle: 0x0005

8. Discover characteristics::

      uart:~$ gatt discover-characteristic

   Or discover within a specific handle range::

      uart:~$ gatt discover-characteristic 0x0001 0x0010

9. View the discovered GATT database::

      uart:~$ gatt show-db

10. Read a characteristic (replace handle with actual value from discovery)::

       uart:~$ gatt read 0x0003

11. Write to a characteristic::

       uart:~$ gatt write <handle> <offset> <hex_data>

   Example::

       uart:~$ gatt write 0x0003 0 0102

   Or write without response::

       uart:~$ gatt write-without-response 0x0003 0102

12. Subscribe to notifications (if supported)::

       uart:~$ gatt subscribe <ccc_handle> <value_handle>

   Example::

       uart:~$ gatt subscribe 0x0004 0x0003

13. Disconnect when done::

       uart:~$ bt disconnect

**Board 1 - Server:**

14. Stop advertising::

      uart:~$ bt advertise off

Scanning with name filter and timeout
======================================

You can scan for a specific device name with a timeout:

1. Set a name filter::

      uart:~$ bt scan-filter-set name "Bluetooth Shell"

2. Scan with timeout (timeout value is in hexadecimal)::

      uart:~$ bt scan on timeout 0xa

   Timeout examples:
   * ``0x5`` = 5 seconds
   * ``0xa`` = 10 seconds
   * ``0x1e`` = 30 seconds
   * ``0x3c`` = 60 seconds

3. Clear the filter when done::

      uart:~$ bt scan-filter-clear name

Auto-connect by name
====================

You can automatically scan and connect to a device by name:

::

   uart:~$ bt connect-name "Bluetooth Shell"

This command:
* Sets a name filter
* Starts scanning
* Automatically connects when the device is found
* Uses a default timeout of 10 seconds

Useful commands
===============

**Server (Peripheral) commands:**
* ``bt init`` - Initialize Bluetooth
* ``bt advertise on discov`` - Start advertising (discoverable)
* ``bt advertise off`` - Stop advertising
* ``gatt register`` - Register test service (if CONFIG_BT_GATT_DYNAMIC_DB is enabled)
* ``gatt show-db`` - Show local GATT database

**Client (Central) commands:**
* ``bt init`` - Initialize Bluetooth
* ``bt scan on`` - Start scanning
* ``bt scan off`` - Stop scanning
* ``bt scan-filter-set name <name>`` - Filter scan by device name
* ``bt connect <addr> <type>`` - Connect (type: public/random)
* ``bt connect-name <name>`` - Scan and auto-connect by name
* ``bt connections`` - List connections
* ``bt disconnect`` - Disconnect
* ``gatt discover-primary`` - Discover primary services
* ``gatt discover-characteristic`` - Discover characteristics
* ``gatt read <handle>`` - Read characteristic
* ``gatt write <handle> <offset> <hex_data>`` - Write characteristic
* ``gatt subscribe <ccc_handle> <value_handle>`` - Subscribe to notifications

**Helper commands:**
* ``gatt show-db`` - View discovered GATT database
* ``gatt att_mtu`` - Check ATT MTU size
* ``bt name`` - Check/set device name
* ``bt help`` - Show available commands

Dependencies
************

This sample uses the following Zephyr components:

* :ref:`zephyr:bluetooth_api`:
  * Bluetooth Host stack
  * Bluetooth Shell module
  * GATT Client support
  * GATT Dynamic Database support

