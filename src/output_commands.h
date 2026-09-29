#ifndef OUTPUT_COMMANDS_H
#define OUTPUT_COMMANDS_H
#include <string.h>
enum { CMD_ERROR, CMD_OUTPUT, CMD_POLARITY, CMD_RELEASE, CMD_DIAG, CMD_STATUS };
/* In-place tokenizer. ASCII case-insensitive, whitespace-separated tokens. */
static char *next_token(char **cursor)
{
    char *start, *p = *cursor;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) { *cursor = p; return 0; }
    start = p;
    while (*p && *p != ' ' && *p != '\t') {
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
        ++p;
    }
    if (*p) *p++ = 0;
    *cursor = p;
    return start;
}
static unsigned char output_index(char *name)
{
    if (!strcmp(name, "LED1")) return 0;
    if (!strcmp(name, "LED2")) return 1;
    if (!strcmp(name, "BUZZER")) return 2;
    return 0xff;
}
static unsigned char parse_command(char *line, unsigned char *target,
                                   unsigned char *value)
{
    char *cursor = line, *a, *b, *c;
    unsigned char index;
    a = next_token(&cursor);
    if (!a) return CMD_ERROR;
    b = next_token(&cursor);
    c = next_token(&cursor);
    if (next_token(&cursor)) return CMD_ERROR;
    if (!b) {
        if (!strcmp(a, "RELEASE")) return CMD_RELEASE;
        if (!strcmp(a, "?")) return CMD_STATUS;
        return CMD_ERROR;
    }
    if (!strcmp(a, "POL")) {
        if (!c || c[1] || (c[0] != 'H' && c[0] != 'L')) return CMD_ERROR;
        index = output_index(b);
        if (index == 0xff) return CMD_ERROR;
        *target = index; *value = c[0] == 'H';
        return CMD_POLARITY;
    }
    if (c || b[1] || (b[0] != 'Y' && b[0] != 'N')) return CMD_ERROR;
    if (!strcmp(a, "DIAG")) { *value = b[0] == 'Y'; return CMD_DIAG; }
    index = output_index(a);
    if (index == 0xff) return CMD_ERROR;
    *target = index; *value = b[0] == 'Y';
    return CMD_OUTPUT;
}
#endif
