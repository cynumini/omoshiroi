// #define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

char *name = "./omoshiroi";

int main(int argc, char **argv)
{
    sakana_build(argc, argv);
    Cmd build = {0};
    cmd_append(&build, "gcc");
    cmd_append(&build, "main.c");
    cmd_append(&build, "-Wall");
    cmd_append(&build, "-Wextra");
    cmd_append(&build, "-Wpedantic");
    cmd_append(&build, "-Wconversion");
    cmd_append(&build, "-lsqlite3");
    cmd_append(&build, "-lreadline");
    cmd_append(&build, "-fsanitize=address");
    cmd_append(&build, "-o");
    cmd_append(&build, name);
    if (run_cmd(&build) == 0) {
        Cmd run = {0};
        cmd_append(&run, name);
        run_cmd(&run);
    }
    return 0;
}
