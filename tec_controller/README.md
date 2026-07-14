# Control TEC — BBB

Software de la BeagleBone Black para el controlador de temperatura de los 4 SPAD
del rack. Un único MAX1968 (corriente **fija**, CTLI ≈ 1,3 V) se reparte entre los
4 TEC con los current switches (4 GPIO) + On/Off (SHDN). Las NTC se leen por I²C
(ADS1115). Todo en el ARM (Linux): GPIO por sysfs + I²C por `/dev/i2c-2`.

## Modos (argumento de línea de comandos)
| Comando | Qué hace |
|---|---|
| `./tec_controller` (o `open`) | **LAZO ABIERTO** — corriente fija, multiplexa los 4 TEC y monitoriza. **← usar ahora** |
| `./tec_controller regulate` | **LAZO CERRADO** (termostato 15-18 °C). ← futuro |
| `./tec_controller test` | **PRUEBA MANUAL de banco** (comandos por teclado, ver abajo) |

### Modo prueba (`test`) — para verificar en banco
```
 1..4  seleccionar canal (cierra ese current switch)
 o / f SHDN On / oFf (da / corta corriente al canal seleccionado)
 r     leer las 4 NTC (temperatura)
 x     todo a reposo
 q     salir
```

## Mapa de pines (cableado J_BBB → BBB)
| J_BBB | Función | Pin BBB | GPIO |
|---|---|---|---|
| SEL1-4 | current switches | P8_12/11/16/15 | 44/45/46/47 |
| SHDN | On/Off MAX1968 | P8_14 | 26 |
| SCL/SDA | I²C (ADS1115@0x48) | P9_19/P9_20 | /dev/i2c-2 |

## Compilar y ejecutar
```bash
# en la VM (host):
make                       # -> tec_controller
python3 verify_sim.py      # (opcional) verifica la logica
# en la BBB:
sudo ./config_pins_tec.sh  # configura los pines
sudo ./tec_controller test # prueba de banco   (o sin 'test' para lazo abierto)
```

## Ficheros
`tec_controller.cpp` · `config_pins_tec.sh` · `Makefile` · `run_tec.sh` · `verify_sim.py`

## ⚠️ A confirmar en hardware
1. Los 5 GPIO libres y cableados como la tabla.
2. Bus I²C = `/dev/i2c-2` (`i2cdetect -y -r 2` → 0x48).
3. Ajustar el divisor de CTLI en banco para la corriente/temperatura deseada.
