#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sensors/sensors.h>
#include <sensors/error.h>

#define EXIT_NO_XDG_RUNTIME_DIR 2
#define EXIT_FAIL_DELETE_EXISTING_PIPE 3
#define EXIT_FAIL_CREATE_EXISTING_PIPE 4
#define EXIT_FAIL_OPEN_PIPE_FOR_WRITING 5
#define EXIT_FAIL_SENSORS_INIT 6
#define EXIT_FAIL_SENSORS_GET_LABEL 7
#define EXIT_FAIL_SENSORS_GET_VALUE 8

#define PIPE_NAME "sensorsmonitor"

#define POLLING_INTERVAL_SEC 5

#define PREFIX_K10_TEMP "k10temp"
#define PREFIX_AMDGPU "amdgpu"

typedef struct {
    double tempInput;
    double powerAverage;
} Amdgpu;

#define LABEL_TDIE "Tdie"
typedef struct {
    double tdie; // only Tdie is correct; Tctl is offset by +27 and exists only for legacy purposes
} K10temp;

#define MAX_AMDGPUS 4
#define MAX_K10_TEMPS 4
typedef struct {
    Amdgpu amdgpus[MAX_AMDGPUS];
    int numAmdgpus;
    K10temp k10temps[MAX_K10_TEMPS];
    int numk10temps;
} Stats;

// if fail, print the message to stderr, with errno appended and exit
#define CHECK_AND_EXIT(fail, exitCode, format, ...) \
if (fail) { \
    fprintf(stderr, format"; errno=%i, exiting %i\n", __VA_ARGS__, errno, exitCode); \
    exit(exitCode); \
}

// if rc != 0, print the message to stderr, with sensors_strerror(rc) appended and exit
#define CHECK_AND_EXIT_SENSORS(rc, exitCode, format, ...) \
if (rc != 0) { \
    fprintf(stderr, format"; '%s', exiting %i\n", __VA_ARGS__, sensors_strerror(rc), exitCode); \
    exit(exitCode); \
}

// use an existing pipe or create a new pipe at $XDG_RUNTIME_DIR/PIPE_NAME
// returns full path of this pipe, which may be free'd
char *initPipe() {

    // read the environment variable
    const char *xdgRuntimeDir = getenv("XDG_RUNTIME_DIR");
    CHECK_AND_EXIT(
            xdgRuntimeDir == NULL,
            EXIT_NO_XDG_RUNTIME_DIR, "%s not set", "$XDG_RUNTIME_DIR"
    )

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
            CHECK_AND_EXIT(
                    remove(pipePath) != 0,
                    EXIT_FAIL_DELETE_EXISTING_PIPE, "failed to remove unexpected file '%s'", pipePath
            )
        }
    }

    // create the pipe
    CHECK_AND_EXIT(
            mkfifo(pipePath, 0644),
            EXIT_FAIL_CREATE_EXISTING_PIPE, "failed to create named pipe '%s'", pipePath
    )

    return pipePath;
}

