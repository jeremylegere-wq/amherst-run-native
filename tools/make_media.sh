#!/bin/sh
# Makes the screenshots and GIFs in docs/media from the game itself (run from the repository root after a build).
# Needs ffmpeg (a tool for the docs only, not a dependency of the game).
#
#   tools/make_media.sh [build/amherst-run]
set -e
GAME="${1:-build/amherst-run}"
OUT=docs/media
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
R="$GAME --no-save --no-train"

gif() {  # gif FRAMES_DIR OUT.gif FIRST COUNT: 15 fps, scaled 2x with chunky pixels, one palette per clip
    ffmpeg -loglevel error -y -framerate 15 -start_number "$3" -i "$1/frame_%05d.bmp" -frames:v "$4" \
        -vf "scale=640:360:flags=neighbor,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
        -loop 0 "$2"
}
still() {  # still NAME OPTIONS...: one 320x180 picture, scaled 3x
    name="$1"; shift
    $R "$@" --screenshot "$TMP/$name.bmp" > /dev/null
    ffmpeg -loglevel error -y -i "$TMP/$name.bmp" -vf scale=960:540:flags=neighbor "$OUT/$name.png"
}

mkdir -p "$TMP/cruise" "$TMP/cover" "$TMP/piece"
$R --players 1 --at 470,-793 --heading 275 --autopilot --pace 1.3 --frames 300 --record "$TMP/cruise" > /dev/null
gif "$TMP/cruise" "$OUT/cruise.gif" 12 135
$R --players 1 --no-cops --traffic 4 --at 504.7,-946.8 --heading 241 --tag-test --frames 80 --record "$TMP/cover" > /dev/null
gif "$TMP/cover" "$OUT/tag-cover.gif" 0 40
$R --players 1 --no-cops --traffic 0 --at 441.4,-789.7 --heading 52 --tag-test --frames 130 --record "$TMP/piece" > /dev/null
gif "$TMP/piece" "$OUT/big-piece.gif" 0 65

still start-screen --menu --frames 40
still split-screen --players 4 --at 391,-850 --heading 190 --frames 45
still houses --players 1 --no-cops --traffic 0 --at 732.3,-1082.9 --heading 227 --frames 2
still mural --players 1 --no-cops --traffic 0 --at 409.9,-1003.4 --heading 205 --frames 2
still touch --players 1 --touch --at 391,-850 --heading 190 --frames 30
still golden-hour --players 1 --at 391,-850 --heading 190 --weather golden --frames 30

for w in clear overcast fog golden snow; do
    $R --players 1 --no-cops --traffic 0 --at 732.3,-1082.9 --heading 227 --weather $w --frames 20 --screenshot "$TMP/w_$w.bmp" > /dev/null
done
$R --players 1 --at 391,-850 --heading 190 --weather snow --frames 30 --screenshot "$TMP/w_snow2.bmp" > /dev/null
ffmpeg -loglevel error -y -i "$TMP/w_clear.bmp" -i "$TMP/w_overcast.bmp" -i "$TMP/w_fog.bmp" -i "$TMP/w_golden.bmp" \
    -i "$TMP/w_snow.bmp" -i "$TMP/w_snow2.bmp" \
    -filter_complex "[0][1][2]hstack=3[top];[3][4][5]hstack=3[bot];[top][bot]vstack,scale=1920:720:flags=neighbor" "$OUT/weather.png"
ls -l "$OUT"
