#define _POSIX_C_SOURCE 200809L
#include "srwriter.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "srzip.h"

static char *join(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    char *s = malloc(na + nb + 1);

    if (!s)
        return NULL;
    memcpy(s, a, na);
    memcpy(s + na, b, nb + 1);
    return s;
}

static void release(srwriter_t *w)
{
    if (w->fd >= 0)
        close(w->fd);
    w->fd = -1;
    free(w->out_path);
    free(w->payload_path);
    w->out_path = w->payload_path = NULL;
}

int srwriter_open(srwriter_t *w, const char *out_path)
{
    memset(w, 0, sizeof(*w));
    w->fd = -1;
    w->out_path = join(out_path, "");
    w->payload_path = join(out_path, ".logic");
    if (!w->out_path || !w->payload_path) {
        fprintf(stderr, "srwriter: out of memory\n");
        release(w);
        return -1;
    }

    w->fd = open(w->payload_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (w->fd < 0) {
        fprintf(stderr, "srwriter: cannot create %s: %s\n", w->payload_path, strerror(errno));
        release(w);
        return -1;
    }
    return 0;
}

int srwriter_write(srwriter_t *w, const uint8_t *samples, size_t n)
{
    size_t done = 0;

    while (done < n) {
        ssize_t got = write(w->fd, samples + done, n - done);

        if (got < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "srwriter: writing %s: %s\n", w->payload_path, strerror(errno));
            return -1;
        }
        done += (size_t)got;
    }
    w->samples += n;
    return 0;
}

int srwriter_truncate(srwriter_t *w, uint64_t samples)
{
    if (samples >= w->samples)
        return 0;
    if (ftruncate(w->fd, (off_t)samples)) {
        fprintf(stderr, "srwriter: truncating %s: %s\n", w->payload_path, strerror(errno));
        return -1;
    }
    w->samples = samples;
    return 0;
}

int srwriter_finish(srwriter_t *w, uint64_t samplerate_hz, unsigned channels,
                    const char *const *probe_names)
{
    int ret;

    if (close(w->fd)) {
        fprintf(stderr, "srwriter: writing %s: %s\n", w->payload_path, strerror(errno));
        w->fd = -1;
        srwriter_abort(w);
        return -1;
    }
    w->fd = -1;

    ret = srzip_write_file(w->out_path, w->payload_path, samplerate_hz, channels, probe_names);
    unlink(w->payload_path);
    release(w);
    return ret;
}

void srwriter_abort(srwriter_t *w)
{
    if (w->payload_path)
        unlink(w->payload_path);
    release(w);
}
