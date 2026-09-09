idf_component_register(
    SRCS
        "src/grok_face.c"
        "src/grok_face_engine.c"
        "src/grok_face_render.c"
        "src/bloub_shapes.c"
        "src/bloub_face.c"
        "src/bloub_decor.c"
        "src/bloub_states.c"
        "src/bloub_expressions.c"
        "src/bloub_skins.c"
        "src/bloub_eyefit.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer log
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O3)

# -ffast-math only on the files that run per pixel or per sample: there the
# math is plain arithmetic with no NaN or Inf semantics to preserve.
#
# It is deliberately NOT applied to grok_face.c or grok_face_engine.c, which
# both reject a non-finite input with isfinite() -- config validation in one,
# the aim target in the other. Under -ffast-math the compiler is entitled to
# assume no operand is ever NaN or Inf and fold those checks away, which turns
# a guard into nothing at all.
foreach(fast_math_src
        src/grok_face_render.c
        src/bloub_shapes.c
        src/bloub_face.c
        src/bloub_decor.c
        src/bloub_states.c
        src/bloub_expressions.c)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/${fast_math_src}"
        PROPERTIES COMPILE_OPTIONS "-ffast-math"
    )
endforeach()
