#!/bin/bash
PKG=$(echo "$1" | sed "s/,/ /g")
sudo apt-get update
sudo apt-get install -y $PKG
sudo apt-get clean

