#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import logging
import time

from pc_ble_driver_py import config

config.__conn_ic_id__ = "nrf52"

from pc_ble_driver_py.exceptions import NordicSemiException
from pc_ble_driver_py.ble_driver import (BLEDriver, BLEDriverObserver, BLEUUIDBase,
                                          BLEUUID, Flasher)
from pc_ble_driver_py.ble_driver import (BLEConfig, BLEConfigConnGatt, BLEAdvData,
                                          BLEGapConnParams, BLEGattStatusCode,
                                          BLEGapSecStatus, BLEHci)
from pc_ble_driver_py.ble_adapter import BLEAdapter, BLEAdapterObserver, EvtSync

from .ble_interface import BLEInterface

logger = logging.getLogger(__name__)

BLE_GAP_PHY_2MBPS = 0x02


class PCBLEDriverPyBLE(BLEInterface, BLEDriverObserver, BLEAdapterObserver):
    """
    BLE implementation using pc_ble_driver_py library.
    """

    CFG_TAG = 1

    def __init__(self, comm: str):
        """
        Initialize BLE driver and adapter.

        Args:
            comm: COM port name
        """
        # Define constants locally to avoid circular dependency
        # These match the constants in mds_ble_gateway.py
        MDS_UUID = [0x54, 0x22, 0x00, 0x00, 0xf6, 0xa5, 0x40, 0x07, 0xa3, 0x71, 0x72, 0x2f, 0x4e, 0xbd,
                    0x84, 0x36]
        MDS_SVC_UUID_OFFSET = 0x0000

        driver = BLEDriver(serial_port=comm, baud_rate=1000000)
        adapter = BLEAdapter(driver)

        self.adapter = adapter
        self.evt_sync = EvtSync(['connected', 'disconnected'])

        self.adapter.observer_register(self)
        self.adapter.driver.observer_register(self)
        self.adapter.driver.open()

        self.conn_handle = None
        self.keys = None
        self._att_mtu = self.adapter.default_mtu
        self._notification_callback = None

        # Create UUID objects
        self.BASE_UUID = BLEUUIDBase(MDS_UUID)
        self.MDS_SVC_UUID = BLEUUID(MDS_SVC_UUID_OFFSET, self.BASE_UUID)
        self.DATA_EXPORT_UUID = None  # Will be set when needed via uuid_offset

        gatt_cfg = BLEConfigConnGatt()
        gatt_cfg.att_mtu = BLEInterface.DEFAULT_MTU
        gatt_cfg.conn_cfg_tag = PCBLEDriverPyBLE.CFG_TAG

        self.adapter.driver.ble_cfg_set(BLEConfig.conn_gatt, gatt_cfg)
        self.adapter.driver.ble_enable()
        self.adapter.driver.ble_vs_uuid_add(self.BASE_UUID)

    @property
    def att_mtu(self) -> int:
        """Get current ATT MTU size."""
        return self._att_mtu

    def connect(self, conn_timeout: int, bond: bool) -> None:
        """
        Establish BLE connection to a device advertising MDS service.

        Args:
            conn_timeout: Connection timeout in seconds
            bond: Whether to enable bonding

        Raises:
            NordicSemiException: If connection fails or times out
        """
        logger.debug('BLE: Scanning...')
        self.adapter.driver.ble_gap_scan_start()
        self.conn_handle = self.evt_sync.wait('connected', timeout=conn_timeout)

        if self.conn_handle is None:
            raise NordicSemiException('Timeout. BLE Memfault target device not found',
                                      BLEInterface.ERROR_NOCONN)

        logger.info('BLE: Connected to the Memfault target')

        # Try to set longer ATT MTU
        self._att_mtu = self.adapter.att_mtu_exchange(self.conn_handle,
                                                       BLEInterface.DEFAULT_MTU)
        logging.info(f'BLE: Current ATT MTU: {self._att_mtu}')

        # Try to use 2Mb PHY
        status = self.adapter.phy_update(self.conn_handle,
                                         [BLE_GAP_PHY_2MBPS, BLE_GAP_PHY_2MBPS])
        if status['status'] == BLEHci.success:
            logger.info(f"BLE: Phy updated tx_phy: {status['tx_phy']} "
                       f"rx_phy: {status['rx_phy']}")

        self.adapter.service_discovery(self.conn_handle, self.MDS_SVC_UUID)

        if not self.keys:
            self.adapter.authenticate(conn_handle=self.conn_handle,
                                      _role=self.adapter.db_conns[self.conn_handle].role,
                                      bond=bond)

            # Simulate a bonding, bond will be stored during a lifetime of this script.
            if bond:
                self.keys = self.adapter.db_conns[self.conn_handle]._keyset
        else:
            enc_key = self.keys.keys_peer.enc_key
            self.adapter.encrypt(self.conn_handle, enc_key.master_id.ediv,
                                 enc_key.master_id.rand, enc_key.enc_info.ltk,
                                 enc_key.enc_info.auth)

    def disconnect(self) -> None:
        """Disconnect from the current BLE connection."""
        self.adapter.disconnect(self.conn_handle)

    def read_characteristic(self, uuid_offset: int, offset: int = 0,
                            raise_on_error: bool = True) -> tuple[int, list]:
        """
        Read a GATT characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            offset: Read offset in bytes
            raise_on_error: If True, raise exception on error; if False, return error status

        Returns:
            Tuple of (status_code, data_bytes). status_code is STATUS_SUCCESS (0) on success.

        Raises:
            NordicSemiException: If read fails and raise_on_error is True
        """
        uuid = BLEUUID(uuid_offset, self.BASE_UUID)
        status = self.adapter.read_req(self.conn_handle, uuid, offset=offset)
        if status[0] != BLEGattStatusCode.success:
            if raise_on_error:
                raise NordicSemiException(f'Read characteristic returned error status: {status[0]}')
            return (status[0], status[1])

        # Return status code as integer (0 for success) and data
        return (BLEInterface.STATUS_SUCCESS, status[1])

    def write_characteristic(self, uuid_offset: int, data: list) -> None:
        """
        Write to a GATT characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            data: Data bytes to write
        """
        uuid = BLEUUID(uuid_offset, self.BASE_UUID)
        self.adapter.write_req(self.conn_handle, uuid, data)

    def enable_notification(self, uuid_offset: int, callback) -> None:
        """
        Enable notifications for a characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            callback: Callback function to receive notification data
        """
        self._notification_callback = callback
        uuid = BLEUUID(uuid_offset, self.BASE_UUID)
        self.adapter.enable_notification(conn_handle=self.conn_handle, uuid=uuid)

    def wait_for_disconnection(self, timeout: int = None) -> None:
        """
        Wait for disconnection event.

        Args:
            timeout: Timeout in seconds (None for infinite wait)
        """
        self.evt_sync.wait('disconnected', timeout=timeout)

    def close(self) -> None:
        """Close and cleanup BLE resources."""
        self.adapter.close()

    def on_gap_evt_adv_report(self, ble_driver, conn_handle, peer_addr, rssi, adv_type,
                              adv_data):
        """Handle advertising report event."""
        # Define constants locally to avoid circular dependency
        MDS_UUID = [0x54, 0x22, 0x00, 0x00, 0xf6, 0xa5, 0x40, 0x07, 0xa3, 0x71, 0x72, 0x2f, 0x4e, 0xbd,
                    0x84, 0x36]

        uuid = 0

        if BLEAdvData.Types.service_128bit_uuid_complete in adv_data.records:
            uuid = adv_data.records[BLEAdvData.Types.service_128bit_uuid_complete]
            uuid.reverse()

            logger.info('BLE: Received advertising packet with the MDS UUID')

        address = "".join(f'{b:02X}:' for b in peer_addr.addr)
        address = address[:-1]

        logger.debug(f'BLE: Received advertisement report, address: {address}')

        if uuid == MDS_UUID:
            conn_params = BLEGapConnParams(min_conn_interval_ms=15,
                                           max_conn_interval_ms=30,
                                           conn_sup_timeout_ms=4000,
                                           slave_latency=0)

            logger.info(f'BLE: Connecting to {address}')
            self.adapter.connect(address=peer_addr, conn_params=conn_params,
                                 tag=self.CFG_TAG)

    def on_gap_evt_connected(self, ble_driver, conn_handle, peer_addr, role, conn_params):
        """Handle connection event."""
        self.evt_sync.notify('connected', data=conn_handle)

    def on_gap_evt_disconnected(self, ble_driver, conn_handle, reason):
        """Handle disconnection event."""
        self.evt_sync.notify('disconnected', data=conn_handle)
        logger.info(f'BLE: Disconnected (reason: {reason})')

    def on_gap_evt_sec_request(self, ble_driver, conn_handle, bond, mitm, lesc, keypress):
        """Handle security request event."""
        if conn_handle != self.conn_handle:
            return

        logger.info(f'BLE: Security request, bond: {bond}')

    def on_gap_evt_conn_sec_update(self, ble_driver, conn_handle, conn_sec):
        """Handle connection security update event."""
        if conn_handle != self.conn_handle:
            return

        logger.info(f'BLE: Security changed level: {conn_sec.sec_mode.lv}')

    def on_gap_evt_auth_status(self, ble_driver, conn_handle, **kwargs):
        """Handle authentication status event."""
        if conn_handle != self.conn_handle:
            return

        status = kwargs.get('auth_status')
        if status == BLEGapSecStatus.success:
            status_str = 'Success'
        else:
            status_str = 'Failed'

        logger.info(f'BLE: Authentication status conn_handle: {conn_handle} '
                   f'status: {status_str}')

    def on_notification(self, ble_adapter, conn_handle, uuid, data):
        """Handle notification event."""
        if conn_handle != self.conn_handle:
            return

        if uuid != self.DATA_EXPORT_UUID:
            return

        logger.debug(f'Received data export notification, data length: {len(data)}')

        if self._notification_callback:
            self._notification_callback(data)

def flash_device(comm: str, snr: str, erase: bool) -> None:
    """
    Flash device with connectivity firmware if needed.

    Args:
        comm: COM port name
        snr: Segger chip ID
        erase: Whether to erase device before flashing
    """
    flasher = Flasher(comm, snr)

    if erase:
        logger.info("Erasing...")
        flasher.erase()
        logger.info("Erased")

    if flasher.fw_check():
        logger.info("Device is already flashed with connectivity firmware")
        logger.info("Restarting device...")
        flasher.reset()
        time.sleep(1)
    else:
        logger.info("Flashing firmware...")
        flasher.fw_flash()
        logger.info("Firmware flashed")
        logger.info("Restarting...")
        flasher.reset()
        time.sleep(1)