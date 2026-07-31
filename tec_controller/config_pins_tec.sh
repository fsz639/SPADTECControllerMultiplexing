#!/bin/bash
# Configura los pines de la BBB para el control TEC (GPIO normal, sin PRU).
#   SEL1..4 -> current switches (C_SW / G3VM)
#   SHDN    -> On/Off del MAX1968
#   I2C2    -> ADS1115 (lee las 4 NTC)
# set -euo pipefail

# Header pins and their corresponding Linux GPIO sysfs numbers:
# P8_12=44 | P8_11=45 | P8_16=46 | P8_15=47 | P8_14=26
gpio_pins=(P8_12 P8_11 P8_16 P8_15 P8_14)
gpio_nums=(44 45 46 47 26)

echo "--- 1. Resetting Sysfs GPIOs (Preventing EBUSY) ---"
for num in "${gpio_nums[@]}"; do
    if [ -d "/sys/class/gpio/gpio${num}" ]; then
        echo "$num" > /sys/class/gpio/unexport 2>/dev/null || true
    fi
done

echo "--- 2. Configuring Pinmux ---"
for p in "${gpio_pins[@]}"; do
    # Reset mode to gpio first, then set to output
    config-pin "$p" gpio 2>/dev/null || true
    config-pin "$p" out  2>/dev/null || true
done

echo "--- 3. Configuring I2C2 ---"
config-pin P9_19 i2c 2>/dev/null || true   # SCL
config-pin P9_20 i2c 2>/dev/null || true   # SDA

echo "--- 4. Estado de los pines ---"
for p in "${gpio_pins[@]}"; do 
    config-pin -q "$p" || true
done
