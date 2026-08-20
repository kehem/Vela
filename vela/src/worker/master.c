#include "vela/worker.h"
#include "vela/server.h"
#include "vela/python.h"
#include "vela/log.h"
#include "vela/util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_term;
static volatile sig_atomic_t g_hup;
static volatile sig_atomic_t g_usr1;
static my_server_t *g_srv;

static void on_sig(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        g_term = 1;
    else if (sig == SIGHUP)
        g_hup = 1;
    else if (sig == SIGUSR1)
        g_usr1 = 1;
    if (g_srv)
        my_server_stop(g_srv);
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

static int drop_privs(const my_config_t *cfg)
{
    if (cfg->group) {
        struct group *gr = getgrnam(cfg->group);
        if (!gr) {
            MY_ERROR("unknown group %s", cfg->group);
            return -1;
        }
        if (setgid(gr->gr_gid) < 0)
            return -1;
    }
    if (cfg->user) {
        struct passwd *pw = getpwnam(cfg->user);
        if (!pw) {
            MY_ERROR("unknown user %s", cfg->user);
            return -1;
        }
        if (setuid(pw->pw_uid) < 0)
            return -1;
    }
    return 0;
}

static int write_pid(const char *path)
{
    if (!path)
        return 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    char b[32];
    int n = snprintf(b, sizeof b, "%d\n", (int)getpid());
    (void)write(fd, b, (size_t)n);
    close(fd);
    return 0;
}

static void purge_pyc(const char *dir, int depth)
{
    if (depth > 6)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (!strcmp(de->d_name, "__pycache__")) {
                DIR *p = opendir(path);
                if (p) {
                    struct dirent *pe;
                    while ((pe = readdir(p))) {
                        if (pe->d_name[0] == '.')
                            continue;
                        char fp[PATH_MAX];
                        snprintf(fp, sizeof fp, "%s/%s", path, pe->d_name);
                        unlink(fp);
                    }
                    closedir(p);
                }
            } else if (strcmp(de->d_name, "build") && strcmp(de->d_name, "bin")) {
                purge_pyc(path, depth + 1);
            }
        }
    }
    closedir(d);
}

static int worker_main(my_config_t *cfg)
{
    my_log_init((my_log_level_t)cfg->log_level, cfg->error_log, cfg->access_log);
    MY_INFO("worker started pid=%d", (int)getpid());
    if (cfg->chdir && chdir(cfg->chdir) < 0) {
        MY_ERROR("chdir: %s", strerror(errno));
        return 1;
    }
    setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
    purge_pyc(".", 0);
    if (my_py_init(cfg) < 0) {
        MY_ERROR("failed to initialize CPython");
        return 1;
    }
    if (cfg->app) {
        if (my_py_load_app(cfg->app) < 0) {
            MY_ERROR("failed to load %s", cfg->app);
            my_py_fini();
            return 1;
        }
    }
    my_server_t srv;
    if (my_server_init(&srv, cfg) < 0)
        return 1;
    g_srv = &srv;
    if (my_server_listen(&srv) < 0) {
        my_server_fini(&srv);
        my_py_fini();
        return 1;
    }
    if (drop_privs(cfg) < 0)
        return 1;
    int rc = my_server_run(&srv);
    MY_INFO("worker shutting down");
    my_server_fini(&srv);
    my_py_fini();
    return rc == 0 ? 0 : 1;
}

static pid_t spawn_worker(my_config_t *cfg)
{
    pid_t pid = fork();
    if (pid < 0) {
        MY_ERROR("fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0)
        _exit(worker_main(cfg));
    MY_INFO("spawned worker pid=%d", (int)pid);
    return pid;
}

#define WATCH_MAX 4096

typedef struct {
    char path[PATH_MAX];
    time_t sec;
    long nsec;
} watch_ent;

static watch_ent g_watch[WATCH_MAX];
static int g_nwatch;

static int path_is_py(const char *name)
{
    size_t n = strlen(name);
    return n > 3 && !strcmp(name + n - 3, ".py");
}

static void watch_add(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return;
    if (g_nwatch >= WATCH_MAX)
        return;
    snprintf(g_watch[g_nwatch].path, sizeof g_watch[g_nwatch].path, "%s", path);
    g_watch[g_nwatch].sec = st.st_mtim.tv_sec;
    g_watch[g_nwatch].nsec = st.st_mtim.tv_nsec;
    g_nwatch++;
}

static void watch_dir(const char *dir, int depth)
{
    if (depth > 6 || g_nwatch >= WATCH_MAX)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        if (!strcmp(de->d_name, "__pycache__") || !strcmp(de->d_name, "build") ||
            !strcmp(de->d_name, "bin") || !strcmp(de->d_name, ".git"))
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode))
            watch_dir(path, depth + 1);
        else if (S_ISREG(st.st_mode) && path_is_py(de->d_name))
            watch_add(path);
    }
    closedir(d);
}

