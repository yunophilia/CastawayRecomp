#!/bin/sh
# Runs a TAS script (docs/tas.md) headless on the Linux build (tools/run_test_linux.sh: Xvfb,
# lavapipe unless VK_ICD_FILENAMES is set) with an empty save folder, and collects the log,
# telemetry and screenshots in out/linux/tas/<tag>.log / .csv / shots/<tag>/.
#   sh tools/tas_run_linux.sh tools/tas/e2e.tas [seconds] [tag]
# Prints "<tag> : OK" when the script ran to its end with every check passed; exit code 0
# then, else the game's (1 a check failed, 3 an "until" timed out) or 2 if the run timed out.
script=$(realpath "$1") || exit 1
secs=${2:-1200}
tag=${3:-$(basename "$1" .tas)}
root=$(realpath "$(dirname "$0")/..")
out=$root/out/linux
mkdir -p "$out/tas"
nand=$(mktemp -d)
rm -rf "$out/shots"
rm -f "$out/CastawayRecomp.log"   # a run that never starts must not report an old log
sh "$root/tools/run_test_linux.sh" "$secs" --nobox --skip-launcher --mute --scale 2 \
    --data "$root/game/R42E69" --nand "$nand" --tas "$script" --telemetry "$out/tas/$tag.csv" > /dev/null
rm -rf "$nand"
cp "$out/CastawayRecomp.log" "$out/tas/$tag.log" 2>/dev/null || : > "$out/tas/$tag.log"
if [ -d "$out/shots" ]; then
    rm -rf "$out/tas/shots/$tag"
    mkdir -p "$out/tas/shots"
    mv "$out/shots" "$out/tas/shots/$tag"
fi
log=$out/tas/$tag.log
code=$(sed -n 's/.*TAS: .* quit (\([0-9]*\)).*/\1/p' "$log" | tail -1)
fails=$(grep -E "TAS: FAIL|^\[E |FATAL" "$log")
if [ -z "$code" ]; then result="TIMEOUT after $secs s"; status=2
elif [ "$code" = 0 ] && [ -z "$fails" ]; then result=OK; status=0
else result="FAILED (exit $code)"; status=${code:-1}; [ "$status" = 0 ] && status=1
fi
echo "$tag : $result"
grep -o "TAS: [0-9]* passed.*" "$log" | tail -1 | sed 's/^TAS: /  /'
[ -n "$fails" ] && echo "$fails" | head -5 | sed 's/^/  /'
exit $status
