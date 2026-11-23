#!/bin/sh
# Build the JLCPCB order files from echo-breakout.kicad_pcb:
#   jlcpcb/echo-breakout-gerbers.zip  (Gerbers + drill)
#   jlcpcb/bom.csv                    (written by gen_board.py)
#   jlcpcb/cpl.csv                    (written by finish_board.py)
set -e
cd "$(dirname "$0")"
PCB=echo-breakout.kicad_pcb
OUT=jlcpcb
G=$OUT/gerbers
rm -rf "$G" && mkdir -p "$G"

kicad-cli pcb export gerbers -o "$G" \
  -l F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
  --subtract-soldermask "$PCB"
kicad-cli pcb export drill -o "$G/" --format excellon --excellon-units mm \
  --excellon-separate-th --generate-map --map-format gerberx2 "$PCB"
rm -f "$OUT/echo-breakout-gerbers.zip"
(cd "$G" && zip -q ../echo-breakout-gerbers.zip ./*)

# cpl.csv is written by finish_board.py (it needs the body centre of each through-hole part).
echo "done: $OUT/echo-breakout-gerbers.zip, $OUT/bom.csv, $OUT/cpl.csv"
