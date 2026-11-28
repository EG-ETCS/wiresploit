/**
 * @file srwriter.h
 * @brief Streaming .sr writer.
 *
 * Samples are appended to a payload file (one byte per sample) as they arrive,
 * so a capture is limited by disk space rather than RAM. srwriter_finish()
 * wraps the payload in the sigrok session file and removes it.
 *
 * The payload lives next to the output as `<out>.logic`.
 */
#ifndef PI5LA_SRWRITER_H
#define PI5LA_SRWRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** An open writer. Opened with srwriter_open(), closed by srwriter_finish() or srwriter_abort(). */
typedef struct {
    /** The .sr file to produce. */
    char *out_path;
    /** The payload file samples are appended to. */
    char *payload_path;
    /** Open payload file descriptor. */
    int fd;
    /** Bytes written to the payload. */
    uint64_t samples;
} srwriter_t;

/**
 * Open a writer for out_path and create its payload file.
 *
 * @param w        Writer to open.
 * @param out_path The .sr file to produce.
 * @return 0, or -1 with a message on stderr.
 */
int srwriter_open(srwriter_t *w, const char *out_path);

/**
 * Append samples (one byte each).
 *
 * @param w       Open writer.
 * @param samples Samples to append.
 * @param n       Number of samples.
 * @return 0 or -1.
 */
int srwriter_write(srwriter_t *w, const uint8_t *samples, size_t n);

/**
 * Drop everything after `samples`, for example a burst's trailing idle.
 *
 * @param w       Open writer.
 * @param samples Samples to keep.
 * @return 0 or -1.
 */
int srwriter_truncate(srwriter_t *w, uint64_t samples);

/**
 * Write the .sr and delete the payload. The writer is closed either way.
 *
 * @param w             Open writer.
 * @param samplerate_hz Sample rate, in Hz.
 * @param channels      Channel count.
 * @param probe_names   Channel names, or NULL for D0, D1, ...
 * @return 0 or -1.
 */
int srwriter_finish(srwriter_t *w, uint64_t samplerate_hz, unsigned channels,
                    const char *const *probe_names);

/**
 * Close and delete the payload without writing a .sr.
 *
 * @param w Open writer.
 */
void srwriter_abort(srwriter_t *w);

#endif
