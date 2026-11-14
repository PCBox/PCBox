# FatFs R0.16

Vendored from the upstream FatFs R0.16 distribution. The upstream license
notices are in the source files. The build uses `ff.c` and `ffunicode.c`
with one FAT volume, UTF-8 long filenames with CP437 short aliases, and
512- or 1024-byte sectors. The emulator provides the disk I/O callbacks.

Local changes keep mkdir available at minimization level 3, omit file reads
from the importer, use thread-local volume registration, and rename FatFs'
directory type to avoid a collision with the platform directory type.
