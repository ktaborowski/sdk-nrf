#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import argparse
import logging
import time
import os
import sys
from pathlib import Path

# Add current directory to path for imports when run as script
sys.path.insert(0, str(Path(__file__).parent))

from lib.ble_interface import BLEInterface
from lib.http_interface import HTTPInterface

# MDS (Memfault Diagnostic Service) constants
MDS_UUID = [0x54, 0x22, 0x00, 0x00, 0xf6, 0xa5, 0x40, 0x07, 0xa3, 0x71, 0x72, 0x2f, 0x4e, 0xbd,
            0x84, 0x36]

# Characteristic UUID offsets (16-bit UUIDs within MDS base UUID)
MDS_SVC_UUID_OFFSET = 0x0000
SUPPORTED_FEATURES_UUID_OFFSET = 0x0001
DEVICE_IDENTIFIER_UUID_OFFSET = 0x0002
DATA_URI_UUID_OFFSET = 0x0003
AUTHORIZATION_UUID_OFFSET = 0x0004
DATA_EXPORT_UUID_OFFSET = 0x0005

DATA_STREAM_ENABLE = [0x01]
DATA_STREAM_DISABLE = [0x00]

logger = logging.getLogger(__name__)


class BLEMemfault:
    """
    A class used to represent a MDS client instance.
    It is used to perform connection with device running the MDS service.
    """

    ERROR_NOCONN = -1
    DEFAULT_MTU = 247

    def __init__(self, ble_interface: BLEInterface, bond: bool, conn_timeout: int):
        """
        Initialize BLE Memfault client.

        Args:
            ble_interface: BLE interface implementation
            bond: Whether to enable bonding
            conn_timeout: Connection timeout in seconds
        """
        self.ble = ble_interface
        self.conn_timeout = conn_timeout
        self.bond = bond

    def data_export_received(self, data):
        """Callback for data export notifications. Override in subclasses."""
        pass

    def connect(self):
        """Establish BLE connection and perform service discovery."""
        self.ble.connect(self.conn_timeout, self.bond)

    def disconnect(self):
        """Disconnect from BLE device."""
        self.ble.disconnect()

    def read_supported_features(self):
        """Read supported features characteristic."""
        status, data = self.ble.read_characteristic(SUPPORTED_FEATURES_UUID_OFFSET)
        return data

    def read_device_identifier(self):
        """Read device identifier characteristic."""
        status, data = self.ble.read_characteristic(DEVICE_IDENTIFIER_UUID_OFFSET)
        device_identifier = "".join(chr(c) for c in data)
        return device_identifier

    def read_data_uri(self):
        """Read data URI characteristic (may require multiple reads)."""
        status, data = self.ble.read_characteristic(DATA_URI_UUID_OFFSET, raise_on_error=True)

        while status == BLEInterface.STATUS_SUCCESS:
            offset = len(data)

            if len(data) < (self.ble.att_mtu - 3):
                break

            status, chunk_data = self.ble.read_characteristic(DATA_URI_UUID_OFFSET, offset=offset,
                                                               raise_on_error=False)
            if status == BLEInterface.STATUS_SUCCESS:
                data.extend(chunk_data)
            else:
                break

        uri = "".join(chr(c) for c in data)
        return uri

    def read_authorization(self):
        """Read authorization characteristic (may require multiple reads)."""
        status, data = self.ble.read_characteristic(AUTHORIZATION_UUID_OFFSET, raise_on_error=True)

        while status == BLEInterface.STATUS_SUCCESS:
            offset = len(data)

            if len(data) < (self.ble.att_mtu - 3):
                break

            status, chunk_data = self.ble.read_characteristic(AUTHORIZATION_UUID_OFFSET,
                                                               offset=offset, raise_on_error=False)
            if status == BLEInterface.STATUS_SUCCESS:
                data.extend(chunk_data)
            else:
                break

        project_key = "".join(chr(c) for c in data)
        return project_key

    def data_export_notification_enable(self):
        """Enable notifications for data export characteristic."""
        self.ble.enable_notification(DATA_EXPORT_UUID_OFFSET, self.data_export_received)

    def data_export_write(self, data):
        """Write to data export characteristic."""
        self.ble.write_characteristic(DATA_EXPORT_UUID_OFFSET, data)

    def wait_for_disconnection(self):
        """Wait for disconnection event."""
        self.ble.wait_for_disconnection()

    def close(self):
        """Close BLE resources."""
        self.ble.close()


