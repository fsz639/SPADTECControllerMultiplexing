# Hoja de conexiones — control TEC 4 canales (protoboard, lazo abierto)

Montaje de banco con **4 R_load en lugar de las 4 Peltier**. Un único **MAX1968**
(corriente fija, CTLI ≈ 1,30 V) se reparte entre 4 canales con 4 current switches
**G3VM-21HR**. Las 4 NTC (aquí resistencias de test) se leen por I²C con un módulo
**ADS1115**. Control en la **BeagleBone Black** (GPIO por sysfs + I²C `/dev/i2c-2`).

Pinout del MAX1968 = extraído del símbolo KiCad verificado (Driver:MAX1968xUI).

---

## 0. Alimentación y masa (LO PRIMERO)

| Raíl | Origen | Alimenta | Corriente |
|---|---|---|---|
| **+5 V (potencia)** | **Fuente externa** (TRACO/laboratorio) | MAX1968 (VDD/PVDD) + carga TEC | ≥ 3 A |
| **+3V3 (lógica)** | BeagleBone `P9_3` / `P9_4` (VDD_3V3) | ADS1115 + divisores NTC + pull-ups | < 50 mA |
| **GND (común)** | unir TODAS las masas | todo | — |

> ⚠️ **NO** uses el 5 V de la BBB (`P9_5/6`) para el TEC: solo da ~250 mA.
> ⚠️ **Masa común obligatoria**: GND de la fuente 5 V + GND de la BBB (`P9_1/2`) +
> GND del raíl 3V3, todo unido en un mismo nodo de la protoboard.

**Orden de encendido:** 1º 3V3 + BBB (lanzar firmware con SHDN en off) → 2º 5 V.

---

## 1. Puertos de la BeagleBone Black (los que usamos)

| Señal | Pin cabecera | GPIO / bus | `config-pin` | Va a |
|---|---|---|---|---|
| SEL1 | **P8_12** | gpio44 | `gpio` | R12 (330 Ω) → G3VM ch1 pin1 |
| SEL2 | **P8_11** | gpio45 | `gpio` | R13 (330 Ω) → G3VM ch2 pin1 |
| SEL3 | **P8_16** | gpio46 | `gpio` | R14 (330 Ω) → G3VM ch3 pin1 |
| SEL4 | **P8_15** | gpio47 | `gpio` | R15 (330 Ω) → G3VM ch4 pin1 |
| SHDN | **P8_14** | gpio26 | `gpio` | nodo SHDN (MAX1968 pin17 + R9) |
| I²C2 SCL | **P9_19** | i2c-2 | `i2c` | ADS1115 SCL (+ pull-up R10) |
| I²C2 SDA | **P9_20** | i2c-2 | `i2c` | ADS1115 SDA (+ pull-up R11) |
| +3V3 | **P9_3 / P9_4** | VDD_3V3 | — | raíl 3V3 |
| GND | **P9_1 / P9_2** | DGND | — | masa común |

Comandos (ya en `config_pins_tec.sh`):
`config-pin P8_12/P8_11/P8_16/P8_15/P8_14 gpio` · `config-pin P9_19/P9_20 i2c`

---

## 2. MAX1968 — TODOS los pines (HTSSOP-28 + pad, 1..29)

| Pin | Nombre | Conexión |
|---:|---|---|
| 1 | VDD | **+5 V** |
| 2 | GND | **GND** |
| 3 | CTLI | nodo **CTLI** (unión R16/R17 + C7) → ≈ 1,30 V |
| 4 | REF | nodo **REF** (+ C5 1 µF a GND). Salida 1,50 V = origen de los divisores |
| 5 | PGND | **GND** |
| 6 | LX2 | nodo **LX2** (→ L2). *Unir 6-8-10* |
| 7 | PGND | **GND** |
| 8 | LX2 | nodo **LX2** (*unir con 6 y 10*) |
| 9 | PVDD2 | **+5 V** |
| 10 | LX2 | nodo **LX2** (*unir con 6 y 8*) |
| 11 | PVDD2 | **+5 V** |
| 12 | FREQ | **GND** (frecuencia interna por defecto) |
| 13 | ITEC | **libre** (lazo abierto). Opcional: monitor de corriente (∝ I_TEC) |
| 14 | OS2 | nodo **TEC_RET** |
| 15 | OS1 | nodo **OS1N** |
| 16 | CS | nodo **TEC_DRV** |
| 17 | ~SHDN (activo bajo) | nodo **SHDN** (R9 100k a GND + BBB P8_14) |
| 18 | PVDD1 | **+5 V** |
| 19 | LX1 | nodo **LX1** (→ L1). *Unir 19-21-23* |
| 20 | PVDD1 | **+5 V** |
| 21 | LX1 | nodo **LX1** (*unir con 19 y 23*) |
| 22 | PGND | **GND** |
| 23 | LX1 | nodo **LX1** (*unir con 19 y 21*) |
| 24 | PGND | **GND** |
| 25 | COMP | **R2 100k** en serie con **C6 100nF** a GND |
| 26 | MAXIN | nodo **MAXIN** (unión R7/R8) |
| 27 | MAXIP | nodo **MAXIP** (unión R5/R6) |
| 28 | MAXV | nodo **MAXV** (unión R3/R4) |
| 29 | GND (pad térmico, EP) | **GND** — soldar el pad a masa |

