#!/usr/bin/env bash
#
# configuration_setup.sh
# Reconfigura la BeagleBone Black despues de cada arranque.
# La BBB no tiene RTC con pila -> el reloj vuelve al pasado en cada boot,
# y eso hace que git/apt fallen la verificacion TLS ("certificate NOT
# trusted / not yet valid"). Ademas la ruta por defecto y el DNS se pierden.
#
# Este script deja las tres cosas en su sitio:
#   1) ruta por defecto (salida a internet via el NAT del host)
#   2) DNS
#   3) reloj (leyendo la hora de una cabecera HTTP en texto plano, sin cert)
#
# Uso:
#   chmod +x configuration_setup.sh
#   ./configuration_setup.sh        (pedira la contrasena de sudo)

set -u
chmod +x "$0" 2>/dev/null   # se hace ejecutable a si mismo para futuras veces

# ---------- Config (ajusta si cambia tu setup) ----------------------
GATEWAY="192.168.7.1"            # IP del host que hace NAT/ICS
IFACE="usb0"                     # interfaz por la que sale la BBB
DNS="8.8.8.8"                    # servidor DNS
TIME_HOST="http://google.com"    # de aqui se lee la hora (cabecera Date)
# --------------------------------------------------------------------

echo "==== Configuracion de red y reloj de la BBB ===="

# 1) Ruta por defecto ------------------------------------------------
if ip route | grep -q '^default'; then
    echo "[ruta] default ya existe -> $(ip route | awk '/^default/{print $3}')"
else
    echo "[ruta] anadiendo default via $GATEWAY dev $IFACE"
    sudo ip route add default via "$GATEWAY" dev "$IFACE" \
        && echo "[ruta] OK" || echo "[ruta] FALLO al anadir la ruta"
fi

# 2) DNS -------------------------------------------------------------
if grep -q "$DNS" /etc/resolv.conf 2>/dev/null; then
    echo "[dns ] $DNS ya configurado"
else
    echo "[dns ] escribiendo 'nameserver $DNS' en /etc/resolv.conf"
    echo "nameserver $DNS" | sudo tee /etc/resolv.conf >/dev/null
fi

# 3) Conectividad basica ---------------------------------------------
if ping -c1 -W3 "$DNS" >/dev/null 2>&1; then
    echo "[red ] internet OK (ping $DNS)"
else
    echo "[red ] SIN internet -- revisa el NAT del host (VirtualBox)"
    echo "       No tiene sentido seguir con la hora sin red. Saliendo."
    exit 1
fi

# 4) Reloj -----------------------------------------------------------
echo "[hora] antes: $(date)"

# Intento 1 (opcional): systemd-timesyncd via NTP (UDP 123, sin certs).
if command -v timedatectl >/dev/null 2>&1; then
    sudo timedatectl set-ntp true 2>/dev/null
    sleep 3
fi

# Intento 2 (fiable): leer la cabecera HTTP 'Date' por texto plano.
HTTP_DATE=""
if command -v curl >/dev/null 2>&1; then
    HTTP_DATE=$(curl -sI --max-time 10 "$TIME_HOST" 2>/dev/null \
                | awk 'BEGIN{IGNORECASE=1} /^date:/{sub(/^[Dd]ate: /,""); print; exit}')
fi
if [ -z "$HTTP_DATE" ] && command -v wget >/dev/null 2>&1; then
    HTTP_DATE=$(wget -qSO- --max-redirect=0 "$TIME_HOST" 2>&1 \
                | awk 'BEGIN{IGNORECASE=1} /date:/{sub(/.*[Dd]ate: /,""); print; exit}')
fi

if [ -n "$HTTP_DATE" ]; then
    # 'date -s' entiende el formato GMT de la cabecera y fija la hora en UTC.
    sudo date -s "$HTTP_DATE" >/dev/null 2>&1 \
        && echo "[hora] ajustada por HTTP" \
        || echo "[hora] no se pudo aplicar la hora leida ('$HTTP_DATE')"
else
    echo "[hora] no se pudo leer la hora por HTTP (curl/wget?)"
fi
echo "[hora] ahora: $(date)"

# 5) Verificacion final ----------------------------------------------
if getent hosts github.com >/dev/null 2>&1; then
    echo "[test] DNS resuelve github.com OK"
else
    echo "[test] DNS NO resuelve github.com -- revisa /etc/resolv.conf"
fi

echo "==== Listo. Ya puedes usar git y apt ===="
