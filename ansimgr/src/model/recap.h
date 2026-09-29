#ifndef ANSIMGR_RECAP_H
#define ANSIMGR_RECAP_H

#include <stddef.h>

/*
 * The PLAY RECAP block is the only reliable signal this project has for
 * idempotency, so it gets parsed explicitly rather than scraped from the
 * rendered log.
 */

typedef struct {
    int ok;
    int changed;
    int unreachable;
    int failed;
    int skipped;
    int rescued;
    int ignored;
    int valid;      /* 1 when a recap line was found */
    char host[128];
} Recap;

/* Scan accumulated output for the recap. Later output wins. */
void recap_parse(const char *text, size_t len, Recap *out);

int  recap_field(const char *line, const char *field); /* -1 when absent */
const char *recap_verdict(const Recap *r);
/* Same outcome, worded to fit the narrow sidebar value column. */
const char *recap_verdict_short(const Recap *r);

#endif /* ANSIMGR_RECAP_H */
