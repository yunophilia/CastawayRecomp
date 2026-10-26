#!/bin/sh
# Runs the Linux build headless for at most <seconds> (Xvfb; lavapipe unless VK_ICD_FILENAMES
# is set), from out/linux, with the given game arguments. The time limit applies to the game
# itself, inside xvfb-run, so a run never outlives it (xvfb-run then stops its X server).
#   sh tools/run_test_linux.sh 300 --nobox --skip-launcher --mute --scale 1 \
#       --data ../../game/R42E69 --nand /tmp/castaway_nand --script ../../tools/scripts/beach_move.txt
secs=$1
shift
cd "$(dirname "$0")/../out/linux" || exit 1
unset WAYLAND_DISPLAY
export DISPLAY=
export SDL_AUDIO_DRIVER="${SDL_AUDIO_DRIVER:-dummy}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-warn}"
xvfb-run -a -s "-screen 0 1280x720x24" timeout -s KILL "$secs" ./CastawayRecomp "$@"
status=$?
case $status in
    124 | 137) echo "stopped after $secs s" ;;
    *) echo "exited with code $status" ;;
esac
