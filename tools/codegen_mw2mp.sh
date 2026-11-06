#!/usr/bin/env bash
# MW2 multiplayer's tools/codegen.sh (Linux host): codegen of mw2mp/ and the fix-up steps, in order.
# Needs work/load_tu6 (tools/install/tu6_setup.py) and sdk/out/linux-amd64/rexglue (tools/build_host_linux.sh).
# direct_calls.py and localize.py are left out until mw2mp has hooks and a locals list.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$ROOT/mw2mp/generated/default_mp"
PY="${PYTHON:-python3}"

echo "== codegen"
(cd "$ROOT/mw2mp" && "$ROOT/sdk/out/linux-amd64/rexglue" codegen --ignore-stamp mw2mp_manifest.toml > codegen.log 2>&1) || {
  tail -n 8 "$ROOT/mw2mp/codegen.log" >&2
  echo "codegen failed, see mw2mp/codegen.log (unresolved branches go in mw2mp/overrides.toml)" >&2
  exit 1
}
tail -n 2 "$ROOT/mw2mp/codegen.log"
echo "== registers read before written"
"$PY" -I "$ROOT/tools/read_before_write.py" --gen "$GEN" --strict > "$ROOT/mw2mp/codegen_read_before_write.log"
head -n 1 "$ROOT/mw2mp/codegen_read_before_write.log"
echo "== setjmp sites"
"$PY" -I "$ROOT/tools/setjmp_locals.py" --gen "$GEN" --fix
echo "== guest memory barriers"
"$PY" -I "$ROOT/tools/guest_barriers.py" --gen "$GEN"
echo "== stats"
"$PY" -I "$ROOT/tools/codegen_stats.py" "$GEN"
