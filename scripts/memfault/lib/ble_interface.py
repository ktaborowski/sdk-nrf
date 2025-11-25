#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import abc
from typing import Callable, Optional, List


class BLEInterface(abc.ABC):
    """
    Abstract interface for BLE operations.
    Implementations should provide BLE connectivity and GATT operations.
    """

    ERROR_NOCONN = -1
    DEFAULT_MTU = 247
    STATUS_SUCCESS = 0  # Status code indicating success (implementation-specific)

    @abc.abstractmethod
    def connect(self, conn_timeout: int, bond: bool) -> None:
        """
        Establish BLE connection to a device advertising MDS service.

        Args:
            conn_timeout: Connection timeout in seconds
            bond: Whether to enable bonding

        Raises:
            Exception: If connection fails or times out
        """
        pass

    @abc.abstractmethod
    def disconnect(self) -> None:
        """Disconnect from the current BLE connection."""
        pass

    @abc.abstractmethod
    def read_characteristic(self, uuid_offset: int, offset: int = 0,
                            raise_on_error: bool = True) -> tuple[int, List[int]]:
        """
        Read a GATT characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            offset: Read offset in bytes
            raise_on_error: If True, raise exception on error; if False, return error status

        Returns:
            Tuple of (status_code, data_bytes). status_code is STATUS_SUCCESS (0) on success.
        """
        pass

    @abc.abstractmethod
    def write_characteristic(self, uuid_offset: int, data: List[int]) -> None:
        """
        Write to a GATT characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            data: Data bytes to write
        """
        pass

    @abc.abstractmethod
    def enable_notification(self, uuid_offset: int, callback: Callable[[List[int]], None]) -> None:
        """
        Enable notifications for a characteristic.

        Args:
            uuid_offset: Characteristic UUID offset (16-bit UUID within MDS base UUID)
            callback: Callback function to receive notification data
        """
        pass

    @abc.abstractmethod
    def wait_for_disconnection(self, timeout: Optional[int] = None) -> None:
        """
        Wait for disconnection event.

        Args:
            timeout: Timeout in seconds (None for infinite wait)
        """
        pass

    @abc.abstractmethod
    def close(self) -> None:
        """Close and cleanup BLE resources."""
        pass

    @property
    @abc.abstractmethod
    def att_mtu(self) -> int:
        """Get current ATT MTU size."""
        pass

