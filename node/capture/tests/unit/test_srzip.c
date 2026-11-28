#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zip.h>

#include "check.h"
#include "srzip.h"

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
    static const char *const names[8] = { "GPIO2", "GPIO3", "GPIO4", "GPIO5",
                                          "GPIO6", "GPIO7", "GPIO8", "GPIO9" };
    char path[] = "/tmp/pi5la_test_XXXXXX";
    char rate[32], *s;
    uint8_t samples[1000];
    zip_uint64_t len;
    zip_t *za;
    int fd, err;
    size_t i;

    srzip_format_samplerate(10000000, rate, sizeof(rate));
    CHECK(!strcmp(rate, "10 MHz"));
    srzip_format_samplerate(115200, rate, sizeof(rate));
    CHECK(!strcmp(rate, "115200 Hz"));
    srzip_format_samplerate(3000, rate, sizeof(rate));
    CHECK(!strcmp(rate, "3 kHz"));
    srzip_format_samplerate(66666667, rate, sizeof(rate));
    CHECK(!strcmp(rate, "66666667 Hz"));

    for (i = 0; i < sizeof(samples); i++)
        samples[i] = (uint8_t)(i * 7);

    fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    CHECK(srzip_write(path, samples, sizeof(samples), 10000000, 8, names) == 0);

    za = zip_open(path, ZIP_RDONLY, &err);
    CHECK(za != NULL);
    if (za) {
        s = read_member(za, "version", &len);
        CHECK(s && len == 1 && s[0] == '2');
        free(s);

        s = read_member(za, "metadata", &len);
        CHECK(s != NULL);
        if (s) {
            CHECK(strstr(s, "[global]\n") != NULL);
            CHECK(strstr(s, "[device 1]\n") != NULL);
            CHECK(strstr(s, "capturefile=logic-1\n") != NULL);
            CHECK(strstr(s, "total probes=8\n") != NULL);
            CHECK(strstr(s, "samplerate=10 MHz\n") != NULL);
            CHECK(strstr(s, "unitsize=1\n") != NULL);
            CHECK(strstr(s, "probe1=GPIO2\n") != NULL);
            CHECK(strstr(s, "probe8=GPIO9\n") != NULL);
            CHECK(strstr(s, "probe9=") == NULL);
        }
        free(s);

        s = read_member(za, "logic-1", &len);
        CHECK(s && len == sizeof(samples) && !memcmp(s, samples, sizeof(samples)));
        free(s);
        zip_close(za);
    }
    unlink(path);

    CHECK_DONE();
}
