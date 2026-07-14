# Checklist de banco — control TEC (imprimir y marcar)

## ⚠️ Dos cosas críticas antes de empezar
- [ ] **SHDN (On/Off) del MAX1968 con 3,3 V:** verificar en el datasheet que **V_IH ≤ 3,3 V** (que la BBB pueda habilitarlo). Si necesitara ~5 V → poner **level shifter 3,3→5 V**.
- [ ] **Sentido cool/heat:** con resistencias solo verás corriente y su dirección; que 1,3 V **enfríe** (y no caliente) solo se confirma con un Peltier. Si calienta → consigna a **1,7 V**.
- [x] *R9 ya cambiada a **pull-down** (SHDN reposo = deshabilitado, seguro).* 

## A) Antes (en la VM / software)
- [ ] `make` compila sin errores (si falla, avisar).
- [ ] `python3 verify_sim.py` pasa (config ADS + conversión NTC + regulación).
- [ ] BBB: `ls /sys/class/gpio` existe (si no → libgpiod).
- [ ] BBB: `i2cdetect -l` → apuntar qué `/dev/i2c-N` es P9_19/20. Ajustar `I2C_BUS` si no es i2c-2.
- [ ] BBB: `i2cdetect -y -r N` → el **ADS1115 aparece en 0x48**.

## B) Montaje del banco
- [ ] **4× R_load ~2 Ω / ≥10 W** en J_TEC1-4 (en vez de los Peltier). *(No usar >2,5 Ω: el driver se satura.)*
- [ ] **NTC → resistencias/pot** en J_NTC1-4: 10k=25 °C · 15,8k=15 °C · 13,7k=18 °C.
- [ ] BBB ↔ J_BBB: SEL1-4 (P8_12/11/16/15), SHDN (P8_14), I²C (P9_19/20).
- [ ] **MASA COMÚN**: BBB + placa + fuente 5 V + fuente 3,3 V, todas unidas.
- [ ] Circuito del MAX1968 completo (L1/L2, condensadores, R-sense, FREQ→GND).

## C) Encendido (en este orden)
- [ ] 1º **3,3 V** (lógica) y la BBB. Lanzar `sudo ./config_pins_tec.sh` y `sudo ./tec_controller test`.
- [ ] 2º **5 V** (TEC) — solo después de que el firmware tenga SHDN en off.

## D) Fase 0 — tensiones de referencia
- [ ] Raíles 5 V y 3,3 V correctos.
- [ ] **REF = 1,50 V** (y no se hunde bajo carga).
- [ ] **CTLI ≈ 1,30 V** (empezar cerca de 1,5 V con poca corriente, luego bajar).

## E) Fase 1 — un canal (en modo `test`)
- [ ] `1` selecciona canal 1 → `o` da corriente. Medir **V en R_load1 → I = V/R**.
- [ ] Contrastar con el pin **ITEC** (∝ corriente). Corriente **estable** y **< límite** (~2 A).
- [ ] `f` → la corriente cae a 0. `x` → reposo.

## F) Fase 2 — multiplexado
- [ ] `2`,`3`,`4` → cada current switch enruta a su R_load (solo uno a la vez).
- [ ] `sudo ./tec_controller` (lazo abierto): osciloscopio en SEL/SHDN → **1 canal activo** y **SHDN cae al conmutar** (corriente a 0).

## G) Fase 3 — lectura NTC
- [ ] `test` → `r`: las 4 "temperaturas" coinciden con las R_test (±0,5 °C).

## H) Fase 4 — regulación (futuro, opcional)
- [ ] `sudo ./tec_controller regulate` + potenciómetro como NTC: SHDN activa >18 °C y para <15 °C.

---
*Empezar siempre con poca corriente. Ante cualquier medida rara, cortar (SHDN off / `x`) y revisar.*
