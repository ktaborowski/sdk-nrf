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
* GATT Discovery Manager for simplified service discovery
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

GATT Discovery Manager
======================

The GATT Discovery Manager provides simplified service discovery with automatic parsing of services, characteristics, and descriptors.

**Discover all services:**

After connecting to a device, discover all services::

   uart:~$ gatt_dm discover-all

Or specify a connection index (from ``bt connections`` output)::

   uart:~$ gatt_dm discover-all 0

The discovery results show:
* Service UUID
* Total attribute count
* Detailed attribute list (services, characteristics, descriptors)

**Continue discovery:**

After discovering a service, continue to find the next service::

   uart:~$ gatt_dm continue

Repeat this command to discover all services sequentially.

**Discover specific service by UUID:**

Discover a specific service by its UUID::

   uart:~$ gatt_dm discover-uuid 0x180F

This is useful when you know which service you're looking for (e.g., Battery Service: ``0x180F``, Heart Rate Service: ``0x180D``).

**Release discovery data:**

Release discovery data when finished::

   uart:~$ gatt_dm release

This must be called before starting a new discovery if the previous one completed successfully.

**Example workflow:**

::

   uart:~$ bt connect DE:AD:ED:66:71:59 random
   uart:~$ gatt_dm discover-all
   Discovery completed:
     Service UUID: 0000180f-0000-1000-8000-00805f9b34fb
     Attribute count: 5
   [... detailed attribute list ...]
   Use 'gatt_dm continue' to find next service or 'gatt_dm release' to finish
   uart:~$ gatt_dm continue
   [... next service discovered ...]
   uart:~$ gatt_dm release

Multiple Advertising Sets
==========================

The Multiple Advertising Sets feature allows you to create and manage multiple simultaneous Bluetooth advertising sets. This is useful for scenarios where you want to advertise different services or data on different advertising sets.

**Create an advertising set:**

Create a new advertising set at a specific index::

   uart:~$ multi_adv create 0 connectable interval-min 100 interval-max 150

Options:
* ``connectable`` - Make the advertising set connectable
* ``non-connectable`` - Make the advertising set non-connectable (default)
* ``scannable`` - Make the advertising set scannable
* ``interval-min <ms>`` - Minimum advertising interval in milliseconds
* ``interval-max <ms>`` - Maximum advertising interval in milliseconds
* ``sid <id>`` - Advertising set ID (0-15, defaults to index if not specified)

**Set advertising data:**

Set advertising data for a specific advertising set::

   uart:~$ multi_adv data 0 name "Nordic Beacon" flags

   uart:~$ multi_adv data 1 name "Nordic Shell" scan-response name "Extended Info"

Options:
* ``name <name>`` - Set device name in advertising data
* ``flags`` - Add flags (LE General Discoverable, BR/EDR not supported)
* ``scan-response`` - Switch to scan response data (use before subsequent options)

**Start advertising:**

Start advertising on a specific advertising set::

   uart:~$ multi_adv start 0

   uart:~$ multi_adv start 1 timeout 5000 num-events 10

Options:
* ``timeout <ms>`` - Maximum advertising duration in milliseconds
* ``num-events <count>`` - Maximum number of advertising events

**Stop advertising:**

Stop advertising on a specific advertising set::

   uart:~$ multi_adv stop 0

**Delete advertising set:**

Delete an advertising set to free up resources::

   uart:~$ multi_adv delete 0

**List all advertising sets:**

View the status of all advertising sets::

   uart:~$ multi_adv list

**Bulk operations:**

Start all created advertising sets::

   uart:~$ multi_adv start-all

Stop all started advertising sets::

   uart:~$ multi_adv stop-all

**Example workflow:**

Create and start two advertising sets::

   uart:~$ bt init
   uart:~$ multi_adv create 0 non-connectable scannable
   uart:~$ multi_adv data 0 name "Nordic Beacon" flags
   uart:~$ multi_adv start 0
   uart:~$ multi_adv create 1 connectable interval-min 100 interval-max 150
   uart:~$ multi_adv data 1 name "Nordic Shell" flags
   uart:~$ multi_adv start 1
   uart:~$ multi_adv list

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

**GATT Discovery Manager commands:**
* ``gatt_dm discover-all [conn_idx]`` - Discover all services on a connection
* ``gatt_dm discover-uuid <uuid> [conn_idx]`` - Discover specific service by UUID (e.g., ``0x180F``)
* ``gatt_dm continue`` - Continue discovery to find next service (after ``discover-all``)
* ``gatt_dm release`` - Release discovery data to allow new discovery

**Multiple Advertising Sets commands:**
* ``multi_adv create <index> [options]`` - Create an advertising set at specified index
* ``multi_adv data <index> [data options]`` - Set advertising data for an advertising set
* ``multi_adv start <index> [timeout <ms>] [num-events <count>]`` - Start advertising on a set
* ``multi_adv stop <index>`` - Stop advertising on a set
* ``multi_adv delete <index>`` - Delete an advertising set
* ``multi_adv list`` - List all advertising sets and their status
* ``multi_adv start-all`` - Start all created advertising sets
* ``multi_adv stop-all`` - Stop all started advertising sets

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

This sample also uses the following NCS libraries:

* :ref:`gatt_dm_readme`: GATT Discovery Manager for simplified service discovery

Extended Advertising support:
* :kconfig:option:`CONFIG_BT_EXT_ADV`: Extended Advertising API support
* :kconfig:option:`CONFIG_BT_EXT_ADV_MAX_ADV_SET`: Maximum number of simultaneous advertising sets

