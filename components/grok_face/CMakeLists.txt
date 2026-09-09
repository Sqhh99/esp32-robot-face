idf_component_register(
    SRCS
        "src/grok_face.c"
        "src/bloub_shapes.c"
        "src/bloub_face.c"
        "src/bloub_decor.c"
        "src/bloub_states.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer log
)

# The rasterizer is pure float math with no NaN/Inf semantics to preserve.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math)
