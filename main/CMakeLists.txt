idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "touch.c"
        "boot_button.c"
        "bloub_shapes.c"
        "bloub_face.c"
        "bloub_decor.c"
        "bloub_states.c"
        "bloub_engine.c"
    INCLUDE_DIRS "."
)

# The rasterizer is pure float math with no NaN/Inf semantics to preserve, so
# relaxed FP rules are safe and measurably faster in its per-pixel loops.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math)
