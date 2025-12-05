#!/bin/bash

echo "=== Fixing DPM Boot Parameters ==="
echo "Current Kernel Command Line:"
cat /proc/cmdline
echo ""

if grep -q "amdgpu.dpm=0" /proc/cmdline; then
    echo "WARNING: DPM is explicitly disabled (amdgpu.dpm=0)!"
else
    echo "DPM disable flag not found in current boot, but we will enforce enable."
fi

echo "Adding amdgpu.dpm=1 to /etc/default/grub..."

# Backup
sudo cp /etc/default/grub /etc/default/grub.bak

# Check if amdgpu.dpm is already there
if grep -q "amdgpu.dpm=" /etc/default/grub; then
    sudo sed -i 's/amdgpu.dpm=[0-9]/amdgpu.dpm=1/g' /etc/default/grub
else
    # Append to GRUB_CMDLINE_LINUX_DEFAULT
    # Remove the closing quote, add parameter, add closing quote
    sudo sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="/GRUB_CMDLINE_LINUX_DEFAULT="amdgpu.dpm=1 /' /etc/default/grub
    # Fix potential double quotes if script runs multiple times incorrectly (simple check)
fi

echo "Updating GRUB..."
sudo update-grub

echo "Done! Please reboot to enable DPM."
