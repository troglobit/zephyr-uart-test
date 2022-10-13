Welcome To the UART Playground
==============================

How to build this, provided you have already set up your NCS/nRF
toolchain on your Linux system.

	west build -t menuconfig -b nrf5340dk_nrf5340_cpuapp -- -DDTC_OVERLAY_FILE=uart-test.overlay
    rm -rf build/; west build -c -b nrf5340dk_nrf5340_cpuapp -- -DDTC_OVERLAY_FILE=uart-test.overlay
	west flash

**Note:** ttyACM0 runs at 2400 baud while the shell on tyyACM1 runs at 115200 baud.

    minicom -b 2400 -D /dev/ttyACM0
	minicom -b 115200 -D /dev/ttyACM1
	
	
