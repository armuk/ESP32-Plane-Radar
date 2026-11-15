#!/bin/sh
# Build the JLCPCB flex order files and renders for each extender:
#   <name>/jlcpcb/<name>-gerbers.zip  (Gerbers, drill, stiffener layers User.1/User.2)
#   <name>/jlcpcb/bom.csv             (written by gen_extenders.py)
#   <name>/jlcpcb/cpl.csv             (the socket, top side)
#   <name>/renders/top.png, bottom.png
set -e
cd "$(dirname "$0")"
for NAME in j44-ext j46-ext j100-ext; do
  PCB=$NAME/$NAME.kicad_pcb
  OUT=$NAME/jlcpcb
  G=$OUT/gerbers
  rm -rf "$G" && mkdir -p "$G" "$NAME/renders"
  kicad-cli pcb export gerbers -o "$G" \
    -l F.Cu,B.Cu,F.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts,User.1,User.2 \
    --subtract-soldermask "$PCB" >/dev/null
  kicad-cli pcb export drill -o "$G/" --format excellon --excellon-units mm \
    --excellon-separate-th --generate-map --map-format gerberx2 "$PCB" >/dev/null
  mv "$G/$NAME-User_1.gbr" "$G/$NAME-Stiffener_FR4_0.8mm_bottom.gbr"
  mv "$G/$NAME-User_2.gbr" "$G/$NAME-Stiffener_PI_0.1mm_top.gbr"
  rm -f "$OUT/$NAME-gerbers.zip"
  (cd "$G" && zip -q "../$NAME-gerbers.zip" ./*)

  kicad-cli pcb export pos -o "$OUT/pos-raw.csv" --format csv --units mm --side front --smd-only "$PCB" >/dev/null
  awk -F, 'NR==1 {print "Designator,Mid X,Mid Y,Layer,Rotation"; next}
           {gsub(/"/,""); print $1","$4"mm,"$5"mm,Top,"$6}' "$OUT/pos-raw.csv" > "$OUT/cpl.csv"
  rm "$OUT/pos-raw.csv"

  kicad-cli pcb render -o "$NAME/renders/top.png" --side top --width 2000 --height 500 "$PCB" >/dev/null
  kicad-cli pcb render -o "$NAME/renders/bottom.png" --side bottom --width 2000 --height 500 "$PCB" >/dev/null
  echo "done: $OUT/$NAME-gerbers.zip, $OUT/bom.csv, $OUT/cpl.csv"
done
