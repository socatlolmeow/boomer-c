#ifndef BOOMER_CONFIG_H
#define BOOMER_CONFIG_H

#include <stdbool.h>

typedef struct {
    double min_scale;
    double scroll_speed;
    double drag_friction;
    double scale_friction;
} Config;

extern const Config default_config;

bool parse_double(const char *s, double *out);

Config load_config(const char *file_path);

void generate_default_config(const char *file_path);

void print_config(const Config *config);

#endif
