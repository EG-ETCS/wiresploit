#ifndef PI5LA_SHIPPER_H
#define PI5LA_SHIPPER_H

#include <stdbool.h>
#include <sys/types.h>

#include "record.h"

/*
 * Handing finished bursts to tools/sr2es.py while the capture keeps running.
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
typedef struct {
    bool enabled;               /* false when no --es-url was given */
    const record_opts_t *opts;
    char script[512];           /* resolved sr2es.py */
    char dir[512];              /* the monitor directory */
    unsigned base_gpio;         /* GPIO of channel 0, so sr2es names probes right */
    pid_t child;                /* 0 when none is running */
    unsigned *spawned;          /* stats counters, may be NULL */
    unsigned *deferred;
} shipper_t;

/*
 * Resolves sr2es.py and validates the options. A missing script is an error
 * here, at startup, rather than a silent failure once per burst.
 *
 * With opts->es_url unset this disables the shipper and returns 0, so callers
 * need no special case.
 */
int shipper_init(shipper_t *s, const record_opts_t *opts, record_stats_t *stats,
                 unsigned base_gpio);

/* A burst just closed. Never blocks; failures warn and are not fatal. */
void shipper_notify(shipper_t *s);

/*
 * Capture is over: wait for the running shipper, then run one last sweep so
 * the final burst -- and anything an earlier sweep deferred -- is not
 * stranded. This one is waited for.
 */
void shipper_finish(shipper_t *s);

#endif
