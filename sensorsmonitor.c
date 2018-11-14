#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#define EXIT_NO_XDG_RUNTIME_DIR 2
#define EXIT_FAIL_DELETE_EXISTING_PIPE 3
#define EXIT_FAIL_CREATE_EXISTING_PIPE 4
#define EXIT_FAIL_OPEN_PIPE_FOR_WRITING 5

#define PIPE_NAME "sensorsmonitor"
#define POLLING_INTERVAL_SEC 5

// if fail, print the message to stderr and exit
#define CHECK_AND_EXIT(fail, exitCode, format, ...) \
if (fail) { \
    fprintf(stderr, format"; errno=%i, exiting %i\n", __VA_ARGS__, errno, exitCode); \
    exit(exitCode); \
}

// use an existing pipe or create a new pipe at $XDG_RUNTIME_DIR/PIPE_NAME
// returns full path of this pipe, which may be free'd
char *initPipe() {

    // read the environment variable
    const char *xdgRuntimeDir = getenv("XDG_RUNTIME_DIR");
    CHECK_AND_EXIT(xdgRuntimeDir == NULL, EXIT_NO_XDG_RUNTIME_DIR, "%s not set", "$XDG_RUNTIME_DIR")

    // create the path
    char *pipePath = malloc(sizeof(char) * (strlen(xdgRuntimeDir) + strlen(PIPE_NAME) + 2));
    sprintf(pipePath, "%s/%s", xdgRuntimeDir, PIPE_NAME);

    // stat the pipe
    struct stat pipeStat;
    int statRc = stat(pipePath, &pipeStat);

    if (statRc == 0) {
        if (S_ISFIFO(pipeStat.st_mode)) {
            // just use the existing pipe, so as not to break any consumers
            return pipePath;
        } else {
            // clean whatever it is
            CHECK_AND_EXIT(remove(pipePath) != 0, EXIT_FAIL_DELETE_EXISTING_PIPE, "failed to remove unexpected file '%s'", pipePath)
        }
    }

    // create the pipe
    CHECK_AND_EXIT(mkfifo(pipePath, 0644), EXIT_FAIL_CREATE_EXISTING_PIPE, "failed to create named pipe '%s'", pipePath)

    return pipePath;
}

void discover() {

}

void collect(const int pd) {

    char buf[100];
    sprintf(buf, "blarg\n");
    write(pd, buf, strlen(buf) * sizeof(char));
}

int main() {
    int pd;

    discover();

    // create the pipe
    const char *pipePath = initPipe();

    while(true) {

        // open pipe write only; will block until a reader comes along
        CHECK_AND_EXIT((pd = open(pipePath, O_WRONLY)) == -1, EXIT_FAIL_OPEN_PIPE_FOR_WRITING, "failed to open %s for write, exiting", pipePath)

        // output data
        collect(pd);

        // close pipe
        close(pd);

        sleep(POLLING_INTERVAL_SEC);
    }
}
