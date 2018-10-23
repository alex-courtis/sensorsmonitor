#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <sensors/sensors.h>
#include <sensors/error.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

#define WATCH_MAX 8

#define PIPE "/home/alex/piping"

typedef struct watch {
    const sensors_chip_name *chip;
    const sensors_subfeature *sub;
    const char *label;
} watch;

void cleanup() {
    printf("removing %s\n", PIPE);
    int rc = remove(PIPE);
    if (rc) {
        printf("failed to remove %s with rc=%i\n", PIPE, rc);
        exit(1);
    }
}

void signalHandler(int signum) {
    printf("caught signal %i\n", signum);
    cleanup();
    exit(0);
}

int main() {
    watch **watching = malloc(sizeof(watch *) * WATCH_MAX);
    int watchingCount = 0;
    int rc;

    rc = sensors_init(NULL);
    if (rc) {
        fprintf(stderr, "sensors_init: %s\n", sensors_strerror(rc));
        exit(1);
    }

    const sensors_chip_name *chip;
    int chip_nr;

    chip_nr = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_nr))) {
        printf("%i %s %s\n", chip_nr, chip->prefix, chip->path);
        const char *adapter = sensors_get_adapter_name(&chip->bus);
        if (adapter) {
            printf("  adapter=%s\n", adapter);
        } else {
            fprintf(stderr, "Can't get adapter name\n");
            exit(1);
        }

        int a = 0;
        const sensors_feature *feature;
        char *label;
        while ((feature = sensors_get_features(chip, &a))) {
            if (!(label = sensors_get_label(chip, feature))) {
                fprintf(stderr, "ERROR: Can't get label of feature "
                                "%s!\n", feature->name);
                continue;
            }
            printf("    label=%s\n", label);

            int b = 0;
            const sensors_subfeature *sub;
            double val;
            while ((sub = sensors_get_all_subfeatures(chip, feature, &b))) {
                if (sub->flags & SENSORS_MODE_R) {
                    if ((rc = sensors_get_value(chip, sub->number, &val))) {
                        fprintf(stderr, "ERROR: Can't get value of subfeature %s: %s\n", sub->name,
                                sensors_strerror(rc));
                        exit(1);
                    }
                    printf("        %s=%f\n", sub->name, val);

                    if (sub->type == SENSORS_SUBFEATURE_TEMP_INPUT && watchingCount < WATCH_MAX) {
                        watching[watchingCount] = malloc(sizeof(watch));
                        watching[watchingCount]->chip = chip;
                        watching[watchingCount]->sub = sub;
                        watching[watchingCount]->label = label;
                        watchingCount++;
                    }
                }
            }
        }
    }
    printf("\n");

//    signal(SIGINT | SIGTERM, signalHandler);

    if (watching) {
        struct stat bleh;
        rc = stat(PIPE, &bleh);
        if (rc == 0 && S_ISFIFO(bleh.st_mode)) {
            // use the existing pipe
            printf("found existing pipe %s\n", PIPE);
        } else {
            if (rc == 0) {
                cleanup();
            }
            rc = mkfifo(PIPE, 0644);
            printf("mkfifo %s rc=%i\n", PIPE, rc);
        }

        const watch *w;
        char buf[1024];
        while (true) {
            int pd;
            // open write only; will block until a reader comes along
            if ((pd = open(PIPE, O_WRONLY)) < 0) {
                fprintf(stderr, "failed to open %s for write, exiting", PIPE);
                exit(1);
            }

            for (int i = 0; i < watchingCount; i++) {
                w = watching[i];
                double val;
                if ((rc = sensors_get_value(w->chip, w->sub->number, &val))) {
                    fprintf(stderr, "ERROR: Can't get value of subfeature %s: %s\n", w->sub->name,
                            sensors_strerror(rc));
                    exit(1);
                }
                sprintf(buf, "%s:%s=%.0f ", w->chip->prefix, w->label, val + 0.5);
                printf("%s\n", buf);
                write(pd, buf, strlen(buf) * sizeof(char));
            }
            write(pd, "\n", sizeof(char));
            close(pd);
            printf("\n");

            sleep(5);
        }
    } else {
        fprintf(stderr, "ERROR: nothing found to monitor");
        exit(1);
    }
}
