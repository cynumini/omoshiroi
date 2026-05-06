// #define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

int main(int argc, char **argv)
{
    sakana_build(argc, argv);
    Cmd build = {0};
    cmd_append(&build, "gcc");
    cmd_append(&build, "main.c");
    cmd_append(&build, "-o");
    cmd_append(&build, "memorable");
    if (run_cmd(&build) == 0) {
        Cmd run = {0};
        cmd_append(&run, "./memorable");
        run_cmd(&run);
    }
    return 0;
}
