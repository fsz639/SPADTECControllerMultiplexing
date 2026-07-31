#!/bin/bash
# Configures BBB's pins for multiplexed TEC control (GPIO normal, no PRU).
#   SEL1..4 -> current switches (C_SW / G3VM)
#   SHDN    -> On/Off MAX1968
#   I2C2    -> ADS1115 (reads 4 NTC)

# Header pins and their corresponding Linux GPIO sysfs numbers:
# P8_12=44 | P8_11=45 | P8_16=46 | P8_15=47 | P8_14=26
gpio_pins=(P8_12 P8_11 P8_16 P8_15 P8_14)
gpio_nums=(44 45 46 47 26)

echo "--- Resetting GPIOs ---"
for num in "${gpio_nums[@]}"; do
    if [ -d "/sys/class/gpio/gpio${num}" ]; then
        echo "$num" > /sys/class/gpio/unexport 2>/dev/null || true
    fi
done

echo "--- Configuring Pinmux ---"
for p in "${gpio_pins[@]}"; do
    # Reset mode to gpio first, then set to output
    sudo config-pin "$p" gpio 2>/dev/null || true
    sudo config-pin "$p" out  2>/dev/null || true
done

echo "--- Configuring I2C2 ---"
sudo config-pin P9_19 i2c 2>/dev/null || true   # SCL
sudo config-pin P9_20 i2c 2>/dev/null || true   # SDA

echo "--- Configuring state of pins ---"
for p in "${gpio_pins[@]}"; do 
    sudo config-pin -q "$p" || true
done
