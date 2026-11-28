#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zip.h>

#include "check.h"
#include "srwriter.h"

static char *read_member(zip_t *za, const char *name, zip_uint64_t *len)
{
    zip_stat_t st;
    zip_file_t *zf;
    char *buf;

    if (zip_stat(za, name, 0, &st) < 0)
        return NULL;
    buf = calloc(1, st.size + 1);
    zf = zip_fopen(za, name, 0);
    if (!buf || !zf || zip_fread(zf, buf, st.size) != (zip_int64_t)st.size) {
        free(buf);
        if (zf)
            zip_fclose(zf);
        return NULL;
    }
    zip_fclose(zf);
    *len = st.size;
    return buf;
}

int main(void)
{
    static const char *const names[2] = { "GPIO2", "GPIO3" };
    char path[] = "/tmp/pi5la_srw_XXXXXX";
    char payload[sizeof(path) + 8];
    uint8_t chunk[256];
    zip_uint64_t len;
    srwriter_t w;
    zip_t *za;
    char *s;
    int fd, err;
    size_t i;

    for (i = 0; i < sizeof(chunk); i++)
        chunk[i] = (uint8_t)i;

    fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    snprintf(payload, sizeof(payload), "%s.logic", path);

    /* Several appends, the way a capture writes one DMA period at a time. */
    CHECK(srwriter_open(&w, path) == 0);
    CHECK(access(payload, F_OK) == 0);       /* payload exists while capturing */
    for (i = 0; i < 4; i++)
        CHECK(srwriter_write(&w, chunk, sizeof(chunk)) == 0);
    CHECK(w.samples == 4 * sizeof(chunk));

    /* Trailing idle is dropped by truncating, as a burst does. */
    CHECK(srwriter_truncate(&w, 900) == 0);
    CHECK(w.samples == 900);
    CHECK(srwriter_truncate(&w, 5000) == 0); /* past the end: no change */
    CHECK(w.samples == 900);

    CHECK(srwriter_finish(&w, 2000000, 2, names) == 0);
    CHECK(access(payload, F_OK) != 0);       /* payload removed afterwards */

    za = zip_open(path, ZIP_RDONLY, &err);
    CHECK(za != NULL);
    if (za) {
        s = read_member(za, "version", &len);
        CHECK(s && len == 1 && s[0] == '2');
        free(s);

        s = read_member(za, "metadata", &len);
        CHECK(s != NULL);
        if (s) {
            CHECK(strstr(s, "samplerate=2 MHz\n") != NULL);
            CHECK(strstr(s, "total probes=2\n") != NULL);
            CHECK(strstr(s, "probe2=GPIO3\n") != NULL);
        }
        free(s);

        s = read_member(za, "logic-1", &len);
        CHECK(s && len == 900);
        if (s && len == 900) {
            CHECK(!memcmp(s, chunk, sizeof(chunk)));                     /* first append */
            CHECK((uint8_t)s[899] == (uint8_t)((900 - 1) % 256));        /* up to the cut */
        }
        free(s);
        zip_close(za);
    }
    unlink(path);

    /* abort() leaves no payload and no .sr. */
    CHECK(srwriter_open(&w, path) == 0);
    CHECK(srwriter_write(&w, chunk, sizeof(chunk)) == 0);
    srwriter_abort(&w);
    CHECK(access(payload, F_OK) != 0);
    unlink(path);

    CHECK_DONE();
}
