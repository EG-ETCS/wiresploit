#ifndef PI5LA_SRWRITER_H
#define PI5LA_SRWRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Streaming .sr writer.
 *
 * Samples are appended to a payload file (one byte per sample) as they arrive,
 * so a capture is limited by disk space rather than RAM. srwriter_finish()
 * wraps the payload in the sigrok session file and removes it.
 *
 * The payload lives next to the output as "<out>.logic".
 */
typedef struct {
    char *out_path;
    char *payload_path;
    int fd;
    uint64_t samples;       /* bytes written to the payload */
} srwriter_t;

/* Returns 0, or -1 with a message on stderr. */
int srwriter_open(srwriter_t *w, const char *out_path);

/* Append samples (one byte each). Returns 0 or -1. */
int srwriter_write(srwriter_t *w, const uint8_t *samples, size_t n);

/* Drop everything after `samples`, e.g. a burst's trailing idle. Returns 0 or -1. */
int srwriter_truncate(srwriter_t *w, uint64_t samples);

/* Write the .sr and delete the payload. Returns 0 or -1; the writer is closed either way. */
int srwriter_finish(srwriter_t *w, uint64_t samplerate_hz, unsigned channels,
                    const char *const *probe_names);

/* Close and delete the payload without writing a .sr. */
void srwriter_abort(srwriter_t *w);

#endif
