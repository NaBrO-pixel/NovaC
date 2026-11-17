#include "diag/diag.h"

#include <string.h>

void nc_diag_init(nc_diag *d, const nc_source_map *sources, FILE *out)
{
    memset(d, 0, sizeof *d);
    d->sources = sources;
    d->out = out;
    d->error_limit = 50;
}

bool nc_diag_has_errors(const nc_diag *d)
{
    return d->errors > 0;
}

/* Number of decimal digits in n (n >= 1). */
static int digits(uint32_t n)
{
    int k = 1;
    while (n >= 10) {
        n /= 10;
        k++;
    }
    return k;
}

static void render_excerpt(nc_diag *d, nc_span span)
{
    uint32_t line, col, eline, ecol;
    nc_source_line_col(d->sources, span.file, span.lo, &line, &col);
    nc_source_line_col(d->sources, span.file, span.hi, &eline, &ecol);
    uint32_t len;
    const char *text = nc_source_line_text(d->sources, span.file, line, &len);
    int w = digits(line);
    if (w < 4) w = 4;

    fprintf(d->out, "%*u | ", w, line);
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        /* Control characters other than tab would corrupt the terminal. */
        fputc((c < 0x20 && c != '\t') || c == 0x7f ? '?' : c, d->out);
    }
    fputc('\n', d->out);

    fprintf(d->out, "%*s | ", w, "");
    uint32_t start = col - 1;
    uint32_t end = eline == line ? ecol - 1 : len;
    if (end > len) end = len;
    if (start > len) start = len;
    for (uint32_t i = 0; i < start; i++) fputc(text[i] == '\t' ? '\t' : ' ', d->out);
    fputc('^', d->out);
    for (uint32_t i = start + 1; i < end; i++) fputc('~', d->out);
    fputc('\n', d->out);
}

static void render_location(nc_diag *d, nc_span span)
{
    if (span.file == NC_NO_FILE) {
        fputs("ncc: ", d->out);
        return;
    }
    const nc_source_file *f = nc_source_get(d->sources, span.file);
    uint32_t line, col;
    nc_source_line_col(d->sources, span.file, span.lo, &line, &col);
    fprintf(d->out, "%s:%u:%u: ", f ? f->name : "?", line, col);
}

static void emit(nc_diag *d, bool is_error, nc_span span, const char *code,
                 const char *fmt, va_list ap)
{
    if (is_error) {
        if (d->limit_reached) {
            d->last_suppressed = true;
            return;
        }
        d->errors++;
    } else {
        d->warnings++;
    }
    d->last_suppressed = d->out == NULL;
    if (!d->out) return;

    render_location(d, span);
    fprintf(d->out, "%s[%s]: ", is_error ? "error" : "warning", code);
    vfprintf(d->out, fmt, ap);
    fputc('\n', d->out);
    if (span.file != NC_NO_FILE) render_excerpt(d, span);

    if (is_error && d->error_limit && d->errors >= d->error_limit) {
        d->limit_reached = true;
        fprintf(d->out, "ncc: error: too many errors (limit %u); stopping\n", d->error_limit);
    }
}

void nc_diag_error(nc_diag *d, nc_span span, const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(d, true, span, code, fmt, ap);
    va_end(ap);
}

void nc_diag_warning(nc_diag *d, nc_span span, const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (d->werror) {
        emit(d, true, span, code, fmt, ap);
    } else {
        emit(d, false, span, code, fmt, ap);
    }
    va_end(ap);
}

void nc_diag_note(nc_diag *d, nc_span span, const char *fmt, ...)
{
    if (d->last_suppressed || !d->out) return;
    va_list ap;
    va_start(ap, fmt);
    if (span.file == NC_NO_FILE) {
        fputs("      = note: ", d->out);
        vfprintf(d->out, fmt, ap);
        fputc('\n', d->out);
    } else {
        render_location(d, span);
        fputs("note: ", d->out);
        vfprintf(d->out, fmt, ap);
        fputc('\n', d->out);
        render_excerpt(d, span);
    }
    va_end(ap);
}

void nc_diag_help(nc_diag *d, const char *fmt, ...)
{
    if (d->last_suppressed || !d->out) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("      = help: ", d->out);
    vfprintf(d->out, fmt, ap);
    fputc('\n', d->out);
    va_end(ap);
}

void nc_diag_fixit(nc_diag *d, nc_span span, const char *replacement)
{
    if (d->last_suppressed || !d->out) return;
    uint32_t line, col;
    nc_source_line_col(d->sources, span.file, span.lo, &line, &col);
    fprintf(d->out, "      = fix-it: %u:%u: replace with `%s`\n", line, col, replacement);
}