// discover and collect interesting sensor stats
// pointer to static is returned; do not free
const Stats* collect() {
    static Stats stats;

    static const sensors_chip_name *chip_name;
    static int chip_nr;
    static const sensors_feature *feature;
    static int feature_nr;
    static const sensors_subfeature *subfeature;
    static int subfeature_nr;
    static const char *label;
    static double value;
    static Amdgpu *amdgpu;
    static K10temp *k10temp;

    stats.numAmdgpus = 0;
    stats.numk10temps = 0;

    // init; clean up is done at end
    CHECK_AND_EXIT_SENSORS(
            sensors_init(NULL),
            EXIT_FAIL_SENSORS_INIT, "failed %s", "sensors_init"
    )

    // iterate chips
    chip_nr = 0;
    while (chip_name = sensors_get_detected_chips(NULL, &chip_nr)) {
        amdgpu = NULL;
        k10temp = NULL;

        // only interested in known chips
        if (strcmp(chip_name->prefix, PREFIX_AMDGPU) == 0) {
            if (stats.numAmdgpus >= MAX_AMDGPUS) {
                continue;
            }
            amdgpu = &(stats.amdgpus[stats.numAmdgpus++]);
        } else if (strcmp(chip_name->prefix, PREFIX_K10_TEMP) == 0) {
            if (stats.numk10temps >= MAX_K10_TEMPS) {
                continue;
            }
            k10temp = &(stats.k10temps[stats.numk10temps++]);
        } else {
            continue;
        }

        // iterate features
        feature_nr = 0;
        while (feature = sensors_get_features(chip_name, &feature_nr)) {

            // read the label
            CHECK_AND_EXIT_SENSORS(
                    (label = sensors_get_label(chip_name, feature)) == NULL,
                    EXIT_FAIL_SENSORS_GET_LABEL, "failed sensors_get_label for '%s:%s'", chip_name->prefix, feature->name
            )

            // iterate readable sub-features
            subfeature_nr = 0;
            while (subfeature = sensors_get_all_subfeatures(chip_name, feature, &subfeature_nr)) {
                if (!(subfeature->flags & SENSORS_MODE_R)) {
                    continue;
                }

                // read the value
                CHECK_AND_EXIT_SENSORS(
                        sensors_get_value(chip_name, subfeature->number, &value) != 0,
                        EXIT_FAIL_SENSORS_GET_VALUE, "failed sensors_get_value for '%s:%s'", chip_name->prefix, subfeature->name
                )

                if (amdgpu) {
                    switch (subfeature->type) {
                        case SENSORS_SUBFEATURE_TEMP_INPUT:
                            amdgpu->tempInput = value;
                            break;
                        case SENSORS_SUBFEATURE_POWER_AVERAGE:
                            amdgpu->powerAverage = value;
                            break;
                        default:
                            break;
                    }
                } else if (k10temp) {
                    switch (subfeature->type) {
                        case SENSORS_SUBFEATURE_TEMP_INPUT:
                            if (strcmp(label, LABEL_TDIE) == 0) {
                                k10temp->tdie = value;
                            }
                        default:
                            break;
                    }
                }
            }
        }
    }

    // promises not to error
    sensors_cleanup();

    return &stats;
}

// render average stats as a string with a trailing newline
// static buffer is returned, do not free
const char *render(const Stats *stats) {
    static char buf[128]; // ensure that this is large enough for all the sprintfs with maxints

    static char *bufPtr;

    bufPtr = buf;
    if (stats) {
        if (stats->numAmdgpus > 0) {
            double tempInput = 0.5;
            double powerAverage = 0.5;
            for (int i = 0; i < stats->numAmdgpus; i++) {
                tempInput += stats->amdgpus[i].tempInput;
                powerAverage += stats->amdgpus[i].powerAverage;
            }
            tempInput /= stats->numAmdgpus;
            powerAverage /= stats->numAmdgpus;
            bufPtr += sprintf(bufPtr, "amdgpu %i°C %iW", (int) tempInput, (int) powerAverage);
        }

        if (stats->numk10temps > 0) {
            double tdie = 0.5;
            for (int i = 0; i < stats->numk10temps; i++) {
                tdie += stats->k10temps[i].tdie;
            }
            tdie /= stats->numk10temps;
            bufPtr += sprintf(bufPtr, "%s%s %i°C", bufPtr == buf ? "" : "   ", LABEL_TDIE, (int) tdie);
        }
    }

    sprintf(bufPtr, "\n");

    return buf;
}

int main() {

    // collect once, to detect issues
    collect();

    // create the pipe
    const char *pipePath = initPipe();

    while (true) {

        // open pipe write only; will block until a reader comes along
        const int pd = open(pipePath, O_WRONLY);
        CHECK_AND_EXIT(pd == -1,
                       EXIT_FAIL_OPEN_PIPE_FOR_WRITING, "failed to open %s for write, exiting", pipePath)

        // collect
        const Stats *stats = collect();

        // render
        const char *rendered = render(stats);

        // write
        write(pd, rendered, strlen(rendered) * sizeof(char));

        // close pipe
        close(pd);

        sleep(POLLING_INTERVAL_SEC);
    }
}
