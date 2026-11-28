/**
 * @file shipper.h
 * @brief Handing finished bursts to node/exporter/sr2es.py while the capture keeps running.
 *
 * Monitor mode finalises a burst-NNNN.sr the moment a burst ends, but nothing
 * used to read it until the whole session was stopped, so a run that watches a
 * bus for hours delivered nothing until it was killed. This spawns a shipper
 * for each burst instead.
 *
 * Decoding and uploading must never happen in the capture path: a piolib call
 * is a PCIe round trip and the DMA ring is only four periods deep, so a
 * sigrok-cli run or a stalled HTTP POST inside burst_close() would drop
 * samples. So the shipper is always a separate process, and never waited for
 * until the capture is over.
 *
 * At most one runs at a time. When a burst closes while one is still working,
 * nothing is queued in here: the burst simply stays on disk, and the next
 * shipper's sweep picks it up. Disk state is the queue, which also means a
 * crash loses nothing.
 */
#ifndef PI5LA_SHIPPER_H
#define PI5LA_SHIPPER_H

#include <stdbool.h>
#include <sys/types.h>

#include "record.h"

/** Shipper state. Set up with shipper_init(). */
typedef struct {
    /** False when no --es-url was given. */
    bool enabled;
    /** The recording options (URL, index, script path, labels). */
    const record_opts_t *opts;
    /** Resolved sr2es.py. */
    char script[512];
    /** The monitor directory. */
    char dir[512];
    /** GPIO of channel 0, so sr2es names channels right. */
    unsigned base_gpio;
    /** Running shipper process; 0 when none is running. */
    pid_t child;
    /** Stats counter of bursts handed to a shipper; may be NULL. */
    unsigned *spawned;
    /** Stats counter of bursts left for a later sweep; may be NULL. */
    unsigned *deferred;
} shipper_t;

/**
 * Resolves sr2es.py and validates the options.
 *
 * A missing script is an error here, at startup, rather than a silent failure
 * once per burst. With opts->es_url unset this disables the shipper and
 * returns 0, so callers need no special case.
 *
 * @param s         Shipper to initialise.
 * @param opts      Recording options; must outlive the shipper.
 * @param stats     Its shipped / ship_deferred counters are updated; may be NULL.
 * @param base_gpio GPIO of channel 0.
 * @return 0, or -1 on error (message on stderr).
 */
int shipper_init(shipper_t *s, const record_opts_t *opts, record_stats_t *stats,
                 unsigned base_gpio);

/**
 * A burst just closed. Never blocks; failures warn and are not fatal.
 *
 * @param s Shipper.
 */
void shipper_notify(shipper_t *s);

/**
 * Capture is over: wait for the running shipper, then run one last sweep.
 *
 * The final sweep makes sure the final burst -- and anything an earlier sweep
 * deferred -- is not stranded. This one is waited for.
 *
 * @param s Shipper.
 */
void shipper_finish(shipper_t *s);

#endif
