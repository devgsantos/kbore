#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"

BUFFER_C="$FFMPEG/libavutil/buffer.c"
BUFFER_H="$FFMPEG/libavutil/buffer.h"

if [ ! -f "$BUFFER_C" ]; then
  echo "ERROR: not found: $BUFFER_C"
  exit 1
fi

if [ ! -f "$BUFFER_H" ]; then
  echo "ERROR: not found: $BUFFER_H"
  exit 1
fi

python3 - <<PY
from pathlib import Path

buffer_c = Path("$BUFFER_C")
buffer_h = Path("$BUFFER_H")

c = buffer_c.read_text()
h = buffer_h.read_text()

# 1) Ensure config.h is included.
if '#include "config.h"' not in c:
    marker = ' */\\n\\n'
    pos = c.find(marker)
    if pos == -1:
        raise SystemExit("Could not find license header end in buffer.c")
    pos += len(marker)
    c = c[:pos] + '#include "config.h"\\n\\n' + c[pos:]

# 2) Ensure malloc.h block exists.
malloc_block = '''#if HAVE_MALLOC_H
#include <malloc.h>
#endif
'''

if '#if HAVE_MALLOC_H' not in c:
    marker = '#include <string.h>\\n'
    if marker not in c:
        raise SystemExit("Could not find #include <string.h> in buffer.c")
    c = c.replace(marker, marker + malloc_block + '\\n', 1)

# 3) Add av_buffer_aligned_alloc implementation after av_buffer_allocz.
if 'AVBufferRef *av_buffer_aligned_alloc(size_t size, size_t align)' not in c:
    marker = '''AVBufferRef *av_buffer_ref(const AVBufferRef *buf)
'''
    if marker not in c:
        raise SystemExit("Could not find av_buffer_ref marker in buffer.c")

    function = r'''
AVBufferRef *av_buffer_aligned_alloc(size_t size, size_t align)
{
    AVBufferRef *ret = NULL;
    uint8_t    *data = NULL;

#if HAVE_POSIX_MEMALIGN
    if (posix_memalign((void **)&data, align, size))
        return NULL;
#elif HAVE_ALIGNED_MALLOC
    data = aligned_alloc(align, size);
#elif HAVE_MEMALIGN
    data = memalign(align, size);
#else
    return NULL;
#endif

    if (!data)
        return NULL;

    ret = av_buffer_create(data, size, av_buffer_default_free, NULL, 0);
    if (!ret)
        av_freep(&data);

    return ret;
}

'''
    c = c.replace(marker, function + marker, 1)

# 4) Add declaration in buffer.h after av_buffer_allocz.
if 'AVBufferRef *av_buffer_aligned_alloc(size_t size, size_t align);' not in h:
    marker = '''AVBufferRef *av_buffer_allocz(size_t size);
'''
    if marker not in h:
        raise SystemExit("Could not find av_buffer_allocz declaration in buffer.h")

    declaration = r'''
/**
 * Allocate an AVBuffer of the given size and alignment.
 *
 * @return an AVBufferRef of given size or NULL when out of memory
 */
AVBufferRef *av_buffer_aligned_alloc(size_t size, size_t align);
'''
    h = h.replace(marker, marker + declaration, 1)

buffer_c.write_text(c)
buffer_h.write_text(h)

print("Applied nvtegra patch 0001 manually:")
print("  libavutil/buffer.c")
print("  libavutil/buffer.h")
PY