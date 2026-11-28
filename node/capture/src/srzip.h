/**
 * @file srzip.h
 * @brief Write sigrok session files (.sr, srzip version 2).
 *
 * A session file is a zip archive holding:
 * - `version`: "2"
 * - `metadata`: INI with [global] and [device 1]
 * - `logic-1`: one byte per sample, channel N in bit N
 */
#ifndef PI5LA_SRZIP_H
#define PI5LA_SRZIP_H

#include <stddef.h>
#include <stdint.h>

/**
 * Write a sigrok session file from samples in memory.
 *
 * @param path          The .sr file to write.
 * @param samples       Samples, one byte each.
 * @param n_samples     Number of samples.
 * @param samplerate_hz Sample rate, in Hz.
 * @param channels      Channel count.
 * @param probe_names   Channel names; may be NULL (they are then named `D0..D<n-1>`).
 * @return 0 on success, -1 on error (message on stderr).
 */
int srzip_write(const char *path, const uint8_t *samples, size_t n_samples,
                uint64_t samplerate_hz, unsigned channels, const char *const *probe_names);

/**
 * Same file, but logic-1 is copied from an existing payload file.
 *
 * The payload holds one byte per sample, so an unbounded capture never holds
 * the samples in RAM.
 *
 * @param path          The .sr file to write.
 * @param payload_path  Payload file, one byte per sample.
 * @param samplerate_hz Sample rate, in Hz.
 * @param channels      Channel count.
 * @param probe_names   Channel names, or NULL.
 * @return 0 or -1.
 */
int srzip_write_file(const char *path, const char *payload_path, uint64_t samplerate_hz,
                     unsigned channels, const char *const *probe_names);

/**
 * Format a sample rate as sigrok does: "10 MHz", "115200 Hz", "3 kHz".
 *
 * Uses the largest unit that divides the rate exactly.
 *
 * @param hz  Sample rate, in Hz.
 * @param buf Receives the text.
 * @param len Size of buf.
 */
void srzip_format_samplerate(uint64_t hz, char *buf, size_t len);

/**
 * Build the metadata text.
 *
 * @param buf           Receives the text.
 * @param len           Size of buf.
 * @param samplerate_hz Sample rate, in Hz.
 * @param channels      Channel count.
 * @param probe_names   Channel names, or NULL.
 * @return Bytes written (excluding NUL), or -1 if buf is too small.
 */
int srzip_build_metadata(char *buf, size_t len, uint64_t samplerate_hz, unsigned channels,
                         const char *const *probe_names);

#endif
