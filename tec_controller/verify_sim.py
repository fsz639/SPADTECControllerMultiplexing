#!/usr/bin/env python3
# Verificacion (en el HOST) de la logica de tec_controller.cpp:
#  1) bits de configuracion del ADS1115
#  2) cadena de medida NTC -> V(ADS, cuantizado) -> T   (misma formula que el .cpp)
#  3) simulacion del lazo bang-bang con multiplexado real (1 solo driver)
# No se ejecuta en la BBB; solo demuestra que el algoritmo regula a 15-18 C.
#
# NTC alimentadas desde REF (1,50 V), PGA del ADS a +/-2.048 V.   <-- 2026-07-21
import math

VSUPPLY=1.50; R_TOP=10000.0; R0=10000.0; T0=298.15; BETA=3950.0   # NTC desde REF
ADS_FS=2.048; ADS_LSB=ADS_FS/32768.0                              # PGA +/-2.048 V
TEMP_LOW=15.0; TEMP_HIGH=18.0

print("=== 1) Config del ADS1115 por canal (debe ser single-ended AINx, +/-2.048V, single, 128SPS) ===")
for ch in range(4):
    cfg = 0x8000 | ((0x4+ch)<<12) | (0x2<<9) | (0x1<<8) | (0x4<<5) | 0x03
    os=(cfg>>15)&1; mux=(cfg>>12)&7; pga=(cfg>>9)&7; mode=(cfg>>8)&1; dr=(cfg>>5)&7
    print(f"  AIN{ch}: cfg=0x{cfg:04X}  OS={os} MUX={mux}(AIN{mux-4}) PGA={pga} MODE={mode} DR={dr}")
    assert os==1 and mux==4+ch and pga==2 and mode==1 and dr==4, "config ADS1115 mal"
print("  -> OK\n")

def T_to_V(Tc):                         # NTC real: temperatura -> tension del nodo (con cuantizacion del ADC)
    Tk=Tc+273.15
    R=R0*math.exp(BETA*(1.0/Tk-1.0/T0))
    V=VSUPPLY*R/(R_TOP+R)
    raw=max(-32768,min(32767,round(V/ADS_LSB)))
    return raw*ADS_LSB
def V_to_T(V):                          # identica a volts_to_celsius() del .cpp
    if V<=0 or V>=VSUPPLY: return float('nan')
    R=R_TOP*V/(VSUPPLY-V)
    return 1.0/(1.0/T0+(1.0/BETA)*math.log(R/R0))-273.15

print("=== 2) Cadena de medida NTC (ida y vuelta) ===")
maxerr=0
for Tc in (10,14,15,16,17,18,20,25,30):
    Tm=V_to_T(T_to_V(Tc)); e=abs(Tm-Tc); maxerr=max(maxerr,e)
    print(f"  T={Tc:5.1f}C -> V={T_to_V(Tc):.4f}V -> Tmedida={Tm:6.2f}C  (err {e:.3f})")
print(f"  -> error max por cuantizacion: {maxerr:.3f} C\n")
assert maxerr<0.25, "conversion NTC imprecisa"

print("=== 3) Lazo bang-bang + multiplexado (4 canales, 1 driver, histeresis 15/18) ===")
Tamb, Tcold = 22.0, 8.0                 # ambiente y suelo frio que alcanza el TEC
tau_warm, tau_cool = 30.0, 8.0          # constantes termicas (s)
T=[22.0]*4; cooling=[False]*4
tmin=[99.0]*4; tmax=[-99.0]*4
t=0.0
while t<4000.0:
    for ch in range(4):                 # round-robin, exactamente como el .cpp
        Tmeas=V_to_T(T_to_V(T[ch]))     # lee la NTC del canal
        if   Tmeas>TEMP_HIGH: cooling[ch]=True
        elif Tmeas<TEMP_LOW:  cooling[ch]=False
        dt = 0.30 if cooling[ch] else 0.02
        for k in range(4):              # SOLO el canal servido puede enfriarse (1 driver)
            if k==ch and cooling[ch]: T[k]+=(Tcold-T[k])*(dt/tau_cool)
            else:                       T[k]+=(Tamb -T[k])*(dt/tau_warm)
        t+=dt
        if t>300:
            for k in range(4): tmin[k]=min(tmin[k],T[k]); tmax[k]=max(tmax[k],T[k])

ok=True
for k in range(4):
    inband = 14.0<=tmin[k] and tmax[k]<=19.0
    ok = ok and inband
    print(f"  TEC{k+1}: regimen {tmin[k]:.2f} .. {tmax[k]:.2f} C   {'OK' if inband else 'FUERA'}")
print(f"\n>>> Sin control (lazo abierto) todos irian a {Tamb:.0f}C (ambiente).")
print(f">>> Con este control REGULAN los 4 en ~15-18 C: {'SI' if ok else 'NO'}")
assert ok, "no regula"
print("\nTODAS LAS COMPROBACIONES PASAN.")
