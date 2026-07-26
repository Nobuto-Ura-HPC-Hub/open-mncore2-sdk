#ifndef ULIB_PARSE_U64_H
#define ULIB_PARSE_U64_H

#include <stdlib.h>
#include <stdint.h>

static inline uint64_t parse_u64(const char *s)
{
    return strtoull(s, NULL, 0);
}

#endif /* ULIB_PARSE_U64_H */
