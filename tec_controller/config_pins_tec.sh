#!/bin/bash
# Configura los pines de la BBB para el control TEC (GPIO normal, sin PRU).
#   SEL1..4 -> current switches (C_SW / G3VM)
#   SHDN    -> On/Off del MAX1968
#   I2C2    -> ADS1115 (lee las 4 NTC)
set -euo pipefail

# pin : gpio sysfs : senal
#  P8_12=44 P8_11=45 P8_16=46 P8_15=47 (SEL1..4)   P8_14=26 (SHDN)
gpio_pins=(P8_12 P8_11 P8_16 P8_15 P8_14)

echo "--- Configurando pines GPIO ---"
for p in "${gpio_pins[@]}"; do
    # 'out' sets both pinmux mode and output direction in one command.
    # '|| true' ensures 'set -e' won't crash if the pin is already exported/busy.
    config-pin "$p" out 2>/dev/null || true
done

echo "--- Configurando I2C2 ---"
config-pin P9_19 i2c 2>/dev/null || true   # SCL
config-pin P9_20 i2c 2>/dev/null || true   # SDA

echo "--- Estado de los pines ---"
for p in "${gpio_pins[@]}"; do 
    config-pin -q "$p" || true
done