class Memfault(BLEMemfault):
    """
    Memfault class performs operations on the MDS service to get necessary data to forward
    a diagnostic data to the cloud.
    """

    MAX_CHUNK_NUMBER = 31

    def __init__(self, ble_interface: BLEInterface, http_interface: HTTPInterface,
                 bond: bool = True, conn_timeout: int = 5):
        """
        Initialize Memfault client.

        Args:
            ble_interface: BLE interface implementation
            http_interface: HTTP interface implementation
            bond: Whether to enable bonding
            conn_timeout: Connection timeout in seconds
        """
        super().__init__(ble_interface, bond, conn_timeout)
        self.http = http_interface

        self.chunk_counter = 0
        self.expected_chunk_number = 0
        self.reconnection = False

    def connect(self):
        """Establish connection and read MDS service data."""
        super().connect()

        self.device_identifier = self.read_device_identifier()
        self.uri = self.read_data_uri()
        self.project_key = self.read_authorization()
        self.supported_features = self.read_supported_features()

        logger.info(f'Device identifier {self.device_identifier}, '
                    f'supported features: {self.supported_features}')
        logger.info('MDS Data Export: Enabling data stream')

        self.data_export_notification_enable()
        self.data_export_write(DATA_STREAM_ENABLE)

        logger.info('MDS Data Export: Data stream enabled')

        self.reconnection = True

    def wait_for_disconnection(self):
        """Wait for disconnection and reset chunk counter."""
        super().wait_for_disconnection()
        self.expected_chunk_number = 0

    def data_export_received(self, data):
        """Handle data export notification."""
        if len(data) < 1:
            return

        chunk_cn = data.pop(0) & 0x1F

        logger.debug(f'Received chunk data CN: {chunk_cn}, data: {data}')

        if chunk_cn != self.expected_chunk_number:
            logger.warning('Invalid chunk number, data lost or duplicated packet! '
                          f'Expected chunk number was {self.expected_chunk_number} '
                          f'but got {chunk_cn}')

        self.expected_chunk_number = (self.expected_chunk_number + 1) % \
                                     (Memfault.MAX_CHUNK_NUMBER + 1)
        self._upload_chunk(data)

        self.chunk_counter += 1

        self.print_progress(self.chunk_counter, self.reconnection)

        self.reconnection = False

    def _upload_chunk(self, data):
        """Upload chunk to Memfault cloud via HTTP."""
        headers = {}

        key = self.project_key.split(':')
        auth_key = key[0]
        auth_value = key[1]

        headers[auth_key] = auth_value
        headers['Content-Type'] = 'application/octet-stream'

        try:
            self.http.post(self.uri, headers, bytes(data))
        except Exception as err:
            logger.info(f'{err}')
            self.ble.disconnect()

    @staticmethod
    def print_progress(chunk_number, reconnection):
        """Print progress indicator."""
        dots_num = (chunk_number % 4)
        dots = ''.join('.' * dots_num)

        if chunk_number == 1 or reconnection:
            print()
        else:
            print('\33[3A')

        print(f'\33[2K\rSending {dots}')
        print(f'\rForwarded \033[92m{chunk_number}\033[39m Memfault Chunks')


def load_config() -> dict:
    """
    Load configuration from environment variables.

    Returns:
        Dictionary with configuration values
    """
    return {
        'ble_library': os.getenv('MDS_BLE_LIBRARY', 'pc_ble_driver_py'),
        'http_library': os.getenv('MDS_HTTP_LIBRARY', 'requests')
    }


def create_ble_interface(library: str, comm: str, snr: str = None, erase: bool = False) -> BLEInterface:
    """
    Create BLE interface implementation based on library name.
    Flashes device if snr is provided.

    Args:
        library: Library name ('pc_ble_driver_py' or other)
        comm: COM port name
        snr: Segger chip ID (optional, if provided device will be flashed)
        erase: Whether to erase device before flashing (only used if snr is provided)

    Returns:
        BLEInterface implementation

    Raises:
        ValueError: If library is not supported
    """
    if library == 'pc_ble_driver_py':
        # Flash device if snr is provided (before creating interface)
        if snr:
            from lib.ble_pc_ble_driver_py import flash_device
            flash_device(comm, snr, erase)
        
        from lib.ble_pc_ble_driver_py import PCBLEDriverPyBLE
        return PCBLEDriverPyBLE(comm)
    else:
        raise ValueError(f'Unsupported BLE library: {library}')


def create_http_interface(library: str) -> HTTPInterface:
    """
    Create HTTP interface implementation based on library name.

    Args:
        library: Library name ('requests' or other)

    Returns:
        HTTPInterface implementation

    Raises:
        ValueError: If library is not supported
    """
    if library == 'requests':
        from lib.http_interface import RequestsHTTP
        return RequestsHTTP()
    else:
        raise ValueError(f'Unsupported HTTP library: {library}')


def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='Memfault BLE gateway',
                                     allow_abbrev=False)
    parser.add_argument('--snr', type=str, required=True, help='Segger chip ID')
    parser.add_argument('--com', type=str, required=True,
                        help='COM port name. For example COM0 or /dev/ttyACM0')
    parser.add_argument('--erase', '-e', action='store_true',
                        help='Erase target device before flashing the firmware')
    parser.add_argument('--bond-disable', action='store_false', dest='bond',
                        help='Disable bonding simulation')
    parser.add_argument('--reconnections', type=int, help='Number of reconnection attempts',
                        default=0)
    parser.add_argument('--timeout', '-t', type=int, default=5,
                        help='Connection establish timeout in seconds')

    args = parser.parse_args()

    return args


if __name__ == '__main__':
    logging.basicConfig(format='%(message)s', level=logging.INFO)

    print("MDS BLE gateway application started")

    args = parse_args()

    # Load configuration from environment variables
    config = load_config()

    # Create interfaces
    ble_interface = create_ble_interface(config['ble_library'], args.com, args.snr, args.erase)
    http_interface = create_http_interface(config['http_library'])

    # Create Memfault client
    memfault = Memfault(ble_interface, http_interface, bond=args.bond,
                       conn_timeout=args.timeout)

    reconnection_count = args.reconnections

    try:
        while True:
            try:
                memfault.connect()
            except Exception as err:
                # Check if it's a connection timeout error
                if hasattr(err, 'error_code') and err.error_code == BLEMemfault.ERROR_NOCONN:
                    print('Connection establish timeout.')
                    if reconnection_count > 0:
                        print('Trying to reconnect..')
                        reconnection_count -= 1
                        continue
                    else:
                        break
                else:
                    raise err

            memfault.wait_for_disconnection()
            # Disconnection, reset the reconnection count
            reconnection_count = args.reconnections

            time.sleep(5)
    except KeyboardInterrupt:
        pass

    finally:
        memfault.close()
