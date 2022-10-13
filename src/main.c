#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell.h>

#include <stdio.h>
#include <string.h>

/* change this to any other UART peripheral if desired */
#define UART_DEVICE "mbus0"

#define MSG_SIZE 32

/* UART ISR callback data */
struct cb_data {
	char   *buf;
	size_t  len;
	int     pos;
};

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_mq, MSG_SIZE, 10, 4);

//static const struct device *dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static int mbus_hello(const struct shell *shell, int argc, char *argv[])
{
	shell_print(shell, "HELO SMTP SPOKEN HERE");
	return 0;
}

SHELL_CMD_REGISTER(mbus, NULL, "Description: foo bar baz", mbus_hello);

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *data)
{
	struct cb_data *cb = (struct cb_data *)data;
	uint8_t c;

	if (!uart_irq_update(dev))
		return;

	while (uart_irq_rx_ready(dev)) {
		uart_fifo_read(dev, &c, 1);

		if ((c == '\n' || c == '\r') && cb->pos > 0) {
			/* terminate string */
			cb->buf[cb->pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_mq, cb->buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			cb->pos = 0;
		} else if (cb->pos < cb->len - 1) {
			cb->buf[cb->pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(const struct device *dev, char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++)
		uart_poll_out(dev, buf[i]);
}

void main(void)
{
	const struct device *dev;
	struct uart_config cfg;
	char rx_buf[MSG_SIZE];
	char tx_buf[MSG_SIZE];
	struct cb_data cb = {
		.buf = rx_buf,
		.len = sizeof(rx_buf),
		.pos = 0,
	};

        dev = device_get_binding(UART_DEVICE);
	if (!dev) {
		printf("Cannot find device tree node %s\n", UART_DEVICE);
		return;
	}
	if (!device_is_ready(dev)) {
		printk("UART %s device not found!\n", UART_DEVICE);
		return;
	}

	if (uart_config_get(dev, &cfg)) {
		printk("Failed reading UART %s config\n", UART_DEVICE);
		return;
	}

	printk("Setting UART %s to 2400 baud even parity, one stop bit\n", UART_DEVICE);
	cfg.baudrate = 2400;
	cfg.parity = UART_CFG_PARITY_EVEN;
	if (uart_configure(dev, &cfg)) {
		printk("Failed setting up UART %s config\n", UART_DEVICE);
		return;
	}

	printf("See other UART for program ...\n");

	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(dev, serial_cb, &cb);
	uart_irq_rx_enable(dev);

	print_uart(dev, "Hello! I'm your echo bot.\r\n");
	print_uart(dev, "Tell me something and press enter: ");

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_mq, &tx_buf, K_FOREVER) == 0) {
		print_uart(dev, "\r\nEcho: ");
		print_uart(dev, tx_buf);
		print_uart(dev, "\r\n");
		print_uart(dev, "Tell me something and press enter: ");
	}
}
