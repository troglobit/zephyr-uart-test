/* Very basic UART test program preparing for M-Bus
 *
 * Note: the emulated UARTs on the nRF5340DK, via the SEGGER interface,
 *       don't like non-standard stop or parity bits.  Only 8N1 work.
 */
#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MODULE         MBUS
#define UART_DEVICE    "mbus0"
#define RING_BUF_SIZE  1024
#define MSG_SIZE       128

LOG_MODULE_REGISTER(MODULE);

/* UART ISR callback data */
struct cb_data {
	char   *buf;
	size_t  len;
	int     pos;
};

static uint8_t ring_buffer[RING_BUF_SIZE];
static struct ring_buf ringbuf;

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_mq, MSG_SIZE, 10, 4);

#ifdef CONFIG_SHELL
static int mbus_hello(const struct shell *shell, int argc, char *argv[])
{
	shell_print(shell, "HELO SMTP SPOKEN HERE");
	return 0;
}

SHELL_CMD_REGISTER(mbus, NULL, "Description: foo bar baz", mbus_hello);
#endif

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *data)
{
	struct cb_data *cb = (struct cb_data *)data;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if  (uart_irq_rx_ready(dev)) {
			uint8_t c;

			uart_fifo_read(dev, &c, 1);
			if ((c == '\n' || c == '\r') && cb->pos > 0) {
				/* terminate string */
				cb->buf[cb->pos] = '\0';

				LOG_HEXDUMP_INF(cb->buf, cb->pos, "RX IN");

				/* if queue is full, message is silently dropped */
				k_msgq_put(&uart_mq, cb->buf, K_NO_WAIT);

				/* reset the buffer (it was copied to the msgq) */
				cb->pos = 0;
			} else if (cb->pos < cb->len - 1) {
				cb->buf[cb->pos++] = c;
			}
			/* else: characters beyond buffer size are dropped */
		}

		if (uart_irq_tx_ready(dev)) {
			char buf[CONFIG_UART_1_NRF_TX_BUFFER_SIZE];
			size_t rlen, slen;

			rlen = ring_buf_get(&ringbuf, buf, sizeof(buf));
			if (!rlen) {
				LOG_DBG("Ring buffer empty, disable TX IRQ");
				uart_irq_tx_disable(dev);
				continue;
			}

			slen = uart_fifo_fill(dev, buf, rlen);
			if (slen < rlen)
				LOG_ERR("Drop %zd bytes", rlen - slen);
			LOG_HEXDUMP_INF(buf, slen, "TX OUT");
		}
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
static void uprintf(const struct device *dev, const char *fmt, ...)
{
	char buf[MSG_SIZE];
	size_t len, rlen;
	va_list va;

	va_start(va, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	rlen = ring_buf_put(&ringbuf, buf, len);
	if (rlen < len)
		LOG_ERR("Drop %zd bytes", len - rlen);
	if (rlen)
		uart_irq_tx_enable(dev);
}

void main(void)
{
	const int baudrate = 2400;
	const struct device *dev;
	struct uart_config cfg;
	char rx_buf[MSG_SIZE];
	char msg_buf[MSG_SIZE];
	struct cb_data cb = {
		.buf = rx_buf,
		.len = sizeof(rx_buf),
		.pos = 0,
	};

	ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

        dev = device_get_binding(UART_DEVICE);
	if (!dev) {
		LOG_ERR("Cannot find device tree node %s", UART_DEVICE);
		return;
	}
	if (!device_is_ready(dev)) {
		LOG_ERR("UART %s device not found!", UART_DEVICE);
		return;
	}

	if (uart_config_get(dev, &cfg)) {
		LOG_ERR("Failed reading UART %s config", UART_DEVICE);
		return;
	}

	LOG_INF("Setting UART %s to %d baud even parity, one stop bit", UART_DEVICE, baudrate);
	cfg.baudrate = baudrate;
//	cfg.parity = UART_CFG_PARITY_EVEN;  DOES NOT WORK WITH SEGGER TTYACM0/TTYACM1
	if (uart_configure(dev, &cfg)) {
		LOG_ERR("Failed setting up UART %s config", UART_DEVICE);
		return;
	}

	uart_config_get(dev, &cfg);
	LOG_INF("See other UART (%d,%d,%d) for running program ...",
	       cfg.baudrate, cfg.parity, cfg.stop_bits);

	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(dev, serial_cb, &cb);
	uart_irq_rx_enable(dev);

	uprintf(dev, "\n\n\rHello! I'm your echo bot.\r\n");
	uprintf(dev, "Tell me something and press enter: ");

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_mq, &msg_buf, K_FOREVER) == 0) {
		LOG_INF("Got data: %s", msg_buf);
		uprintf(dev, "\r\nEcho: %s\r\n", msg_buf);
		uprintf(dev, "Tell me something and press enter: ");
	}
}
