#!/usr/bin/env bash
#
# Matrix benchmark for the Album Mosaic plugin.
#
# Patches AlbumMosaic settings in BOTH fooyin.conf (global defaults) and the
# active layout.fyl (per-instance config) before each run, launches fooyin for
# a longer period, monitors RSS/CPU, and greps for corruption warnings.
#
# Usage:
#   ./scripts/bench.sh                 # full matrix, 90s per case
#   ./scripts/bench.sh --time=120      # custom duration per case
#   ./scripts/bench.sh --case=wave_med # run a single named case
#
# Results: bench-logs/<case>.log + summary printed at the end.
#
set -euo pipefail

DURATION=90
SINGLE_CASE=""
for arg in "$@"; do
    case "$arg" in
        --time=*) DURATION="${arg#--time=}" ;;
        --case=*) SINGLE_CASE="${arg#--case=}" ;;
        --help|-h) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
    esac
done

CONF="$HOME/.config/fooyin/fooyin.conf"
LAYOUT="$HOME/.config/fooyin/layout.fyl"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOGDIR="$ROOT/bench-logs"
mkdir -p "$LOGDIR"

# --- Patch settings in fooyin.conf + layout.fyl -------------------------------
# Args: key=value pairs. Updates [AlbumMosaic] in fooyin.conf and the
# "AlbumMosaic" widget object in the active layout JSON.
patch_settings() {
    python3 - "$CONF" "$LAYOUT" "$@" <<'PYEOF'
import sys, json, re, os

conf_path, layout_path = sys.argv[1], sys.argv[2]
pairs = dict(kv.split('=', 1) for kv in sys.argv[3:])

# fooyin.conf — INI, [AlbumMosaic] section
lines = open(conf_path).read().splitlines()
in_sec = False
seen = set()
out = []
for line in lines:
    if line.startswith('['):
        if in_sec:
            # append missing keys before leaving the section
            for k, v in pairs.items():
                if k not in seen:
                    out.append(f"{k}={v}")
        in_sec = (line.strip() == '[AlbumMosaic]')
        out.append(line)
        continue
    if in_sec and '=' in line:
        k = line.split('=', 1)[0].strip()
        if k in pairs:
            out.append(f"{k}={pairs[k]}")
            seen.add(k)
            continue
    out.append(line)
if in_sec:
    for k, v in pairs.items():
        if k not in seen:
            out.append(f"{k}={v}")
open(conf_path, 'w').write('\n'.join(out) + '\n')

# layout.fyl — JSON; patch every "AlbumMosaic" widget object recursively
def walk(node):
    if isinstance(node, dict):
        for k, v in node.items():
            if k == 'AlbumMosaic' and isinstance(v, dict):
                v.update(json.loads(json.dumps(pairs)))
            else:
                walk(v)
    elif isinstance(node, list):
        for v in node:
            walk(v)

try:
    doc = json.load(open(layout_path))
    # keys in layout are camelCase; map conf names
    keymap = {
        'EnableAnim': 'enableAnim', 'AnimType': 'animType',
        'AnimSpeed': 'animSpeed', 'AnimScope': 'animScope',
        'SortMode': 'sortMode', 'ColumnCount': 'columnCount',
        'BgColor': 'bgColor', 'GenreFilter': 'genreFilter',
        'ArtistFilter': 'artistFilter',
    }
    pairs2 = {}
    for kv in sys.argv[3:]:
        k, v = kv.split('=', 1)
        lk = keymap.get(k)
        if lk:
            if v in ('true', 'false'):
                pairs2[lk] = (v == 'true')
            elif v.lstrip('-').isdigit():
                pairs2[lk] = int(v)
            else:
                pairs2[lk] = v
    pairs = pairs2
    walk(doc)
    open(layout_path, 'w').write(json.dumps(doc, indent=4))
except Exception as e:
    print(f"  (layout patch skipped: {e})")
PYEOF
}

