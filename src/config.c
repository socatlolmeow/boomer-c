#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const Config default_config = {
    .min_scale = 0.01,
    .scroll_speed = 1.5,
    .drag_friction = 6.0,
    .scale_friction = 4.0,
};

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#include <stdarg.h>
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void format_double(double v, char *buf, size_t cap)
{
    for (int prec = 1; prec <= 17; ++prec) {
        snprintf(buf, cap, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) break;
    }
    if (!strpbrk(buf, ".eEn")) {  /* no '.', exponent, "inf" or "nan" */
        size_t n = strlen(buf);
        if (n + 2 < cap) strcpy(buf + n, ".0");
    }
}

bool parse_double(const char *s, double *out)
{
    if (*s == '\0') return false;
    char *end;
    double v = strtod(s, &end);
    if (*end != '\0') return false;
    *out = v;
    return true;
}

static char *strip(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

Config load_config(const char *file_path)
{
    Config result = default_config;

    FILE *f = fopen(file_path, "r");
    if (!f) die("Could not open config file `%s`", file_path);

    char *raw = NULL;
    size_t cap = 0;
    while (getline(&raw, &cap, f) != -1) {
        char *line = strip(raw);
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) die("Malformed config line `%s` (expected `key = value`)", line);
        *eq = '\0';
        char *key = strip(line);
        char *value = strip(eq + 1);

        double *target = NULL;
        if      (strcmp(key, "min_scale") == 0)      target = &result.min_scale;
        else if (strcmp(key, "scroll_speed") == 0)   target = &result.scroll_speed;
        else if (strcmp(key, "drag_friction") == 0)  target = &result.drag_friction;
        else if (strcmp(key, "scale_friction") == 0) target = &result.scale_friction;
        else die("Unknown config key `%s`", key);

        if (!parse_double(value, target))
            die("Invalid number `%s` for config key `%s`", value, key);
    }

    free(raw);
    fclose(f);
    return result;
}

void generate_default_config(const char *file_path)
{
    FILE *f = fopen(file_path, "w");
    if (!f) die("Could not open `%s` for writing", file_path);

    char buf[64];
    format_double(default_config.min_scale, buf, sizeof buf);
    fprintf(f, "min_scale = %s\n", buf);
    format_double(default_config.scroll_speed, buf, sizeof buf);
    fprintf(f, "scroll_speed = %s\n", buf);
    format_double(default_config.drag_friction, buf, sizeof buf);
    fprintf(f, "drag_friction = %s\n", buf);
    format_double(default_config.scale_friction, buf, sizeof buf);
    fprintf(f, "scale_friction = %s\n", buf);

    fclose(f);
}

void print_config(const Config *config)
{
    char a[64], b[64], c[64], d[64];
    format_double(config->min_scale, a, sizeof a);
    format_double(config->scroll_speed, b, sizeof b);
    format_double(config->drag_friction, c, sizeof c);
    format_double(config->scale_friction, d, sizeof d);
    printf("(min_scale: %s, scroll_speed: %s, drag_friction: %s, scale_friction: %s)\n",
           a, b, c, d);
}
