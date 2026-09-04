#pragma once

// Top-level bloub engine: owns the current state + its local animation
// clock, and rasterizes one frame at a time into the display's band buffers.
//
// Deliberately simpler than bloub's own BotEngine: state changes cut instead
// of cross-fading (see bloub_states.h for the full list of what this port
// intentionally leaves out, and why).

void bloub_engine_init(void);

/** Advances to the next state in the 14-state catalogue, wrapping around. */
void bloub_engine_next_state(void);

/** Renders exactly one frame. Paces itself on display band DMA completion. */
void bloub_engine_render_frame(void);
