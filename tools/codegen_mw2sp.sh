#!/usr/bin/env bash
# MW2 campaign's tools/codegen.sh (Linux host): codegen of mw2sp/ and the fix-up steps, in order.
# Needs work/load_tu6 (tools/install/tu6_setup.py) and sdk/out/linux-amd64/rexglue (tools/build_host_linux.sh).
# localize.py is left out until mw2sp has a locals list (docs/MW2.md, milestone 5).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$ROOT/mw2sp/generated/default"
PY="${PYTHON:-python3}"

echo "== codegen"
(cd "$ROOT/mw2sp" && "$ROOT/sdk/out/linux-amd64/rexglue" codegen --ignore-stamp mw2sp_manifest.toml > codegen.log 2>&1) || {
  tail -n 8 "$ROOT/mw2sp/codegen.log" >&2
  echo "codegen failed, see mw2sp/codegen.log (unresolved branches go in mw2sp/overrides.toml)" >&2
  exit 1
}
tail -n 2 "$ROOT/mw2sp/codegen.log"
echo "== registers read before written"
"$PY" -I "$ROOT/tools/read_before_write.py" --gen "$GEN" --strict > "$ROOT/mw2sp/codegen_read_before_write.log"
head -n 1 "$ROOT/mw2sp/codegen_read_before_write.log"
echo "== setjmp sites"
"$PY" -I "$ROOT/tools/setjmp_locals.py" --gen "$GEN" --fix
echo "== guest memory barriers"
"$PY" -I "$ROOT/tools/guest_barriers.py" --gen "$GEN"
echo "== direct calls (unhooked functions inlinable)"
"$PY" -I "$ROOT/tools/direct_calls.py" --gen "$GEN"
echo "== stats"
"$PY" -I "$ROOT/tools/codegen_stats.py" "$GEN"
