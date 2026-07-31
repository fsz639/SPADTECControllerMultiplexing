#!/bin/bash
# Script to launch the automatic Multiplexed TEC system

# Restore terminal settings on exit
trap 'stty sane 2>/dev/null' EXIT

# 1. Prompt for sudo password once up front
sudo -v || exit 1

# 2. Keep sudo timestamp fresh in the background while script runs
( while true; do sudo -n true; sleep 60; kill -0 "$$" || exit; done ) 2>/dev/null &
SUDO_REFRESH_PID=$!

# 3. Clean exit handler for Ctrl+C (SIGINT) or SIGTERM
cleanup() {
    echo -e "\n\n[!] Stopping TEC Controller..."
    
    # Unpause the process first in case it was STOPped
    sudo pkill -CONT -f tec_controller 2>/dev/null
    
    # Gracefully terminate by process name
    sudo pkill -TERM -f tec_controller 2>/dev/null
    sleep 0.2
    
    # Force kill if it's still running
    sudo pkill -KILL -f tec_controller 2>/dev/null

    kill "$SUDO_REFRESH_PID" 2>/dev/null
    stty sane 2>/dev/null
    exit 0
}
trap cleanup SIGINT SIGTERM EXIT

# 4. Set up GPIO pins
echo "[+] Configuring GPIO pins..."
sudo ./config_pins_tec.sh
# Give the BBB pinmux manager time to settle on first boot
sleep 2

# 5. Launch the main C++ controller process in background
echo "[+] Launching TEC Controller..."
sudo ./tec_controller &
sleep 1

# 6. Listen for keypresses to toggle Pause / Resume
PAUSED=0

# Check process existence directly via pgrep
while pgrep -f tec_controller >/dev/null; do
    read -r -s -n 1 -t 1 KEY
    READ_STATUS=$?

    # Catch Ctrl+C manually if terminal is in raw mode (\x03)
    if [[ "$KEY" == $'\x03' ]]; then
        cleanup
    fi

    if [ $READ_STATUS -eq 0 ]; then
        if [ $PAUSED -eq 0 ]; then
            echo -e "\n[PAUSED] Suspending TEC Controller..."
            sudo pkill -STOP -f tec_controller
            PAUSED=1
        else
            echo -e "\n[RESUMED] Continuing TEC Controller..."
            sudo pkill -CONT -f tec_controller
            PAUSED=0
        fi
    fi
done

# Clean up background sudo refresher if process exits on its own
kill "$SUDO_REFRESH_PID" 2>/dev/null
stty sane 2>/dev/null
echo "[+] Controller execution finished."
