#ifndef SAKANA
#define SAKANA

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t position;
    size_t next_position;
    size_t size;
    uint8_t *data;
} Arena;

#ifdef DEBUG
#include <stdio.h>
#define LOG(FMT, ...) printf(FMT, __VA_ARGS__)
#else
#define LOG(FMT, ...)
#endif // DEBUG

Arena alloc_arena(size_t size);
Arena create_arena(void *ptr, size_t size);
void delete_arena(Arena arena);
void *arena_base_alloc(Arena *arena, size_t size, size_t align);
void *arena_base_realloc(Arena *arena, void *ptr, size_t old_len, size_t new_len, size_t align);
#define arena_create(ARENA, TYPE) arena_base_alloc(ARENA, sizeof(TYPE), alignof(TYPE))
#define arena_alloc(ARENA, TYPE, LEN) arena_base_alloc(ARENA, LEN * sizeof(TYPE), alignof(TYPE))
#define arena_realloc(ARENA, PTR, OLD_LEN, NEW_LEN) arena_base_realloc(ARENA, PTR,                       \
                                                                       OLD_LEN * sizeof(typeof(PTR[0])), \
                                                                       NEW_LEN * sizeof(typeof(PTR[0])), \
                                                                       alignof(typeof(PTR[0])))

#define KB(SIZE) (SIZE << 10)
#define MB(SIZE) (KB(SIZE) << 10)

#define define_da(type, name) typedef struct { \
    type *items;                               \
    size_t len;                                \
    size_t capacity;                           \
} name                                         \

#define da_append(arena, array, item) do {                                                      \
    if ((array)->len >= (array)->capacity) {                                                    \
        size_t old_capacity = (array)->capacity;                                                \
        if ((array)->capacity == 0) (array)->capacity = 1;                                      \
        (array)->capacity *= 2;                                                                 \
        (array)->items = arena_realloc(arena, (array)->items, old_capacity, (array)->capacity); \
    }                                                                                           \
    (array)->items[(array)->len] = item;                                                        \
    (array)->len++;                                                                             \
} while(0)

#define foreach(type, item, da) for (type *item = (da)->items; item < (da)->items + (da)->len; ++item)

define_da(char *, Cmd);
int run_cmd(Cmd *cmd);
void cmd_append(Cmd *cmd, char *string);

void sakana_build_base(int argc, char **argv, char *src_path);
#define sakana_build(argc, argv) sakana_build_base(argc, argv, __FILE__)

#ifdef SAKANA_IMPLEMENTATION

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static Arena sakana_arena;

Arena alloc_arena(size_t size)
{
    return create_arena(malloc(size), size);
}

Arena create_arena(void *ptr, size_t size)
{
    return (Arena){ .size = size, .data = ptr };
}

void delete_arena(Arena arena)
{
    free(arena.data);
}

void *arena_base_alloc(Arena *arena, size_t size, size_t align)
{
    size_t shift = 0;
    size_t mod = arena->next_position % align;
    if (mod != 0) shift = align - mod;
    arena->position =  arena->next_position + shift;
    assert(arena->position >= arena->next_position);
    assert((arena->position % align) == 0);
    arena->next_position = arena->position + size;
    assert(arena->next_position <= arena->size);
    memset(arena->data + arena->position, 0, size);
    LOG("arena_base_alloc: size = %li, from position = %li, to position = %li, used = %ld%%\n",
        size, arena->position, arena->next_position,
        ((arena->next_position * 100) / arena->size));
    return arena->data + arena->position;
}

void *arena_base_realloc(Arena *arena, void *ptr, size_t old_len, size_t new_len, size_t align)
{
    void *new_ptr = ptr;
    if (ptr == (arena->data + arena->position))
    {
        arena->next_position = arena->position + new_len;
        assert(arena->next_position <= arena->size);
        LOG("arena_base_realloc: size = %li, from position = %li, to position = %li, used = %ld%%\n",
            new_len, arena->position, arena->next_position,
            ((arena->next_position * 100) / arena->size));
    }
    else
    {
        new_ptr = arena_base_alloc(arena, new_len, align);
        memcpy(new_ptr, ptr, old_len);
    }
    return new_ptr;
}

int run_cmd(Cmd *cmd)
{
    printf("run_cmd:");
    for (size_t i = 0; i < cmd->len; i++) printf(" %s", cmd->items[i]);
    printf("\n");
    assert(cmd->len > 0);
    cmd_append(cmd, NULL);
    pid_t pid = fork();
    assert(pid != -1);
    if (pid == 0)
    {
        execvp(cmd->items[0], cmd->items);
        fprintf(stderr, "run_cmd: failed to run subprocess\n");
        _exit(127);
    }
    else
    {
        int status;
        waitpid(pid, &status, 0);
        cmd->len--;
        return status;
    }
}

void cmd_append(Cmd *cmd, char *string)
{
    da_append(&sakana_arena, cmd, string);
}

static time_t get_last_modification(const char *path)
{
    struct stat attr;
    assert(stat(path, &attr) == 0);
    return attr.st_mtim.tv_sec;
}

static void sakana_atexit()
{
    delete_arena(sakana_arena);
}

void sakana_build_base(int argc, char **argv, char *src_path)
{
    sakana_arena = alloc_arena(KB(1));
    atexit(sakana_atexit);

    char *bin_path = argv[0];

    time_t bin_time = get_last_modification(bin_path);
    time_t src_time = get_last_modification(src_path);

    if (bin_time < src_time) {
        Cmd cmd = {0};
        cmd_append(&cmd, "gcc");
        cmd_append(&cmd, src_path);
        cmd_append(&cmd, "-o");
        cmd_append(&cmd, bin_path);
        if (run_cmd(&cmd) == 0) {
            Cmd run = {0};
            cmd_append(&run, bin_path);
            run_cmd(&run);
            exit(0);
        }
    }
}

#endif // SAKANA_IMPLEMENTATION
#endif // SAKANA
