#include "navigation.h"

#include <math.h>

#define VELOCITY_THRESHOLD 15.0f

Vec2f camera_world(const Camera *camera, Vec2f v)
{
    return vec2_div_s(v, camera->scale);
}

void camera_update(Camera *camera, const Config *config, double dt,
                   const Mouse *mouse, Vec2f window_size)
{
    if (fabs(camera->delta_scale) > 0.5) {
        Vec2f half = vec2_scale(window_size, 0.5f);
        Vec2f p0 = vec2_div_s(vec2_sub(camera->scale_pivot, half), camera->scale);

        double new_scale = (double)camera->scale + camera->delta_scale * dt;
        camera->scale = (float)(new_scale > config->min_scale ? new_scale : config->min_scale);

        Vec2f p1 = vec2_div_s(vec2_sub(camera->scale_pivot, half), camera->scale);
        camera->position = vec2_add(camera->position, vec2_sub(p0, p1));

        camera->delta_scale -= camera->delta_scale * dt * config->scale_friction;
    }

    if (!mouse->drag && vec2_length(camera->velocity) > VELOCITY_THRESHOLD) {
        camera->position = vec2_add(camera->position,
                                    vec2_scale(camera->velocity, (float)dt));
        camera->velocity = vec2_sub(camera->velocity,
                                    vec2_scale(camera->velocity,
                                               (float)(dt * config->drag_friction)));
    }
}
