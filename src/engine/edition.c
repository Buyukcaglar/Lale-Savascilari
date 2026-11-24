/* edition.c - see edition.h */
#include "engine/edition.h"

#include <stdio.h>

int16_t g_edition = EDITION_2005;

const char *ed_file(const char *file2005)
{
    static char buf[4][48];
    static unsigned next;
    char *b;
    if (!ED_CD)
        return file2005;
    b = buf[next++ & 3];
    snprintf(b, sizeof buf[0], "T:%s", file2005);
    return b;
}
