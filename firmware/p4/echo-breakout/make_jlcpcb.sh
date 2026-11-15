#!/bin/sh
# Build the JLCPCB order files from echo-breakout.kicad_pcb:
#   jlcpcb/echo-breakout-gerbers.zip  (Gerbers + drill)
#   jlcpcb/bom.csv                    (written by gen_board.py)
#   jlcpcb/cpl.csv                    (placement, SMD parts on the top side)
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

kicad-cli pcb export pos -o "$OUT/pos-raw.csv" --format csv --units mm --side front --smd-only "$PCB"
# KiCad: Ref,Val,Package,PosX,PosY,Rot,Side  ->  JLCPCB: Designator,Mid X,Mid Y,Layer,Rotation
awk -F, 'NR==1 {print "Designator,Mid X,Mid Y,Layer,Rotation"; next}
         {gsub(/"/,""); if ($1 ~ /^JP/) next; print $1","$4"mm,"$5"mm,Top,"$6}' \
  "$OUT/pos-raw.csv" > "$OUT/cpl.csv"
rm "$OUT/pos-raw.csv"
echo "done: $OUT/echo-breakout-gerbers.zip, $OUT/bom.csv, $OUT/cpl.csv"