> Los pines repetidos (LX1 ×3, LX2 ×3, PVDD1 ×2, PVDD2 ×2, PGND ×4, GND ×2)
> **hay que puentearlos** en el adaptador DIP. Un condensador 100 nF pegado a cada
> par PVDD-PGND mejora mucho la estabilidad del conmutado.

---

## 3. Etapa de potencia (filtro LC + R-sense)

```
 LX1(19,21,23) ── L1 3.3µH ──┬── OS1(15)            OS1N
                             ├── C1 1µF ── GND
                             └── R1 0.05Ω ──┬── CS(16)         TEC_DRV  (bus a los 4 G3VM)
                                            
 LX2(6,8,10) ── L2 3.3µH ──┬── OS2(14)               TEC_RET (bus, retorno de las 4 R_load)
                           └── C2 1µF ── GND

 +5V ──┬── C3 10µF ── GND
       └── C4 1µF  ── GND     (desacoplo, pegados a los PVDD)
```

- **OS1N** = L1 · OS1(15) · C1(+) · R1(lado 1)
- **TEC_DRV** = R1(lado 2) · CS(16) · G3VM pin3 (×4)
- **TEC_RET** = L2 · OS2(14) · C2(+) · R_load retorno (×4)

R1 (sense) = 0,05 Ω **de potencia y baja inductancia** (≥ 3 W; vale poner 4× 0,2 Ω
1206/1 W en paralelo). El sentido de medida: CS(16) mide el lado TEC_DRV, OS1(15) el
lado OS1N; la corriente va OS1N → R1 → TEC_DRV → carga → TEC_RET.

---

## 4. Divisores de referencia (todos desde REF = pin 4, 1,50 V)

| Nodo | Pin MAX | Divisor | Tensión | Nota |
|---|---|---|---|---|
| CTLI | 3 | **R16 1,5k** (de REF) / **R17 10k** (a GND) + **C7 100nF** | **1,30 V** | consigna de corriente |
| MAXV | 28 | R3 10k / R4 10k | 0,75 V | límite de tensión TEC |
| MAXIP | 27 | R5 10k / R6 20k | 1,00 V | límite corriente (+) |
| MAXIN | 26 | R7 10k / R8 20k | 1,00 V | límite corriente (−) |
| COMP | 25 | R2 100k + C6 100nF (serie a GND) | — | compensación lazo |
| SHDN | 17 | **R9 100k a GND** (pull-down) + BBB P8_14 | 0 V reposo | arranca deshabilitado |
| REF | 4 | C5 1µF a GND | 1,50 V | desacoplo |

> ⚠️ **A CONFIRMAR EN DATASHEET antes de dar corriente:** los pines MAXIP/MAXIN
> fijan el límite de corriente respecto a REF (1,5 V). Verificar la fórmula
> (`I_max = (V_MAXIP − 1,5V) / (Gain · R_SENSE)`): si MAXIP debe quedar **por encima**
> de 1,5 V, su divisor NO puede salir de REF→GND, sino de +5 V→GND. Comprobarlo o el
> driver podría limitar la corriente casi a cero en el banco.

---

## 5. Los 4 current switches G3VM-21HR (U2..U5)

Cada canal k (U2=ch1, U3=ch2, U4=ch3, U5=ch4):

