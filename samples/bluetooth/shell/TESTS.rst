.. _bluetooth_shell_tests:

Bluetooth Shell Sample - Test Specifications
#############################################

.. contents::
   :local:
   :depth: 2

Introduction
============

This document provides test specifications for the Bluetooth Shell sample. The tests are designed to serve both as human-readable documentation and automated test specifications that can be executed using Twister's shell harness.

The test framework supports two types of testing:

* **Single-board tests**: Validate command syntax, error handling, and state management on a single device
* **Multi-board tests**: Test actual Bluetooth interactions between two devices (peripheral and central roles)

Prerequisites
=============

Hardware Requirements
---------------------

* Nordic Semiconductor development kit (nRF52840 DK, nRF5340 DK, nRF54L15 DK, etc.)
* For multi-board tests: Two compatible development kits
* USB cable for programming and serial communication

Software Requirements
---------------------

* Nordic Connect SDK (NCS) installed and configured
* Twister test runner (part of Zephyr)
* Serial terminal access (115200 baud, 8N1)

Running Tests
=============

Manual Testing
--------------

Use this document as a guide to manually execute test scenarios:

1. Build and flash the sample to your device
2. Connect to the device via serial terminal
3. Execute the commands listed in each test scenario
4. Verify the expected results match the actual output

Automated Testing
-----------------

Run tests automatically using Twister:

.. code-block:: bash

   # Run all tests
   twister -p nrf54l15dk/nrf54l15/cpuapp -T nrf/samples/bluetooth/shell

   # Run specific test
   twister -p nrf54l15dk/nrf54l15/cpuapp -T nrf/samples/bluetooth/shell -s sample.bluetooth.shell.basic

   # Run with device testing (requires connected hardware)
   twister -p nrf54l15dk/nrf54l15/cpuapp --device-testing --device-serial /dev/ttyACM1 -T nrf/samples/bluetooth/shell

Basic Command Tests
===================

Test: Command Availability and Initialization
----------------------------------------------

**Type:** Single-board
**Feature:** Basic Shell
**Prerequisites:** None

**Test Steps:**

::

   uart:~$ bt init
   uart:~$ help
   uart:~$ gatt_dm
   uart:~$ multi_adv

**Expected Results:**
- Bluetooth stack initializes successfully
- Help command shows available commands including ``gatt_dm`` and ``multi_adv``
- GATT DM and Multi-adv commands show their subcommands

**Test Configuration:**
   - File: test_shell_basic.yml
   - Test ID: sample.bluetooth.shell.basic

GATT Discovery Manager Tests
=============================

Test: GATT DM Command Syntax
-----------------------------

**Type:** Single-board
**Feature:** GATT Discovery Manager
**Prerequisites:** Bluetooth initialized

**Test Steps:**

::

   uart:~$ bt init
   uart:~$ gatt_dm discover-uuid 0x180F

**Expected Results:**
- Command accepts valid UUID format (0x180F)
- Command executes (may fail due to no connection, but syntax is validated)

**Test Configuration:**
   - File: test_shell_gatt_dm.yml
   - Test ID: sample.bluetooth.shell.gatt_dm

Multiple Advertising Sets Tests
================================

Test: Complete Workflow - Multiple Advertising Sets
----------------------------------------------------

**Type:** Single-board
**Feature:** Multiple Advertising Sets
**Prerequisites:** Bluetooth initialized

**Test Steps:**

::

   uart:~$ bt init
   uart:~$ multi_adv create 0 non-connectable scannable
   uart:~$ multi_adv data 0 name "Nordic Beacon" flags
   uart:~$ multi_adv start 0
   uart:~$ multi_adv create 1 connectable interval-min 100 interval-max 150
   uart:~$ multi_adv data 1 name "Nordic Shell" flags
   uart:~$ multi_adv start 1
   uart:~$ multi_adv list
   uart:~$ multi_adv stop-all
   uart:~$ multi_adv delete 0
   uart:~$ multi_adv delete 1

**Expected Results:**
- Both advertising sets are created successfully
- Both sets have data configured and are started
- List shows both sets with "started" state
- Stop-all stops all started sets
- Delete operations succeed
- All commands execute without errors

**Test Configuration:**
   - File: test_shell_multi_adv.yml
   - Test ID: sample.bluetooth.shell.multi_adv

Notes
=====

* All regex patterns in test files use Python regex syntax (supported by Twister shell harness)
* Commands are executed sequentially; timing-sensitive operations may require delays
* Test files are designed to be idempotent (can be run multiple times)
* Error messages may vary slightly between Zephyr versions; regex patterns account for this
* Multi-board tests require hardware map configuration in Twister

