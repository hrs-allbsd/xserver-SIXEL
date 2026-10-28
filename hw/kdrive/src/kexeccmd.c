#include <dix-config.h>
#include "dix/dix_priv.h"
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include "kdrive.h"
#include "opaque.h"

extern char *kdExecuteCommand;
void KdExecuteChildCommand(void);

extern char **environ;

struct child_command {
    int logfd;
    char *command;
    char **env;
};

static void
free_child_command(struct child_command *child)
{
    if (child->env) {
        for (char **p = child->env; *p; p++)
            free(*p);
        free(child->env);
    }
    free(child->command);
    free(child);
}

static void *child_command(void *unused)
{
    struct child_command *child = unused;
    posix_spawn_file_actions_t actions;
    pid_t pid;
    int error, status;
    char *argv[] = { (char *) "sh", (char *) "-c", child->command, NULL };

    /* stdout carries SIXEL and stderr normally shares that terminal. Both
     * command streams, including diagnostics from the shell itself, must stay
     * in the private log. posix_spawn avoids changing the server's descriptors
     * or environment from this worker thread. */
    dprintf(child->logfd, "Starting child command: %s\n", child->command);
    error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, child->logfd, STDOUT_FILENO);
        if (!error)
            error = posix_spawn_file_actions_adddup2(&actions, child->logfd, STDERR_FILENO);
        if (!error)
            error = posix_spawn_file_actions_addclose(&actions, child->logfd);
        if (!error)
            error = posix_spawn(&pid, "/bin/sh", &actions, NULL, argv, child->env);
        posix_spawn_file_actions_destroy(&actions);
    }
    if (error) {
        dprintf(child->logfd, "Could not start child command: %s\n", strerror(error));
    } else {
        pid_t result;
        do {
            result = waitpid(pid, &status, 0);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            dprintf(child->logfd, "Could not wait for child command: %s\n", strerror(errno));
        else if (WIFEXITED(status))
            dprintf(child->logfd, "Child command exited with status %d\n", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            dprintf(child->logfd, "Child command terminated by signal %d\n", WTERMSIG(status));
    }
    close(child->logfd);
    free_child_command(child);
    return NULL;
}

void KdExecuteChildCommand(void)
{
    pthread_t thread_id;
    struct child_command *child;
    const char *tmpdir;
    char *path;
    size_t count = 0, n = 0;
    int error;

    if (!kdExecuteCommand)
        return;

    child = calloc(1, sizeof(*child));
    if (!child)
        FatalError("Xsixel: cannot allocate child command\n");
    child->command = strdup(kdExecuteCommand);
    while (environ[count])
        count++;
    child->env = calloc(count + 2, sizeof(*child->env));
    if (!child->command || !child->env)
        FatalError("Xsixel: cannot allocate child environment\n");
    for (size_t i = 0; i < count; i++) {
        if (!strncmp(environ[i], "DISPLAY=", 8))
            continue;
        child->env[n] = strdup(environ[i]);
        if (!child->env[n++])
            FatalError("Xsixel: cannot copy child environment\n");
    }
    if (asprintf(&child->env[n], "DISPLAY=:%s", display) < 0)
        FatalError("Xsixel: cannot set child display\n");

    tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    if (asprintf(&path, "%s/Xsixel-%s-exec-XXXXXX", tmpdir, display) < 0)
        FatalError("Xsixel: cannot allocate child log path\n");
    child->logfd = mkstemp(path);
    if (child->logfd < 0)
        FatalError("Xsixel: cannot create child log %s: %s\n", path, strerror(errno));
    free(path);
    /* Keep the log separate even if a standard descriptor was closed. */
    if (child->logfd <= STDERR_FILENO) {
        int fd = fcntl(child->logfd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        close(child->logfd);
        child->logfd = fd;
    } else if (fcntl(child->logfd, F_SETFD, FD_CLOEXEC) < 0) {
        FatalError("Xsixel: cannot protect child log descriptor\n");
    }
    if (child->logfd < 0)
        FatalError("Xsixel: cannot duplicate child log descriptor\n");
    error = pthread_create(&thread_id, NULL, child_command, child);
    if (error)
        FatalError("Xsixel: cannot start child command thread: %s\n", strerror(error));
    pthread_detach(thread_id);
}