| Pin G3VM | Conexión |
|---:|---|
| 1 (LED +) | **SELk** desde la BBB, **con Rk = 330 Ω en serie** (R12..R15) |
| 2 (LED −) | **GND** |
| 3 (salida) | **TEC_DRV** (bus común, los 4 juntos) |
| 4 (salida) | **TECk+** → un extremo de **R_load k** |

- R_load k (2 Ω / ≥10 W) entre **TECk+** y **TEC_RET**.
- **No superar 2,5 Ω** por carga (el driver se satura).
- Solo un SELk activo a la vez → un solo canal conduce. El firmware corta SHDN
  antes de cambiar de canal (conmutación a corriente cero).

---

## 6. Sensado: módulo ADS1115 + 4 divisores NTC

**Módulo ADS1115** (usar breakout, no el chip suelto):

| Pin módulo | Conexión |
|---|---|
| VDD | +3V3 |
| GND | GND |
| SCL | BBB P9_19 (+ R10 4,7k a 3V3) |
| SDA | BBB P9_20 (+ R11 4,7k a 3V3) |
| ADDR | **GND** → dirección **0x48** |
| ALRT | libre |
| A0 | nodo NTC_1 |
| A1 | nodo NTC_2 |
| A2 | nodo NTC_3 |
| A3 | nodo NTC_4 |

> Pull-ups R10/R11: **omitir si el módulo ya los trae** (la mayoría sí).

**Divisor NTC por canal** (k = 1..4):

```
 +3V3 ── Rn_k 10k ──┬── A(k-1) del ADS1115      (nodo NTC_k)
                    └── Rtest_k ── GND
```

Resistencias de test (en vez de NTC): **10k = 25 °C · 15,8k = 15 °C · 13,7k = 18 °C**
(o un potenciómetro para barrer temperatura).

---

## 7. Lista de materiales (valores)

| Ref | Valor | Uso |
|---|---|---|
| L1, L2 | 3,3 µH (≥ 3 A) | filtro de salida |
| R1 | 0,05 Ω (≥ 3 W) | sense de corriente |
| C1, C2 | 1 µF | filtro salida |
| C3 | 10 µF | desacoplo +5 V |
| C4 | 1 µF | desacoplo +5 V |
| C5 | 1 µF | REF |
| C6 | 100 nF | COMP |
| C7 | 100 nF | CTLI |
| C8 | 100 nF | ADS1115 |
| R2 | 100 k | COMP |
| R3, R4 | 10 k / 10 k | MAXV |
| R5, R6 | 10 k / 20 k | MAXIP |
| R7, R8 | 10 k / 20 k | MAXIN |
| R9 | 100 k | pull-down SHDN |
| R10, R11 | 4,7 k | pull-up I²C (opc.) |
| R12–R15 | 330 Ω | serie LED G3VM |
| R16 | 1,5 k | CTLI (arriba) |
| R17 | 10 k | CTLI (abajo) |
| Rn1–Rn4 | 10 k | divisor NTC |
| R_load1–4 | 2 Ω / ≥10 W | sustituto Peltier |
| Rtest1–4 | 10k/15,8k/13,7k | sustituto NTC |

---

## 8. Reglas de montaje limpio (protoboard)

1. **MAX1968 en adaptador HTSSOP-28→DIP**; puentear LX1(19/21/23), LX2(6/8/10),
   PVDD, PGND y el pad 29→GND. La etapa conmutada (MAX+L+C+R-sense) mejor **soldada
   en el adaptador o placa perforada**, hilos cortos; el resto en protoboard.
2. **C3/C4** pegados a los pines PVDD; **C1/C2** a la salida; **C5** junto a REF.
   Lazos de corriente lo más pequeños posible.
3. **Masa común** en un único nodo (estrella si puedes): fuente 5 V, BBB, raíl 3V3.
4. Empezar con **1 solo canal** (R_load1) y poca corriente; medir antes de replicar.
5. SHDN arranca en 0 (deshabilitado) por el firmware y el pull-down R9.

## 9. Puntos de medida

| # | Dónde | Valor esperado |
|---|---|---|
| ① | pin 4 (REF) a GND | 1,50 V, estable bajo carga |
| ② | pin 3 (CTLI) a GND | ≈ 1,30 V |
| ③ | en cada R_load | V → **I = V / R** (≈ estable, < límite) |
| ④ | pin 13 (ITEC) a GND | ∝ corriente (contraste con ③) |
