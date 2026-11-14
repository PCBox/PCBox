# miniz

Copied from kuba--/zip's single-header miniz 3.1.2. The source retains
its license notices. 86Box removes three explicit extern specifiers
and marks mz_version with MINIZ_EXPORT to allow private linkage. It also
guards a Windows SDK macro definition.
The floppy importer builds it with compression and zlib APIs disabled.
