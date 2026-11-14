/* Build the ZIP reader with private linkage. libchdr has its own miniz. */
#ifdef _WIN32
#    include <direct.h>
#endif

#define MINIZ_EXPORT static
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS

#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "floppy/vendor/miniz/miniz.h"
#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#endif
