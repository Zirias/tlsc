#ifndef TLSC_UTIL_H
#define TLSC_UTIL_H

#include "decl.h"

#include <stddef.h>

DECLEXPORT void *xmalloc(size_t size)
    ATTR_MALLOC ATTR_RETNONNULL ATTR_ALLOCSZ((1));
DECLEXPORT void *xrealloc(void *ptr, size_t size)
    ATTR_RETNONNULL ATTR_ALLOCSZ((2));
DECLEXPORT char *copystr(const char *src) ATTR_MALLOC;
DECLEXPORT char *lowerstr(const char *src) ATTR_MALLOC;
DECLEXPORT char *joinstr(const char *delim, char **strings)
    ATTR_MALLOC ATTR_NONNULL((1));

#endif
