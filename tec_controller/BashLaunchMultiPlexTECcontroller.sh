#!/bin/bash
# Script to launch the automatic Multiplexed TEC system

# 1. Prompt for sudo password once up front
sudo -v || exit 1

# 2. Keep sudo timestamp fresh in the background while script runs
( while true; do sudo -n true; sleep 60; kill -0 "$$" || exit; done ) 2>/dev/null &
SUDO_REFRESH_PID=$!

# 3. Clean exit handler for Ctrl+C (SIGINT) or SIGTERM
cleanup() {
    echo -e "\n\n[!] Stopping TEC Controller..."
    if [ -n "$APP_PID" ]; then
        sudo kill -SIGINT "$APP_PID" 2>/dev/null
    fi
    kill "$SUDO_REFRESH_PID" 2>/dev/null
    exit 0
}
trap cleanup SIGINT SIGTERM

# 4. Set up GPIO pins
echo "[+] Configuring GPIO pins..."
sudo ./config_pins_tec.sh

# 5. Launch the main C++ controller process in the background (&)
echo "[+] Launching TEC Controller..."
sudo ./tec_controller &
APP_PID=$!

echo "----------------------------------------"
echo "Application launched with PID: $APP_PID"
echo "Press:"
echo "  Ctrl+C        : Terminate system"
echo "  Any other key : Pause / Resume execution"
echo "----------------------------------------"

# 6. Listen for keypresses to toggle Pause / Resume
PAUSED=0
while kill -0 "$APP_PID" 2>/dev/null; do
    # Read 1 character with a 1-second timeout to keep checking if process died
    read -r -n 1 -t 1 KEY
    if [ $? -eq 0 ]; then
        if [ $PAUSED -eq 0 ]; then
            echo -e "\n[PAUSED] Suspending process PID: $APP_PID..."
            sudo kill -STOP "$APP_PID"
            PAUSED=1
        else
            echo -e "\n[RESUMED] Continuing process PID: $APP_PID..."
            sudo kill -CONT "$APP_PID"
            PAUSED=0
        fi
    fi
done

# Clean up background sudo refresher if process exits on its own
kill "$SUDO_REFRESH_PID" 2>/dev/null
echo "[+] Controller execution finished."
