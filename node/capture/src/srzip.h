#ifndef PI5LA_SRZIP_H
#define PI5LA_SRZIP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Write a sigrok session file (.sr, srzip version 2):
 *   version  - "2"
 *   metadata - INI with [global] and [device 1]
 *   logic-1  - one byte per sample, channel N in bit N
 *
 * probe_names may be NULL (probes are then named D0..D<n-1>).
 * Returns 0 on success, -1 on error (message on stderr).
 */
int srzip_write(const char *path, const uint8_t *samples, size_t n_samples,
                uint64_t samplerate_hz, unsigned channels, const char *const *probe_names);

/*
 * Same file, but logic-1 is copied from an existing payload file (one byte per
 * sample) instead of a memory buffer, so an unbounded capture never holds the
 * samples in RAM. Returns 0 or -1.
 */
int srzip_write_file(const char *path, const char *payload_path, uint64_t samplerate_hz,
                     unsigned channels, const char *const *probe_names);

/* "10 MHz", "115200 Hz", "3 kHz" — the largest unit that divides exactly. */
void srzip_format_samplerate(uint64_t hz, char *buf, size_t len);

/* Build the metadata text. Returns bytes written (excluding NUL) or -1 if too small. */
int srzip_build_metadata(char *buf, size_t len, uint64_t samplerate_hz, unsigned channels,
                         const char *const *probe_names);

#endif
