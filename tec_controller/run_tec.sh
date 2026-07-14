#!/usr/bin/env bash
# Arranca el control TEC en la BBB:
#   1) configura los pines (PRU salidas + I2C)
#   2) lanza el programa (carga el PRU y monitoriza temperaturas)
set -euo pipefail
cd "$(dirname "$0")"

sudo ./config_pins_tec.sh
sudo ./tec_controller
