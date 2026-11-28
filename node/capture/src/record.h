/**
 * @file record.h
 * @brief Recording to disk while the capture runs.
 *
 * Samples are written as they arrive (src/srwriter.c), so a capture is bounded
 * by disk space, not RAM, and needs no length given up front.
 *
 * Two modes:
 * - continuous: one .sr at out_path, everything is written.
 * - monitor: one .sr per burst of traffic in monitor_dir, plus index.json.
 *   Idle is never written (src/burst.c).
 *
 * With silence_s, the capture waits for the first change, keeps pre_s of
 * lead-in, and stops once the line has been quiet that long.
 */
#ifndef PI5LA_RECORD_H
#define PI5LA_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "capture.h"
#include "piolib.h"

/** Options for record_run(). */
typedef struct {
    /** Output .sr file (continuous mode). */
    const char *out_path;
    /** Output directory (monitor mode; mutually exclusive with out_path). */
    const char *monitor_dir;
    /** Silence that stops the capture, in seconds; 0 = run until stopped. */
    double silence_s;
    /** Silence that ends a burst (monitor), in seconds. */
    double gap_s;
    /** Lead-in kept before the first change, in seconds. */
    double pre_s;
    /** Tail kept after the last change, in seconds. */
    double post_s;
    /** Sample limit; 0 = unlimited. */
    uint64_t max_samples;
    /** Payload bytes written limit; 0 = unlimited. */
    uint64_t max_bytes;

    /*
     * What is on the bus, declared before the run starts -- never guessed from
     * the data. The capture itself does not care: it detects edges. This only
     * says how a burst should be decoded downstream, and is recorded with
     * every burst so the files on disk describe themselves.
     */
    /** Declared protocol: "uart", "raw", ...; NULL means "raw". */
    const char *protocol;
    /** Channel to decode; -1 when not applicable. */
    int decode_channel;
    /** Baud rate; 0 when the protocol has no baud rate. */
    unsigned baud;

    /*
     * Live shipping: with es_url set, each burst is handed to sr2es.py as it
     * closes instead of waiting for the whole run to finish (src/shipper.c).
     */
    /** Elasticsearch base URL; NULL disables live shipping. */
    const char *es_url;
    /** Elasticsearch index name. */
    const char *es_index;
    /** sr2es.py to run; NULL: resolved next to this binary, then PATH. */
    const char *sr2es_path;
    /** UART line type recorded in the documents, for example TTL-3V3. */
    const char *uart_type;
    /** Port label recorded in the documents. */
    const char *port;
    /** Skip TLS certificate checks when shipping. */
    bool insecure;
} record_opts_t;

/** Statistics filled in by record_run(). */
typedef struct {
    /** Samples seen. */
    uint64_t samples;
    /** Sample bytes written to disk. */
    uint64_t written;
    /** Bursts written (monitor mode). */
    unsigned bursts;
    /** True once any change was seen on the bus. */
    bool activity_seen;
    /** Stopped by silence_s. */
    bool silence_stopped;
    /** Stopped by max_samples. */
    bool samples_cap_hit;
    /** Stopped by max_bytes. */
    bool size_cap_hit;
    /** A disk write failed. */
    bool write_failed;
    /** Bursts handed to a shipper. */
    unsigned shipped;
    /** Bursts left for a later sweep (shipper busy). */
    unsigned ship_deferred;
} record_stats_t;

/**
 * Capture to disk until stopped (cfg->stop), a cap is reached, or silence_s elapses.
 *
 * @param pio   PIO instance to use.
 * @param cfg   What to capture; cfg->samples is ignored.
 * @param opts  Where and how to record.
 * @param stats Filled with recording statistics.
 * @param sres  Filled with the stream statistics.
 * @return 0, or -1 on error (message on stderr).
 */
int record_run(PIO pio, const capture_config_t *cfg, const record_opts_t *opts,
               record_stats_t *stats, capture_stream_result_t *sres);

#endif
