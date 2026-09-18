#!/bin/bash
# Configures BBB's pins for multiplexed TEC control (GPIO normal, no PRU).
#   SEL1..4 -> current switches (C_SW / G3VM)
#   SHDN    -> On/Off MAX1968
#   I2C2    -> ADS1115 (reads 4 NTC)

# Header pins and their corresponding Linux GPIO sysfs numbers:
# P8_7=66 | P8_8=67 | P8_9=69 | P8_10=68 | P8_11=45
gpio_pins=(P8_07 P8_08 P8_09 P8_10 P8_11)
gpio_nums=(66 67 69 68 45)

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
    # Handled by the tec_controller.cpp sudo config-pin "$p" out  2>/dev/null || true
done

echo "--- Configuring I2C_2 ---"
sudo config-pin P9_19 i2c 2>/dev/null || true   # SCL_2
sudo config-pin P9_20 i2c 2>/dev/null || true   # SDA_2

echo "--- Configuring I2C_1 ---"
sudo config-pin P9_17 i2c 2>/dev/null || true   # SCL_1
sudo config-pin P9_18 i2c 2>/dev/null || true   # SDA_1

echo "--- Configuring state of pins ---"
for p in "${gpio_pins[@]}"; do 
    sudo config-pin -q "$p" || true
done
