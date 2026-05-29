#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"

export CONFIGURE="$FFMPEG/configure"
export CPU_C="$FFMPEG/libavutil/cpu.c"

if [ ! -f "$CONFIGURE" ]; then
  echo "ERROR: not found: $CONFIGURE"
  exit 1
fi

if [ ! -f "$CPU_C" ]; then
  echo "ERROR: not found: $CPU_C"
  exit 1
fi

python3 - <<'PY'
import os
from pathlib import Path

configure = Path(os.environ["CONFIGURE"])
cpu_c = Path(os.environ["CPU_C"])

conf = configure.read_text()
cpu = cpu_c.read_text()

# -------------------------------------------------------------------
# 1) configure: add horizon target_os handling near the first target_os case
# -------------------------------------------------------------------

if "horizon)" not in conf or "enable section_data_rel_ro" not in conf:
    case_pos = conf.find("case $target_os in")
    if case_pos == -1:
        raise SystemExit("Could not find first 'case $target_os in' in configure")

    none_pos = conf.find("    none)", case_pos)
    if none_pos == -1:
        raise SystemExit("Could not find 'none)' entry after first target_os case")

    insert = """    horizon)
        enable section_data_rel_ro
        add_extralibs -lnx
        ;;
"""

    conf = conf[:none_pos] + insert + conf[none_pos:]

# -------------------------------------------------------------------
# 2) configure: disable sysctl/sysctlbyname for horizon in target_os feature case
# -------------------------------------------------------------------

if "horizon)\n    disable sysctl\n    disable sysctlbyname" not in conf:
    flatten_pos = conf.find("flatten_extralibs()")
    if flatten_pos == -1:
        raise SystemExit("Could not find flatten_extralibs() in configure")

    esac_pos = conf.rfind("\nesac", 0, flatten_pos)
    if esac_pos == -1:
        raise SystemExit("Could not find esac before flatten_extralibs()")

    insert = """horizon)
    disable sysctl
    disable sysctlbyname
    ;;
"""

    conf = conf[:esac_pos + 1] + insert + conf[esac_pos + 1:]

configure.write_text(conf)

# -------------------------------------------------------------------
# 3) libavutil/cpu.c: include switch.h
# -------------------------------------------------------------------

if "#include <switch.h>" not in cpu:
    unistd_block = """#if HAVE_UNISTD_H
#include <unistd.h>
#endif
"""
    insert = """#ifdef __SWITCH__
#include <switch.h>
#endif
"""

    if unistd_block in cpu:
        cpu = cpu.replace(unistd_block, unistd_block + insert, 1)
    else:
        fallback = "#include <string.h>\n"
        if fallback not in cpu:
            raise SystemExit("Could not find include insertion point in cpu.c")
        cpu = cpu.replace(fallback, fallback + insert, 1)

# -------------------------------------------------------------------
# 4) libavutil/cpu.c: add Switch CPU core count branch
# -------------------------------------------------------------------

if "svcGetInfo(&core_mask, InfoType_CoreMask" not in cpu:
    printed_marker = "    if (!atomic_exchange_explicit(&printed, 1, memory_order_relaxed))"
    marker_pos = cpu.find(printed_marker)

    if marker_pos == -1:
        raise SystemExit("Could not find printed marker in av_cpu_count()")

    endif_pos = cpu.rfind("#endif", 0, marker_pos)

    if endif_pos == -1:
        raise SystemExit("Could not find #endif before printed marker in av_cpu_count()")

    switch_branch = """#elif defined(__SWITCH__)
    u64 core_mask = 0;
    Result rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    nb_cpus = R_SUCCEEDED(rc) ? av_popcount64(core_mask) : 3;
"""

    cpu = cpu[:endif_pos] + switch_branch + cpu[endif_pos:]

cpu_c.write_text(cpu)

print("Applied nvtegra patch 0002 manually:")
print("  configure")
print("  libavutil/cpu.c")
PY