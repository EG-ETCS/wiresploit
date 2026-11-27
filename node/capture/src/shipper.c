#define _GNU_SOURCE
#include "shipper.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHIP_MAX_ARGS 32

extern char **environ;

static bool is_executable(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

/*
 * CMake copies sr2es.py next to pi5la in the build directory and installs it as
 * bin/sr2es, so look beside this binary first and fall back to PATH.
 */
static int resolve_script(shipper_t *s)
{
    char exe[400];              /* smaller than s->script, so appending fits */
    ssize_t n;
    char *slash;

    if (s->opts->sr2es_path) {
        if (!is_executable(s->opts->sr2es_path)) {
            fprintf(stderr, "shipper: %s is not executable\n", s->opts->sr2es_path);
            return -1;
        }
        snprintf(s->script, sizeof(s->script), "%s", s->opts->sr2es_path);
        return 0;
    }

    n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(s->script, sizeof(s->script), "%s/sr2es.py", exe);
            if (is_executable(s->script))
                return 0;
            snprintf(s->script, sizeof(s->script), "%s/sr2es", exe);
            if (is_executable(s->script))
                return 0;
        }
    }

    /* Let execvp search PATH. */
    snprintf(s->script, sizeof(s->script), "sr2es");
    return 0;
}

int shipper_init(shipper_t *s, const record_opts_t *opts, record_stats_t *stats,
                 unsigned base_gpio)
{
    memset(s, 0, sizeof(*s));
    s->opts = opts;
    s->base_gpio = base_gpio;
    if (stats) {
        s->spawned = &stats->shipped;
        s->deferred = &stats->ship_deferred;
    }

    if (!opts->es_url || !opts->es_url[0])
        return 0;
    if (!opts->monitor_dir) {
        fprintf(stderr, "shipper: live shipping needs --monitor\n");
        return -1;
    }
    if (resolve_script(s))
        return -1;

    snprintf(s->dir, sizeof(s->dir), "%s", opts->monitor_dir);
    s->enabled = true;
    fprintf(stderr, "shipping each burst with %s -> %s\n", s->script, opts->es_url);
    return 0;
}

/* Reaps the child if it has finished. Returns true when one is still running. */
static bool child_busy(shipper_t *s)
{
    int status;
    pid_t done;

    if (!s->child)
        return false;
    done = waitpid(s->child, &status, WNOHANG);
    if (done == 0)
        return true;
    if (done < 0 && errno != ECHILD)
        return true;
    if (done > 0 && WIFEXITED(status) && WEXITSTATUS(status))
        fprintf(stderr, "shipper: sr2es exited %d; the burst stays on disk for a retry\n",
                WEXITSTATUS(status));
    s->child = 0;
    return false;
}

static void add_arg(const char **argv, int *n, const char *a)
{
    if (*n < SHIP_MAX_ARGS - 1)
        argv[(*n)++] = a;
}

static void add_pair(const char **argv, int *n, const char *flag, const char *value)
{
    if (value && value[0]) {
        add_arg(argv, n, flag);
        add_arg(argv, n, value);
    }
}

static void spawn(shipper_t *s)
{
    const record_opts_t *o = s->opts;
    const char *argv[SHIP_MAX_ARGS];
    char channel[16], baud[16], gpio[16];
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t attr;
    pid_t pid;
    int n = 0, ret;

    argv[n++] = s->script;
    add_arg(argv, &n, "--ship");
    add_arg(argv, &n, s->dir);
    add_pair(argv, &n, "--protocol", o->protocol);
    add_pair(argv, &n, "--es-url", o->es_url);
    add_pair(argv, &n, "--index", o->es_index);
    add_pair(argv, &n, "--uart-type", o->uart_type);
    add_pair(argv, &n, "--port", o->port);
    snprintf(gpio, sizeof(gpio), "%u", s->base_gpio);
    add_pair(argv, &n, "--base-gpio", gpio);
    if (o->decode_channel >= 0) {
        snprintf(channel, sizeof(channel), "%d", o->decode_channel);
        add_pair(argv, &n, "--channel", channel);
    }
    if (o->baud > 0) {
        snprintf(baud, sizeof(baud), "%u", o->baud);
        add_pair(argv, &n, "--baud", baud);
    }
    if (o->insecure)
        add_arg(argv, &n, "--insecure");
    argv[n] = NULL;

    /*
     * Credentials are never passed on the command line: ES_API_KEY, or ES_USER
     * and ES_PASSWORD, are inherited from the environment.
     */
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_SETSID
    /* Its own session, so Ctrl-C on pi5la cannot kill an upload in flight. */
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#endif
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 34)
    /* Don't hand /dev/pio0 to a child that outlives us. */
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
#endif
#endif

    ret = posix_spawnp(&pid, s->script, &fa, &attr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);

    if (ret) {
        fprintf(stderr, "shipper: cannot run %s: %s\n", s->script, strerror(ret));
        return;
    }
    s->child = pid;
    if (s->spawned)
        (*s->spawned)++;
}

void shipper_notify(shipper_t *s)
{
    if (!s->enabled)
        return;
    if (child_busy(s)) {
        /* Backpressure: leave it on disk and let the running sweep, or the
           next one, find it. */
        if (s->deferred)
            (*s->deferred)++;
        return;
    }
    spawn(s);
}

void shipper_finish(shipper_t *s)
{
    int status;

    if (!s->enabled)
        return;
    if (s->child) {
        waitpid(s->child, &status, 0);
        s->child = 0;
    }
    /* One last sweep, waited for: the burst that closed at teardown has not
       been shipped by anyone yet. */
    spawn(s);
    if (s->child) {
        waitpid(s->child, &status, 0);
        s->child = 0;
    }
}
