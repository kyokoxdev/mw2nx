#!/usr/bin/env bash
# Builds zone_containers (host) against mw2-recompiled's fastfile reader, launcher/zone (GPL-3.0), which is read from
# a checkout of that project, never copied: MW2_RECOMPILED (default ~/projects/mw2-recompiled).
# Usage: tools/mw2shaders/build_zone_containers.sh <output folder>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:?usage: build_zone_containers.sh <output folder>}"
MW2R="${MW2_RECOMPILED:-$HOME/projects/mw2-recompiled}"
Z="$MW2R/launcher/zone"
[ -f "$Z/walker.cpp" ] || { echo "no mw2-recompiled checkout at $MW2R (set MW2_RECOMPILED)" >&2; exit 1; }
mkdir -p "$OUT"
# The loading rules embedded as launcher_zone_defs.cpp, as mw2-recompiled's CMakeLists.txt makes it.
python3 -I - "$Z" "$OUT/zone_defs.cpp" <<'EOF'
import sys, pathlib
zone, out = pathlib.Path(sys.argv[1]), sys.argv[2]
text, table, n = ["// Generated from launcher/zone/defs/ and defs_pc/.\n"], [], 0
for folder, prefix in (("defs", ""), ("defs_pc", "pc/")):
    base = zone / folder
    files = sorted(p for pattern in ("*.h", "*.txt", "assets/*.txt") for p in base.glob(pattern))
    for path in files:
        data = path.read_bytes()
        text.append(f"static const unsigned char kDef{n}[] = {{ {','.join(str(b) for b in data)} }};\n")
        table.append(f'    {{ "{prefix}{path.relative_to(base).as_posix()}", kDef{n}, {len(data)} }},\n')
        n += 1
text.append("struct ZoneDefinition { const char* name; const unsigned char* data; unsigned size; };\n"
            f"extern const ZoneDefinition kZoneDefinitions[] = {{\n{''.join(table)}}};\n"
            f"extern const unsigned kZoneDefinitionCount = {n};\n")
open(out, "w").write("".join(text))
EOF
CXX="${CXX:-$(command -v clang++ || echo "$HOME/tools/clang/usr/lib/llvm-21/bin/clang++")}"
"$CXX" -std=c++20 -O2 -w "-I$Z" "-I$ROOT/sdk/thirdparty/xxHash" \
  "$HERE/zone_containers.cpp" "$Z/walker.cpp" "$Z/schema.cpp" "$Z/defs.cpp" "$Z/expr.cpp" "$Z/rules.cpp" \
  "$OUT/zone_defs.cpp" -o "$OUT/zone_containers"
echo "zone_containers: $OUT/zone_containers"
