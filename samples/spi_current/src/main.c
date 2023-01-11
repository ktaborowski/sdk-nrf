#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>

#define MY_NODE	DT_NODELABEL(my_spi) // my_spi -> spibb0 fixes the problem

int spi_test(void)
{
	// get from devicetree
	const struct device *const dev = DEVICE_DT_GET(MY_NODE);

	if (!device_is_ready(dev)) {
		return -1;
	}

	struct spi_cs_control cs_ctrl = (struct spi_cs_control){
		.gpio = GPIO_DT_SPEC_GET(MY_NODE, cs_gpios),
		.delay = 0u,
	};

	// spi config
	struct spi_config config;

	config.frequency = 8000000;
	config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8);
	config.slave = 0;
	config.cs = &cs_ctrl;

	// prepare data
	enum { datacount = 5 };
	uint8_t buff[datacount] = { 0x1d, 0x08, 0xac, 0, 0 };
	uint8_t rxdata[datacount];

	struct spi_buf tx_buf[1] = {
		{ .buf = buff, .len = datacount },
	};
	struct spi_buf rx_buf[1] = {
		{ .buf = rxdata, .len = datacount },
	};

	struct spi_buf_set tx_set = { .buffers = tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = rx_buf, .count = 1 };

	// transceive
	int ret = spi_transceive(dev, &config, &tx_set, &rx_set);
	if (ret) {
		return -1;
	}

	return 0;
}

void main(void)
{
	for (;;) {
		spi_test();
		k_sleep(K_MSEC(500));
	}

}
