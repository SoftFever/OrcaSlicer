#!/bin/bash
# Update and upgrade all system packages
apt update
apt upgrade -y          

echo "-----------------------------------------"	
echo "Running BuildLinux.sh with update flag..."
echo "-----------------------------------------"	
./BuildLinux.sh -u

echo "------------------------------"
echo "Installing missing packages..."
echo "------------------------------"
apt install -y libgl1-mesa-dev m4 autoconf libtool