#!/bin/bash

echo "=== AMD GPU Monitor (Gladius) ==="
echo "Monitoring SCLK (Shader Clock) and MCLK (Memory Clock)..."
echo "Press Ctrl+C to exit."
echo ""

while true; do
    # Try to read clocks from sysfs (if DPM is enabled)
    SCLK=$(cat /sys/class/drm/card1/device/pp_dpm_sclk 2>/dev/null | grep "*" | awk '{print $2}')
    MCLK=$(cat /sys/class/drm/card1/device/pp_dpm_mclk 2>/dev/null | grep "*" | awk '{print $2}')
    
    # Try debugfs if sysfs fails
    if [ -z "$SCLK" ]; then
        SCLK=$(cat /sys/kernel/debug/dri/1/amdgpu_pm_info 2>/dev/null | grep "sclk" | awk '{print $2 " " $3}')
    fi
    if [ -z "$MCLK" ]; then
        MCLK=$(cat /sys/kernel/debug/dri/1/amdgpu_pm_info 2>/dev/null | grep "mclk" | awk '{print $2 " " $3}')
    fi
    
    # Read Load
    LOAD=$(cat /sys/class/drm/card1/device/gpu_busy_percent 2>/dev/null)
    
    # Read Temp
    TEMP=$(cat /sys/class/drm/card1/device/hwmon/hwmon*/temp1_input 2>/dev/null)
    if [ ! -z "$TEMP" ]; then
        TEMP=$(($TEMP/1000))
    fi

    echo -ne "SCLK: ${SCLK:-N/A} | MCLK: ${MCLK:-N/A} | Load: ${LOAD:-0}% | Temp: ${TEMP:-N/A}C \r"
    sleep 1
done
