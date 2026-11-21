#ifndef BOOMER_LA_H
#define BOOMER_LA_H

#include <math.h>

typedef struct {
    float x;
    float y;
} Vec2f;

static inline Vec2f vec2(float x, float y) { return (Vec2f){x, y}; }

static inline Vec2f vec2_scale(Vec2f a, float s) { return vec2(a.x * s, a.y * s); }
static inline Vec2f vec2_div_s(Vec2f a, float s)  { return vec2(a.x / s, a.y / s); }
static inline Vec2f vec2_mul(Vec2f a, Vec2f b)    { return vec2(a.x * b.x, a.y * b.y); }
static inline Vec2f vec2_div(Vec2f a, Vec2f b)    { return vec2(a.x / b.x, a.y / b.y); }
static inline Vec2f vec2_sub(Vec2f a, Vec2f b)    { return vec2(a.x - b.x, a.y - b.y); }
static inline Vec2f vec2_add(Vec2f a, Vec2f b)    { return vec2(a.x + b.x, a.y + b.y); }

static inline float vec2_length(Vec2f a) { return sqrtf(a.x * a.x + a.y * a.y); }

static inline Vec2f vec2_normalize(Vec2f a)
{
    float b = vec2_length(a);
    if (b == 0.0f) return vec2(0.0f, 0.0f);
    return vec2(a.x / b, a.y / b);
}

#endif
