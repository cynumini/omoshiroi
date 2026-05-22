#include <assert.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <readline/readline.h>
#include <sys/stat.h>

// #define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

#define UNUSED(value) (void) value
define_da(char *, Strings);

typedef struct {
    char *string;
    size_t len;
} StringView;

StringView create_string_view(char *string)
{
    return (StringView) { .string = string, .len = strlen(string) };
}

void strip(StringView *sv)
{
    while (sv->string[0] == ' ') {
        sv->string++;
        sv->len--;
        if (sv->len == 0)
            break;
    }
    while (sv->string[sv->len - 1] == ' ') {
        sv->len--;
        if (sv->len == 0)
            break;
    }
}

typedef struct DatabaseManager {
    const char *file;
    const char *sql_path;
    sqlite3 *db;
} DatabaseManager;

static bool is_file(const char *file)
{
    struct stat buf;
    if (stat(file, &buf) != 0)
        return false;
    return S_ISREG(buf.st_mode);
}

int get_version(DatabaseManager self)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(self.db, "PRAGMA user_version;", -1, &stmt, NULL) == SQLITE_OK);
    int result, version;
    do {
        result = sqlite3_step(stmt);
        assert(result == SQLITE_DONE || result == SQLITE_BUSY || result == SQLITE_ROW);
        if (result == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
    } while (result != SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return version;
}

void set_version(DatabaseManager self, int value)
{
    sqlite3_stmt *stmt = NULL;
    char sql[256] = { 0 };
    sprintf(sql, "PRAGMA user_version = %d;", value);
    assert(sqlite3_prepare_v2(self.db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_int(stmt, 1, value);
    int result;
    do {
        result = sqlite3_step(stmt);
        assert(result == SQLITE_DONE || result == SQLITE_BUSY);
    } while (result != SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

static void migrate(DatabaseManager self)
{
    int version = get_version(self);
    if (version == 0) {
        printf("migrate from 0 to 1\n");
        const char *sql =
            "BEGIN;\n"
            "ALTER TABLE media RENAME TO media_old;\n"
            "CREATE TABLE media (\n"
            "    id       INTEGER NOT NULL UNIQUE,\n"
            "    name     TEXT NOT NULL,\n"
            "    kind     TEXT NOT NULL,\n"
            "    release  TEXT NOT NULL,\n"
            "    elo      INT NOT NULL DEFAULT 1000,\n"
            "    japanese INT NOT NULL,\n"
            "    state    INT NOT NULL DEFAULT 0,\n"
            "    note     TEXT,\n"
            "    PRIMARY KEY(id AUTOINCREMENT),\n"
            "    UNIQUE(name, kind)\n"
            ") STRICT;\n"
            "INSERT INTO media (id, name, kind, release, elo, japanese, note)\n"
            "SELECT id, name, type, release, elo, japanese, note\n"
            "FROM media_old;\n"
            "ALTER TABLE log RENAME TO log_old;\n"
            "CREATE TABLE log (\n"
            "    id               INTEGER NOT NULL UNIQUE,\n"
            "    timestamp        REAL NOT NULL UNIQUE,\n"
            "    kind             TEXT NOT NULL,\n"
            "    duration_seconds INTEGER NOT NULL,\n"
            "    parent_id        INTEGER,\n"
            "    characters       INTEGER,\n"
            "    page             INTEGER,\n"
            "    eps              REAL,\n"
            "    yt_id            TEXT,\n"
            "    yt_title         TEXT,\n"
            "    notes            TEXT,\n"
            "    PRIMARY KEY(id AUTOINCREMENT),\n"
            "    FOREIGN KEY(parent_id) REFERENCES media(id)\n"
            ") STRICT;\n"
            "INSERT INTO log (id, timestamp, kind, duration_seconds, parent_id, characters, page, eps, yt_id, "
            "yt_title, notes)\n"
            "SELECT id, timestamp, kind, duration_seconds, parent_id, characters, page, eps, yt_id, yt_title, notes\n"
            "FROM log_old;\n"
            "DROP TABLE log_old;\n"
            "DROP TABLE media_old;\n"
            "COMMIT;";
        assert(sqlite3_exec(self.db, sql, NULL, NULL, NULL) == SQLITE_OK);
        set_version(self, 1);
    }
}

char *join_path(Arena *arena, const char *p1, const char *p2)
{
    char *result = arena_alloc(arena, char, strlen(p1) + strlen(p2) + 2);
    sprintf(result, "%s/%s", p1, p2);
    return result;
}

static DatabaseManager create_database_manager(Arena *arena)
{
    const char *home_path = getenv("HOME");
    DatabaseManager self = { .file = join_path(arena, home_path, "org/elo.db"),
                             .sql_path = join_path(arena, home_path, "org/elo.sql") };
    bool exist = is_file(self.file);
    assert(sqlite3_open(self.file, &self.db) == SQLITE_OK);
    if (!exist) {
        const char *sql = "BEGIN;\n"
                          "CREATE TABLE media (\n"
                          "    id       INTEGER NOT NULL UNIQUE,\n"
                          "    name     TEXT NOT NULL UNIQUE,\n"
                          "    type     TEXT NOT NULL,\n"
                          "    release  TEXT NOT NULL,\n"
                          "    elo      INT NOT NULL DEFAULT 1000,\n"
                          "    japanese INT NOT NULL,\n"
                          "    note     TEXT,\n"
                          "    PRIMARY KEY(id AUTOINCREMENT)\n"
                          ") STRICT;\n"
                          "CREATE TABLE log (\n"
                          "    id               INTEGER NOT NULL UNIQUE,\n"
                          "    timestamp        REAL NOT NULL UNIQUE,\n"
                          "    kind             TEXT NOT NULL,\n"
                          "    duration_seconds INTEGER NOT NULL,\n"
                          "    parent_id        INTEGER,\n"
                          "    characters       INTEGER,\n"
                          "    page             INTEGER,\n"
                          "    eps              REAL,\n"
                          "    yt_id            TEXT,\n"
                          "    yt_title         TEXT,\n"
                          "    notes            TEXT,\n"
                          "    PRIMARY KEY(id AUTOINCREMENT),\n"
                          "    FOREIGN KEY(parent_id) REFERENCES media(id)\n"
                          ") STRICT;\n"
                          "COMMIT;";
        assert(sqlite3_exec(self.db, sql, NULL, NULL, NULL) == SQLITE_OK);
    }
    migrate(self);
    return self;
}

// WIP
static void destroy_database_manager(Arena *arena, DatabaseManager self)
{
    sqlite3_close(self.db);
    char *result = arena_alloc(arena, char, strlen("sqlite3 .dump >") + strlen(self.file) + strlen(self.sql_path) + 1);
    sprintf(result, "sqlite3 %s .dump > %s", self.file, self.sql_path);
    assert(system(result) == 0);
}

static char *arena_strdup(Arena *arena, const char *src)
{
    size_t n = strlen(src) + 1;
    char *dest = arena_alloc(arena, char, n);
    memcpy(dest, src, n);
    return dest;
}

static Strings get_names(Arena *arena, DatabaseManager dm)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(dm.db, "SELECT name FROM media", -1, &stmt, NULL) == SQLITE_OK);
    Strings names = {};
    int step;
    while (true) {
        step = sqlite3_step(stmt);
        if (step == SQLITE_DONE)
            break;
        da_append(arena, &names, arena_strdup(arena, (const char *) sqlite3_column_text(stmt, 0)));
    }
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return names;
}

static Strings get_kinds(Arena *arena, DatabaseManager dm)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(dm.db, "SELECT DISTINCT kind FROM media", -1, &stmt, NULL) == SQLITE_OK);
    Strings types = {};
    int step;
    while (true) {
        step = sqlite3_step(stmt);
        if (step == SQLITE_DONE)
            break;
        da_append(arena, &types, arena_strdup(arena, (const char *) sqlite3_column_text(stmt, 0)));
    }
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return types;
}

// WIP
static void add(DatabaseManager dm, StringView name, StringView kind, const char *release, bool japanese, int state,
                const char *note)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO media (name, kind, release, japanese, state, note) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    assert(sqlite3_prepare_v2(dm.db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 1, name.string, (int) name.len, SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 2, kind.string, (int) kind.len, SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 3, release, -1, SQLITE_STATIC) == SQLITE_OK);
    assert(sqlite3_bind_int(stmt, 4, japanese) == SQLITE_OK);
    assert(sqlite3_bind_int(stmt, 5, state) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 6, note, -1, SQLITE_STATIC) == SQLITE_OK);
    int result;
    do {
        result = sqlite3_step(stmt);
        assert(result == SQLITE_DONE || result == SQLITE_BUSY);
    } while (result != SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

static Strings *compentry_func_strings;

static char *entry_func(const char *text, int index)
{
    static size_t len, i;
    if (index == 0) {
        len = strlen(text);
        i = 0;
    }
    while (i < compentry_func_strings->len) {
        const char *option = compentry_func_strings->items[i++];
        if (strncmp(option, text, len) == 0) {
            return strdup(option);
        }
    }
    return NULL;
}

static char **completion_function(const char *text, int start, int end)
{
    UNUSED(start);
    UNUSED(end);
    rl_bind_key('\t', rl_complete);
    rl_attempted_completion_over = 1;
    rl_completion_append_character = '\0';
    rl_completer_word_break_characters = "";
    return rl_completion_matches(text, entry_func);
}

static const char *get_prompt(Arena *arena, const char *prompt)
{
    size_t n = strlen(prompt) + strlen(": ") + 1;
    char *s = arena_alloc(arena, char *, n);
    sprintf(s, "%s: ", prompt);
    return s;
}

static StringView prompt_readline(Arena *arena, const char *prompt, Strings options)
{
    compentry_func_strings = &options;
    rl_attempted_completion_function = completion_function;
    const char *src = readline(get_prompt(arena, prompt));
    char *string = arena_strdup(arena, src);
    free((void *) src);
    StringView sv = create_string_view(string);
    strip(&sv);
    return sv;
}

unsigned int month_to_int(const char *m)
{
    if (!strcmp(m, "Jan") || !strcmp(m, "January"))
        return 1;
    if (!strcmp(m, "Feb") || !strcmp(m, "February"))
        return 2;
    if (!strcmp(m, "Mar") || !strcmp(m, "March"))
        return 3;
    if (!strcmp(m, "Apr") || !strcmp(m, "April"))
        return 4;
    if (!strcmp(m, "May"))
        return 5;
    if (!strcmp(m, "Jun") || !strcmp(m, "June"))
        return 6;
    if (!strcmp(m, "Jul") || !strcmp(m, "July"))
        return 7;
    if (!strcmp(m, "Aug") || !strcmp(m, "August"))
        return 8;
    if (!strcmp(m, "Sep") || !strcmp(m, "September"))
        return 9;
    if (!strcmp(m, "Oct") || !strcmp(m, "October"))
        return 10;
    if (!strcmp(m, "Nov") || !strcmp(m, "November"))
        return 11;
    if (!strcmp(m, "Dec") || !strcmp(m, "December"))
        return 12;
    return 0;
}

static const char *prompt_date(Arena *arena, const char *prompt)
{
    rl_bind_key('\t', rl_insert);
    char *result = arena_alloc(arena, char, strlen("0000-00-00") + 1);
    unsigned int y, m, d;
    char buffer[32] = { 0 };
    char rest;
    const char *src = NULL;
    while (true) {
        if (src != NULL) {
            free((char *) src);
        }
        src = readline(get_prompt(arena, prompt));
        bool fail = !(sscanf(src, "%31s %u, %u%c", buffer, &d, &y, &rest) == 3);
        if (!fail) {
            m = month_to_int(buffer);
            fail = m == 0;
        }
        if (fail) {
            fail = !(sscanf(src, "%u年%u月%u日%c", &y, &m, &d, &rest) == 3);
        }
        if (fail) {
            fail = !(sscanf(src, "%u-%u-%u%c", &y, &m, &d, &rest) == 3);
        }
        if (!fail && (m > 12 || d > 31 || y > 9999)) {
            fail = true;
        }
        if (!fail) {
            sprintf(result, "%04u-%02u-%02u", y, m, d);
            break;
        }
    }
    free((void *) src);
    return result;
}

static bool prompt_check(Arena *arena, const char *prompt)
{
    rl_bind_key('\t', rl_insert);
    size_t n = strlen(prompt) + strlen(" (y/n): ") + 1;
    char *s = arena_alloc(arena, char *, n);
    sprintf(s, "%s (y/n): ", prompt);
    while (true) {
        const char *src = readline(s);
        if (strlen(src) == 0) {
            free((char *) src);
            continue;
        }
        if (src[0] == 'y' || src[0] == 'Y') {
            free((char *) src);
            return true;
        } else if (src[0] == 'n' || src[0] == 'N') {
            free((char *) src);
            return false;
        }
        free((char *) src);
    }
}

static const char *prompt(Arena *arena, const char *prompt, bool empty_ok)
{
    rl_bind_key('\t', rl_insert);
    while (true) {
        const char *s = readline(get_prompt(arena, prompt));
        size_t len = strlen(s);
        if (len || empty_ok) {
            if (len == 0) {
                free((char *) s);
                return NULL;
            }
            char *result = arena_strdup(arena, s);
            free((char *) s);
            return result;
        }
        free((char *) s);
    }
}

int main()
{
    Arena arena = alloc_arena(MB(1));
    DatabaseManager dm = create_database_manager(&arena);
    // TODO: add args parsing instead of manual input
    StringView name = prompt_readline(&arena, "Name", get_names(&arena, dm));
    StringView kind = prompt_readline(&arena, "Kind", get_kinds(&arena, dm));
    const char *release = prompt_date(&arena, "Release");
    bool japanese = prompt_check(&arena, "Will I consume it in Japanese?");
    int state = 0;
    bool completed = prompt_check(&arena, "Completed?");
    if (completed) {
        if (prompt_check(&arena, "Want to re-experience?")) {
            state = 1;
        } else {
            state = 2;
        }
    }
    const char *note = prompt(&arena, "Note", true);
    add(dm, name, kind, release, japanese, state, note);
    destroy_database_manager(&arena, dm);
    delete_arena(arena);
    return 0;
}
