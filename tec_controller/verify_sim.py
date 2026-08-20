#!/usr/bin/env python3
# tec_controller.cpp logic verification (in the host computer):
#  1) ADS1115 configuration bits
#  2) Measurment chain NTC -> V(ADS, quantized) -> T   (same equation as in .cpp)
#  3) Bang-bang feedback multiplexing simulation (only one driver)
# It is not meant to be executed in BBB; it only demonstrates that the algorithm regulates in the range 15-18 C.
#
# NTCs supplied from REF (1,50 V), PGA of the ADS at +/-2.048 V.
import math

VSUPPLY=1.50; R_TOP=10000.0; R0=10000.0; T0=298.15; BETA=3950.0   # NTC from REF
ADS_FS=2.048; ADS_LSB=ADS_FS/32768.0                              # PGA +/-2.048 V
TEMP_SET=18.0; TEMP_HYST=0.1                                      # fixed setpoint to 18,0 C
TEMP_LOW=TEMP_SET-TEMP_HYST; TEMP_HIGH=TEMP_SET+TEMP_HYST         # 17.9 / 18.1

print("=== 1) ADS1115 configuration per channel (single-ended AINx, +/-2.048V, single, 128SPS) ===")
for ch in range(4):
    cfg = 0x8000 | ((0x4+ch)<<12) | (0x2<<9) | (0x1<<8) | (0x4<<5) | 0x03
    os=(cfg>>15)&1; mux=(cfg>>12)&7; pga=(cfg>>9)&7; mode=(cfg>>8)&1; dr=(cfg>>5)&7
    print(f"  AIN{ch}: cfg=0x{cfg:04X}  OS={os} MUX={mux}(AIN{mux-4}) PGA={pga} MODE={mode} DR={dr}")
    assert os==1 and mux==4+ch and pga==2 and mode==1 and dr==4, "wrong ADS1115 configuration!"
print("  -> OK\n")

def T_to_V(Tc):                         # NTC real: temperature -> voltage at the node (with ADC quantization)
    Tk=Tc+273.15
    R=R0*math.exp(BETA*(1.0/Tk-1.0/T0))
    V=VSUPPLY*R/(R_TOP+R)
    raw=max(-32768,min(32767,round(V/ADS_LSB)))
    return raw*ADS_LSB
def V_to_T(V):                          # identical to volts_to_celsius() in .cpp
    if V<=0 or V>=VSUPPLY: return float('nan')
    R=R_TOP*V/(VSUPPLY-V)
    return 1.0/(1.0/T0+(1.0/BETA)*math.log(R/R0))-273.15

print("=== 2) NTC measurement chain ===")
maxerr=0
for Tc in (10,14,15,16,17,18,20,25,30):
    Tm=V_to_T(T_to_V(Tc)); e=abs(Tm-Tc); maxerr=max(maxerr,e)
    print(f"  T={Tc:5.1f}C -> V={T_to_V(Tc):.4f}V -> Tmeas={Tm:6.2f}C  (err {e:.3f})")
print(f"  -> cuantization max error: {maxerr:.3f} C\n")
assert maxerr<0.25, "imprecise NTC conversion"

print("=== 3) Bang-bang + multiplexing (4 channels, 1 driver, setpoint 18C) ===")
Tamb, Tcold = 22.0, 8.0                 # ambient and cold floor achieved by the TEC
tau_warm, tau_cool = 30.0, 8.0          # Temporal thermal constants (s)
T=[22.0]*4; cooling=[False]*4
tmin=[99.0]*4; tmax=[-99.0]*4
t=0.0
while t<4000.0:
    for ch in range(4):                 # round-robin, as in .cpp
        Tmeas=V_to_T(T_to_V(T[ch]))     # reads the NTC of the channel
        if   Tmeas>TEMP_HIGH: cooling[ch]=True
        elif Tmeas<TEMP_LOW:  cooling[ch]=False
        dt = 0.30 if cooling[ch] else 0.02
        for k in range(4):              # ONLY the channel server can be controlled (1 driver)
            if k==ch and cooling[ch]: T[k]+=(Tcold-T[k])*(dt/tau_cool)
            else:                       T[k]+=(Tamb -T[k])*(dt/tau_warm)
        t+=dt
        if t>300:
            for k in range(4): tmin[k]=min(tmin[k],T[k]); tmax[k]=max(tmax[k],T[k])

ok=True
for k in range(4):
    # setpoint to 18 and hysteresis +/-0,1, el regimen debe quedar ceñido a ~18 C
    inband = 17.0<=tmin[k] and tmax[k]<=18.6
    ok = ok and inband
    print(f"  TEC{k+1}: range {tmin[k]:.2f} .. {tmax[k]:.2f} C   {'OK' if inband else 'OUT'}")
print(f"\n>>> Without control, all to {Tamb:.0f}C (ambient).")
print(f">>> With the current control all 4 channels regulate at ~{TEMP_SET:.1f} C: {'YES' if ok else 'NO'}")
assert ok, "does not regulate"
print("\nALL CHECKS PASSED!")