# --- Run one case -------------------------------------------------------------
run_case() {
    local name="$1"; shift
    echo ""
    echo "================================================================"
    echo "  CASE: $name   ($DURATION s)"
    echo "  Settings: $*"
    echo "================================================================"

    pkill -9 -f "fooyin-dev/bin/fooyin" 2>/dev/null || true
    sleep 1
    patch_settings "$@"

    "$ROOT/scripts/test.sh" --timeout="$DURATION" --monitor > "$LOGDIR/$name.log" 2>&1 || true

    # Summary
    local corrupt loaded maxrss
    corrupt=$(grep -c "m_albumOrder corrupted" "$LOGDIR/$name.log" || true)
    loaded=$(grep -c "Loaded .* albums" "$LOGDIR/$name.log" || true)
    maxrss=$(grep -oP 'RSS: \K[0-9]+' "$LOGDIR/$name.log" | sort -n | tail -1 || echo "?")
    # avg CPU skipping first 15s (startup/scan noise)
    local avgcpu
    avgcpu=$(grep -oP '\[\d+s\] RSS: \d+ KB, CPU: \K[0-9.]+' "$LOGDIR/$name.log" \
             | tail -n +4 | awk '{s+=$1; n++} END{if(n) printf "%.1f", s/n; else print "?"}')
    echo "  -> albums_loaded_msgs=$loaded  corruption_warnings=$corrupt  max_rss=${maxrss}KB  avg_cpu(post-15s)=${avgcpu}%"
    grep -iE "corrupt|error|warning|assert|crash" "$LOGDIR/$name.log" | grep -v "corruption_warnings" | head -5 || true
}

# --- Safety: kill any running fooyin first -------------------------------------
pkill -9 -f "fooyin-dev/bin/fooyin" 2>/dev/null || true
cp "$CONF" "$LOGDIR/fooyin.conf.bak"
cp "$LAYOUT" "$LOGDIR/layout.fyl.bak" 2>/dev/null || true

# --- Matrix --------------------------------------------------------------------
if [ -n "$SINGLE_CASE" ]; then
    case "$SINGLE_CASE" in
        noanim)      run_case noanim EnableAnim=false AnimScope=Wave AnimSpeed=Medium ColumnCount=14 ;;
        single_fast) run_case single_fast EnableAnim=true AnimScope=Single AnimSpeed=Fast ColumnCount=10 ;;
        single_slow) run_case single_slow EnableAnim=true AnimScope=Single AnimSpeed=Slow ColumnCount=10 ;;
        multi_med)   run_case multi_med EnableAnim=true AnimScope=Multiple AnimSpeed=Medium ColumnCount=10 ;;
        wave_med)    run_case wave_med EnableAnim=true AnimScope=Wave AnimSpeed=Medium ColumnCount=14 ;;
        wave_slow)   run_case wave_slow EnableAnim=true AnimScope=Wave AnimSpeed=Slow ColumnCount=14 ;;
        multi_wide)  run_case multi_wide EnableAnim=true AnimScope=Multiple AnimSpeed=Slow ColumnCount=20 ;;
        zoom5)       run_case zoom5 EnableAnim=true AnimScope=Multiple AnimSpeed=Medium ColumnCount=5 ;;
        *) echo "Unknown case: $SINGLE_CASE"; exit 1 ;;
    esac
else
    run_case noanim     EnableAnim=false AnimScope=Wave AnimSpeed=Medium ColumnCount=14
    run_case single_fast EnableAnim=true AnimScope=Single AnimSpeed=Fast ColumnCount=10
    run_case multi_med  EnableAnim=true AnimScope=Multiple AnimSpeed=Medium ColumnCount=10
    run_case wave_med   EnableAnim=true AnimScope=Wave AnimSpeed=Medium ColumnCount=14
    run_case wave_slow  EnableAnim=true AnimScope=Wave AnimSpeed=Slow ColumnCount=14
    run_case zoom5      EnableAnim=true AnimScope=Multiple AnimSpeed=Medium ColumnCount=5
fi

echo ""
echo "=== Bench complete — logs in $LOGDIR ==="
echo "(backups: fooyin.conf.bak, layout.fyl.bak)"
