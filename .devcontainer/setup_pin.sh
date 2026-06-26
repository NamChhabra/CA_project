#!/bin/bash
# Install dependencies
sudo apt-get update && sudo apt-get install -y wget vim build-essential gcc-multilib g++-multilib

# Download Intel Pin (Update the URL/version as needed)
cd /home/vscode
wget https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.30-98830-g1d7b601b3-gcc-linux.tar.gz
tar -xzf pin-3.30-98830-g1d7b601b3-gcc-linux.tar.gz
mv pin-3.30-98830-g1d7b601b3-gcc-linux pin

# Add Pin to PATH
echo 'export PIN_ROOT=/home/vscode/pin' >> ~/.bashrc
echo 'export PATH=$PATH:$PIN_ROOT' >> ~/.bashrc
