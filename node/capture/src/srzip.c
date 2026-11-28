#include "srzip.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zip.h>

void srzip_format_samplerate(uint64_t hz, char *buf, size_t len)
{
    if (hz >= UINT64_C(1000000000) && hz % UINT64_C(1000000000) == 0)
        snprintf(buf, len, "%" PRIu64 " GHz", hz / UINT64_C(1000000000));
    else if (hz >= UINT64_C(1000000) && hz % UINT64_C(1000000) == 0)
        snprintf(buf, len, "%" PRIu64 " MHz", hz / UINT64_C(1000000));
    else if (hz >= UINT64_C(1000) && hz % UINT64_C(1000) == 0)
        snprintf(buf, len, "%" PRIu64 " kHz", hz / UINT64_C(1000));
    else
        snprintf(buf, len, "%" PRIu64 " Hz", hz);
}

static int appendf(char *buf, size_t len, size_t *pos, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*pos >= len)
        return -1;
    va_start(ap, fmt);
    n = vsnprintf(buf + *pos, len - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= len - *pos)
        return -1;
    *pos += (size_t)n;
    return 0;
}

int srzip_build_metadata(char *buf, size_t len, uint64_t samplerate_hz, unsigned channels,
                         const char *const *probe_names)
{
    char rate[32];
    size_t pos = 0;
    unsigned i;

    srzip_format_samplerate(samplerate_hz, rate, sizeof(rate));
    if (appendf(buf, len, &pos, "[global]\n") ||
        appendf(buf, len, &pos, "sigrok version=0.5.2\n\n") ||
        appendf(buf, len, &pos, "[device 1]\n") ||
        appendf(buf, len, &pos, "capturefile=logic-1\n") ||
        appendf(buf, len, &pos, "total probes=%u\n", channels) ||
        appendf(buf, len, &pos, "samplerate=%s\n", rate) ||
        appendf(buf, len, &pos, "total analog=0\n"))
        return -1;
    for (i = 0; i < channels; i++) {
        if (probe_names ? appendf(buf, len, &pos, "probe%u=%s\n", i + 1, probe_names[i])
                        : appendf(buf, len, &pos, "probe%u=D%u\n", i + 1, i))
            return -1;
    }
    if (appendf(buf, len, &pos, "unitsize=1\n"))
        return -1;
    return (int)pos;
}

static int add_member(zip_t *za, const char *name, const void *data, size_t len)
{
    zip_source_t *src = zip_source_buffer(za, data, len, 0);

    if (!src) {
        fprintf(stderr, "srzip: zip_source_buffer(%s): %s\n", name, zip_strerror(za));
        return -1;
    }
    if (zip_file_add(za, name, src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        fprintf(stderr, "srzip: zip_file_add(%s): %s\n", name, zip_strerror(za));
        zip_source_free(src);
        return -1;
    }
    return 0;
}

static zip_t *open_sr(const char *path, char *metadata, size_t meta_size, int *meta_len,
                      uint64_t samplerate_hz, unsigned channels,
                      const char *const *probe_names)
{
    static const char version[] = "2";
    zip_t *za;
    int zerr;

    *meta_len = srzip_build_metadata(metadata, meta_size, samplerate_hz, channels,
                                     probe_names);
    if (*meta_len < 0) {
        fprintf(stderr, "srzip: metadata too long\n");
        return NULL;
    }

    za = zip_open(path, ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!za) {
        zip_error_t ze;

        zip_error_init_with_code(&ze, zerr);
        fprintf(stderr, "srzip: cannot create %s: %s\n", path, zip_error_strerror(&ze));
        zip_error_fini(&ze);
        return NULL;
    }

    /* Buffers must stay valid until zip_close(), which is where data is written. */
    if (add_member(za, "version", version, 1) ||
        add_member(za, "metadata", metadata, (size_t)*meta_len)) {
        zip_discard(za);
        return NULL;
    }
    return za;
}

static int close_sr(zip_t *za, const char *path)
{
    if (zip_close(za) < 0) {
        fprintf(stderr, "srzip: writing %s: %s\n", path, zip_strerror(za));
        zip_discard(za);
        return -1;
    }
    return 0;
}

int srzip_write(const char *path, const uint8_t *samples, size_t n_samples,
                uint64_t samplerate_hz, unsigned channels, const char *const *probe_names)
{
    char metadata[1024];
    int meta_len;
    zip_t *za;

    za = open_sr(path, metadata, sizeof(metadata), &meta_len, samplerate_hz, channels,
                 probe_names);
    if (!za)
        return -1;
    if (add_member(za, "logic-1", samples, n_samples)) {
        zip_discard(za);
        return -1;
    }
    return close_sr(za, path);
}

int srzip_write_file(const char *path, const char *payload_path, uint64_t samplerate_hz,
                     unsigned channels, const char *const *probe_names)
{
    char metadata[1024];
    zip_source_t *src;
    int meta_len;
    zip_t *za;

    za = open_sr(path, metadata, sizeof(metadata), &meta_len, samplerate_hz, channels,
                 probe_names);
    if (!za)
        return -1;

    /* libzip streams the payload from disk at zip_close(), so it is never in RAM. */
    src = zip_source_file(za, payload_path, 0, -1);
    if (!src) {
        fprintf(stderr, "srzip: cannot read %s: %s\n", payload_path, zip_strerror(za));
        zip_discard(za);
        return -1;
    }
    if (zip_file_add(za, "logic-1", src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        fprintf(stderr, "srzip: zip_file_add(logic-1): %s\n", zip_strerror(za));
        zip_source_free(src);
        zip_discard(za);
        return -1;
    }
    return close_sr(za, path);
}
