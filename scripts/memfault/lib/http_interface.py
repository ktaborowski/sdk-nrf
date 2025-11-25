#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import abc
from typing import Dict, List


class HTTPInterface(abc.ABC):
    """
    Abstract interface for HTTP operations.
    Implementations should provide HTTP POST functionality.
    """

    @abc.abstractmethod
    def post(self, url: str, headers: Dict[str, str], data: bytes) -> None:
        """
        Perform HTTP POST request.

        Args:
            url: Target URL
            headers: HTTP headers dictionary
            data: Request body as bytes

        Raises:
            Exception: If the request fails
        """
        pass


class RequestsHTTP(HTTPInterface):
    """
    HTTP implementation using the requests library.
    """

    def __init__(self):
        import requests
        self.requests = requests

    def post(self, url: str, headers: Dict[str, str], data: bytes) -> None:
        """
        Perform HTTP POST request using requests library.

        Args:
            url: Target URL
            headers: HTTP headers dictionary
            data: Request body as bytes

        Raises:
            requests.HTTPError: If the request fails
        """
        response = self.requests.post(url, headers=headers, data=data)
        response.raise_for_status()

