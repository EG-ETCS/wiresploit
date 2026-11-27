#ifndef PI5LA_CHECK_H
#define PI5LA_CHECK_H

#include <stdio.h>

static int check_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            check_failures++;                                                \
        }                                                                    \
    } while (0)

#define CHECK_DONE()                                   \
    do {                                               \
        if (check_failures)                            \
            fprintf(stderr, "%d failure(s)\n", check_failures); \
        return check_failures ? 1 : 0;                 \
    } while (0)

#endif