static void watch_init(const char *root)
{
    g_nwatch = 0;
    watch_dir(root && root[0] ? root : ".", 0);
    MY_INFO("reload watching %d Python files under %s", g_nwatch, root && root[0] ? root : ".");
}

static int watch_changed(void)
{
    int changed = 0;
    for (int i = 0; i < g_nwatch; i++) {
        struct stat st;
        if (stat(g_watch[i].path, &st) < 0)
            continue;
        if (st.st_mtim.tv_sec != g_watch[i].sec || st.st_mtim.tv_nsec != g_watch[i].nsec) {
            MY_INFO("reload: %s changed", g_watch[i].path);
            g_watch[i].sec = st.st_mtim.tv_sec;
            g_watch[i].nsec = st.st_mtim.tv_nsec;
            changed = 1;
        }
    }
    return changed;
}

static void rolling_restart(my_config_t *cfg, pid_t *kids, int n)
{
    MY_INFO("rolling restart of %d worker(s)", n);
    for (int i = 0; i < n; i++) {
        pid_t old = kids[i];
        pid_t np = spawn_worker(cfg);
        if (np > 0)
            kids[i] = np;
        if (old > 0) {
            kill(old, SIGTERM);
            int waited = 0;
            while (waited < cfg->graceful_timeout_s * 5) {
                int st;
                pid_t r = waitpid(old, &st, WNOHANG);
                if (r == old)
                    break;
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000000L};
                nanosleep(&ts, NULL);
                waited++;
            }
            waitpid(old, NULL, WNOHANG);
        }
    }
}

int my_master_run(my_config_t *cfg)
{
    install_signals();
    if (cfg->daemon) {
        pid_t p = fork();
        if (p < 0)
            return 1;
        if (p > 0)
            _exit(0);
        setsid();
        p = fork();
        if (p < 0)
            return 1;
        if (p > 0)
            _exit(0);
        int n = open("/dev/null", O_RDWR);
        if (n >= 0) {
            dup2(n, 0);
            dup2(n, 1);
            dup2(n, 2);
            if (n > 2)
                close(n);
        }
    }
    cfg->reuseport = 1;
    if (cfg->workers < 1)
        cfg->workers = 1;
    write_pid(cfg->pid_file);
    my_log_init((my_log_level_t)cfg->log_level, cfg->error_log, cfg->access_log);
    MY_INFO("vela master pid=%d workers=%d app=%s reload=%s", (int)getpid(), cfg->workers,
            cfg->app ? cfg->app : "(none)", cfg->reload ? "on" : "off");
    if (cfg->reload) {
        MY_WARN("--reload is for development only");
        watch_init(cfg->chdir);
    }

    pid_t *kids = calloc((size_t)cfg->workers, sizeof *kids);
    if (!kids)
        return 1;
    for (int i = 0; i < cfg->workers; i++) {
        pid_t pid = spawn_worker(cfg);
        if (pid < 0)
            return 1;
        kids[i] = pid;
    }

    while (!g_term) {
        int st = 0;
        pid_t p = waitpid(-1, &st, WNOHANG);
        if (p > 0 && !g_term) {
            MY_WARN("worker %d exited status=%d — restarting", (int)p, st);
            for (int i = 0; i < cfg->workers; i++) {
                if (kids[i] == p) {
                    pid_t np = spawn_worker(cfg);
                    if (np > 0)
                        kids[i] = np;
                }
            }
        }
        if (g_hup) {
            g_hup = 0;
            if (cfg->reload)
                watch_init(cfg->chdir);
            rolling_restart(cfg, kids, cfg->workers);
        }
        if (cfg->reload && watch_changed())
            g_hup = 1;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000000L};
        nanosleep(&ts, NULL);
    }
    MY_INFO("master shutting down workers");
    for (int i = 0; i < cfg->workers; i++)
        if (kids[i] > 0)
            kill(kids[i], SIGTERM);
    int waited = 0;
    while (waited < cfg->graceful_timeout_s * 5) {
        int live = 0;
        for (int i = 0; i < cfg->workers; i++) {
            if (kids[i] > 0) {
                int wst;
                pid_t r = waitpid(kids[i], &wst, WNOHANG);
                if (r == kids[i])
                    kids[i] = 0;
                else
                    live++;
            }
        }
        if (!live)
            break;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000000L};
        nanosleep(&ts, NULL);
        waited++;
    }
    for (int i = 0; i < cfg->workers; i++)
        if (kids[i] > 0)
            kill(kids[i], SIGKILL);
    free(kids);
    if (cfg->pid_file)
        unlink(cfg->pid_file);
    return 0;
}
