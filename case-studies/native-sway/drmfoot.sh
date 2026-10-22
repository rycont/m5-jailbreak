#!/bin/sh
export TERM=xterm-256color
while :; do
  printf "\033[2J\033[H"
  echo "NATIVE SWAY"
  echo "DRM/KMS 1920x1080"
  date
  sleep 1
done
