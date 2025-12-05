#!/bin/bash

echo "=== AMD GPU Monitor (Gladius) ==="
echo "Monitoring SCLK (Shader Clock) and MCLK (Memory Clock)..."
echo "Press Ctrl+C to exit."
echo ""

if grep -q "amdgpu.dpm=0" /proc/cmdline; then
    echo "⚠️  WARNING: DPM is DISABLED in kernel parameters (amdgpu.dpm=0)!"
    echo "   Frequencies will be stuck at default (low) values."
    echo "   Run 'sudo ./fix_dpm_boot.sh' and reboot to fix this."
    echo ""
fi

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

    printf "SCLK: %-8s | MCLK: %-8s | Load: %-3s%% | Temp: %-3sC \r" "${SCLK:-N/A}" "${MCLK:-N/A}" "${LOAD:-0}" "${TEMP:-N/A}"
    sleep 1
done
