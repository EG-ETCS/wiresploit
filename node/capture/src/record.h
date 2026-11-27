#ifndef PI5LA_RECORD_H
#define PI5LA_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "capture.h"
#include "piolib.h"

/*
 * Recording to disk while the capture runs.
 *
 * Samples are written as they arrive (src/srwriter.c), so a capture is bounded
 * by disk space, not RAM, and needs no length given up front.
 *
 * Two modes:
 *   continuous - one .sr at out_path, everything is written.
 *   monitor    - one .sr per burst of traffic in monitor_dir, plus index.json.
 *                Idle is never written (src/burst.c).
 *
 * With silence_s, the capture waits for the first change, keeps pre_s of
 * lead-in, and stops once the line has been quiet that long.
 */
typedef struct {
    const char *out_path;       /* continuous mode */
    const char *monitor_dir;    /* monitor mode (mutually exclusive with out_path) */
    double silence_s;           /* 0 = run until stopped */
    double gap_s;               /* silence that ends a burst (monitor) */
    double pre_s;               /* lead-in kept before the first change */
    double post_s;              /* tail kept after the last change */
    uint64_t max_samples;       /* 0 = unlimited */
    uint64_t max_bytes;         /* 0 = unlimited; payload bytes written */

    /*
     * What is on the bus, declared before the run starts -- never guessed from
     * the data. The capture itself does not care: it detects edges. This only
     * says how a burst should be decoded downstream, and is recorded with
     * every burst so the files on disk describe themselves.
     */
    const char *protocol;       /* "uart", "raw", ...; NULL means "raw" */
    int decode_channel;         /* channel to decode; -1 when not applicable */
    unsigned baud;              /* 0 when the protocol has no baud rate */

    /* Live shipping: with es_url set, each burst is handed to sr2es.py as it
       closes instead of waiting for the whole run to finish (src/shipper.c). */
    const char *es_url;
    const char *es_index;
    const char *sr2es_path;     /* NULL: resolved next to this binary, then PATH */
    const char *uart_type;
    const char *port;
    bool insecure;
} record_opts_t;

typedef struct {
    uint64_t samples;           /* samples seen */
    uint64_t written;           /* sample bytes written to disk */
    unsigned bursts;            /* monitor mode */
    bool activity_seen;
    bool silence_stopped;       /* stopped by silence_s */
    bool samples_cap_hit;
    bool size_cap_hit;
    bool write_failed;
    unsigned shipped;           /* bursts handed to a shipper */
    unsigned ship_deferred;     /* bursts left for a later sweep (shipper busy) */
} record_stats_t;

/*
 * Captures until stopped (cfg->stop), a cap is reached, or silence_s elapses.
 * Returns 0, or -1 on error (message on stderr).
 */
int record_run(PIO pio, const capture_config_t *cfg, const record_opts_t *opts,
               record_stats_t *stats, capture_stream_result_t *sres);

#endif
