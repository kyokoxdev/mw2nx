#!/usr/bin/env bash
# Codegen of mw2sp/ (MW2 campaign, Linux host) and the fix-up steps, in order.
# Needs work/load_tu6 (tools/install/tu6_setup.py) and sdk/out/linux-amd64/rexglue (tools/build_host_linux.sh).
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
