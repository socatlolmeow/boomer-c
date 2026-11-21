#ifndef BOOMER_NAVIGATION_H
#define BOOMER_NAVIGATION_H

#include <stdbool.h>

#include "config.h"
#include "la.h"

typedef struct {
    Vec2f curr;
    Vec2f prev;
    bool drag;
} Mouse;

typedef struct {
    Vec2f position;
    Vec2f velocity;
    float scale;
    double delta_scale;
    Vec2f scale_pivot;
} Camera;

Vec2f camera_world(const Camera *camera, Vec2f v);

void camera_update(Camera *camera, const Config *config, double dt,
                   const Mouse *mouse, Vec2f window_size);

#endif
