#!/bin/sh
# usage: shoot.sh "<TF_KEYS>" "<frames>" quitframe  -> /tmp/claude-501/tf/fN.png (840 wide)
rm -f /tmp/claude-501/tf/*; mkdir -p /tmp/claude-501/tf
cd "$(dirname "$0")/../build/debug"
TF_RANDOM_AT="$TF_RANDOM_AT" TF_PADKEYS="$TF_PADKEYS" TF_VPADS="$TF_VPADS" TF_KEYS="$1" TF_SHOT="$2" TF_SHOT_DIR=/tmp/claude-501/tf TF_QUIT_AT="$3" ./towerfall > run.log 2>&1
echo "exit=$?"
grep -iv "connection\|nscocoa\|linkd\|plugin\|windowtab\|^saved" run.log | tail -8
cd /tmp/claude-501/tf && for f in frame_*.bmp; do n=${f#frame_}; n=${n%.bmp}; sips -s format png $f --out f$n.png >/dev/null 2>&1; sips -Z 840 f$n.png >/dev/null 2>&1; done; ls *.png
