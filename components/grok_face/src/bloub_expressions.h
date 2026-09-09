#pragma once

#include "bloub_face.h"
#include "bloub_states.h"

// The sixteen resting expressions, ported from bloub's src/bot/expressions.ts.
//
// The face is only two capsules, so everything plays out on four levers: the
// head's orientation, the gap between the eyes, their proportions, and each
// eye's own tilt. That last one is what makes anger and sadness reachable at
// all: they need MIRRORED tilts (tops converging or diverging), which head roll
// cannot give since it leans both eyes the same way.
//
// Only a state flagged `base_face` carries one of these -- `idle` and `swirl`.
// Every other state with a face has an expression measured off the video, and
// that is precisely what the port exists to reproduce.

#define GROK_EXPR_COUNT 16

typedef struct {
    head_gaze_t gaze;
    float split; // half eye-gap on the sphere, degrees
    eye_cfg_t eyes[2];
} bloub_expression_t;

extern const bloub_expression_t BLOUB_EXPRESSIONS[GROK_EXPR_COUNT];

const char *bloub_expression_name(int expr);

/** Interpolates two expressions: a change of mood slides, it does not jump. */
void bloub_expression_blend(const bloub_expression_t *a, const bloub_expression_t *b, float t,
                            bloub_expression_t *out);
