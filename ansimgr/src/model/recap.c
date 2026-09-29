#include "model/recap.h"
#include "edit/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int recap_field(const char *line, const char *field)
{
    const char *p = strstr(line, field);
    if (!p) return -1;
    p += strlen(field);
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    int v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

static int is_recap_line(const char *s)
{
    return strstr(s, "ok=") != NULL && strstr(s, "changed=") != NULL;
}

void recap_parse(const char *text, size_t len, Recap *out)
{
    /* Callers read the struct unconditionally, so it must be a defined "no
     * recap found" rather than whatever was on their stack. */
    memset(out, 0, sizeof(*out));

    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);

    while (line_next(&it, &ln)) {
        if (!is_recap_line(ln.text)) continue;

        Recap r;
        memset(&r, 0, sizeof(r));
        r.valid = 1;
        r.ok = recap_field(ln.text, "ok=");
        r.changed = recap_field(ln.text, "changed=");
        r.unreachable = recap_field(ln.text, "unreachable=");
        r.failed = recap_field(ln.text, "failed=");
        r.skipped = recap_field(ln.text, "skipped=");
        r.rescued = recap_field(ln.text, "rescued=");
        r.ignored = recap_field(ln.text, "ignored=");

        /* The host name is everything before the " : " separator. */
        const char *sep = strstr(ln.text, " : ");
        if (sep) {
            size_t n = (size_t)(sep - ln.text);
            if (n >= sizeof(r.host)) n = sizeof(r.host) - 1;
            memcpy(r.host, ln.text, n);
            r.host[n] = '\0';
        } else {
            const char *c = strchr(ln.text, ':');
            if (c) {
                size_t n = (size_t)(c - ln.text);
                if (n >= sizeof(r.host)) n = sizeof(r.host) - 1;
                memcpy(r.host, ln.text, n);
                r.host[n] = '\0';
            }
        }
        text_trim(r.host);
        *out = r; /* last recap wins */
    }
}

const char *recap_verdict(const Recap *r)
{
    if (!r || !r->valid) return "sem resultado";
    if (r->failed > 0) return "FALHOU";
    if (r->unreachable > 0) return "INALCANÇÁVEL";
    if (r->changed > 0) return "NÃO IDEMPOTENTE";
    return "IDEMPOTENTE";
}

const char *recap_verdict_short(const Recap *r)
{
    /* The sidebar value column is 13 wide, and "NÃO IDEMPOTENTE" does not fit:
     * truncating it there would read as a broken word. */
    if (!r || !r->valid) return "--";
    if (r->failed > 0) return "FALHOU";
    if (r->unreachable > 0) return "SEM ACESSO";
    if (r->changed > 0) return "MUDOU";
    return "IDEMPOTENTE";
}
