#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROJECT="$(cd "$(dirname "$0")" && pwd)"
OUT="${PROJECT}/build"
WORK="${OUT}/.work"
PARSER="${QCC_PARSER:-${ROOT}/build/qcir}"
BACKEND="${QCC_BACKEND:-${ROOT}/build/qir68k}"
MERGE="${QCC_MERGE:-${ROOT}/tools/qcc_merge.py}"
MWOS_TMP="${MWOS_TMP:-/Volumes/SSD1TB/projects/MWOS/TMP}"
WINE_APP="${WINE_APP:-$HOME/.local/wine-stable/Wine Stable.app/Contents/Resources/wine/bin/wine}"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
MWOS_WIN_ROOT="${MWOS_WIN_ROOT:-Z:\\Volumes\\SSD1TB\\projects\\MWOS}"

NO_LINK=0
OUTPUT_NAME=""
while (($# > 0)); do
    case "$1" in
        --no-link) NO_LINK=1 ;;
        -o|--output)
            (($# >= 2)) || { echo "Option $1 braucht einen Namen" >&2; exit 2; }
            OUTPUT_NAME="$2"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--no-link] [-o NAME|--output NAME]"
            exit 0
            ;;
        *) echo "Unbekannte Option: $1" >&2; exit 2 ;;
    esac
    shift
done

[[ -x "$PARSER" ]] || { echo "qcir fehlt: $PARSER" >&2; exit 2; }
[[ -x "$BACKEND" ]] || { echo "qir68k fehlt: $BACKEND" >&2; exit 2; }
command -v python3 >/dev/null || { echo "python3 fehlt" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"

sources=("${PROJECT}"/*.tc)
first_source="${sources[0]}"
first_base="$(basename "$first_source" .tc)"
if [[ -z "$OUTPUT_NAME" ]]; then
    OUTPUT_NAME="$first_base"
    suffix=0
    while [[ -e "${OUT}/${OUTPUT_NAME}.out" ]]; do
        suffix=$((suffix + 1))
        OUTPUT_NAME="${first_base}.${suffix}"
    done
fi
[[ "$OUTPUT_NAME" != */* ]] || { echo "Modulname darf kein Verzeichnis enthalten: $OUTPUT_NAME" >&2; exit 2; }
FINAL_OUT="${OUT}/${OUTPUT_NAME}.out"
echo "MODUL    ${OUTPUT_NAME}.out"
irs=()
for source in "${sources[@]}"; do
    name="$(basename "$source" .tc)"
    ir="${WORK}/${name}.ir"
    echo "IR       ${name}.tc -> ${name}.ir"
    "$PARSER" "@${source}" > "$ir"
    irs+=("$ir")
done

echo "MERGE    ${#irs[@]} IR-Dateien -> project.ir"
python3 "$MERGE" "${irs[@]}" > "${WORK}/project.ir"

echo "BACKEND  project.ir -> ${OUTPUT_NAME}.s68k"
"$BACKEND" "${WORK}/project.ir" "${WORK}/${OUTPUT_NAME}.s68k" -os9 -largedata

if (( NO_LINK )); then
    cp "${WORK}/${OUTPUT_NAME}.s68k" "${OUT}/${OUTPUT_NAME}.s68k"
    echo "FERTIG   Link uebersprungen (--no-link)"
    echo "         Ausgabe: ${OUT}/${OUTPUT_NAME}.s68k"
    exit 0
fi

[[ -x "$WINE_APP" ]] || {
    echo "WARNUNG: Wine fehlt: $WINE_APP" >&2
    echo "         Assemblerdatei bleibt in ${OUT}/${OUTPUT_NAME}.s68k"
    cp "${WORK}/${OUTPUT_NAME}.s68k" "${OUT}/${OUTPUT_NAME}.s68k"
    exit 0
}

for file in cstart.r clib.l os_lib.l sys.l; do
    [[ -f "${MWOS_TMP}/${file}" ]] || {
        echo "WARNUNG: MWOS-Datei fehlt: ${MWOS_TMP}/${file}" >&2
        echo "         Assemblerdatei bleibt in ${OUT}/${OUTPUT_NAME}.s68k"
        cp "${WORK}/${OUTPUT_NAME}.s68k" "${OUT}/${OUTPUT_NAME}.s68k"
        exit 0
    }
done

cp "${WORK}/${OUTPUT_NAME}.s68k" "${MWOS_TMP}/qcc_project.s68k"
WINEPREFIX="$WINEPREFIX" arch -x86_64 "$WINE_APP" cmd /c \
    "${MWOS_WIN_ROOT}\\DOS\\BIN\\r68.exe ${MWOS_WIN_ROOT}\\TMP\\qcc_project.s68k -o=${MWOS_WIN_ROOT}\\TMP\\qcc_project.r -q"
link_status=0
WINEPREFIX="$WINEPREFIX" arch -x86_64 "$WINE_APP" cmd /c \
    "${MWOS_WIN_ROOT}\\DOS\\BIN\\l68.exe -a ${MWOS_WIN_ROOT}\\TMP\\cstart.r ${MWOS_WIN_ROOT}\\TMP\\qcc_project.r -l=${MWOS_WIN_ROOT}\\TMP\\clib.l -l=${MWOS_WIN_ROOT}\\TMP\\os_lib.l -l=${MWOS_WIN_ROOT}\\TMP\\sys.l -o=${MWOS_WIN_ROOT}\\TMP\\qcc_project.out -s=${MWOS_WIN_ROOT}\\TMP\\qcc_project.sym" || link_status=$?
[[ -f "${MWOS_TMP}/qcc_project.out" ]] || {
    echo "FEHLER: l68 hat kein OS-9-Modul erzeugt (Status ${link_status})" >&2
    exit 1
}
cp "${MWOS_TMP}/qcc_project.out" "$FINAL_OUT"
if (( link_status != 0 )); then
    echo "WARNUNG: l68 meldete Status ${link_status}; Modul ist dennoch vorhanden"
fi
echo "LINK     ${OUTPUT_NAME}.out erzeugt"
echo "FERTIG   Ausgabe: ${FINAL_OUT}"
