#pragma once
#define DEFAULT_SAMPLER "tex"

static GLchar const defaultVertexSource[] =
        "vec4 position(mat4 transform_projection, vec4 vertex_position) {\n"
        "  return transform_projection * vertex_position;\n"
        "}\n";

static GLchar const vertexHeader[] =
        "#version 330 core\n"
        "uniform mat4 projection;\n"
        "uniform mat4 view;\n"
        "uniform mat4 model;\n"
        "uniform mat2 textureRect;\n"
        "uniform vec2 size;\n"
        "layout(location = 0) in vec2 vPos;\n"
        "layout(location = 1) in vec2 vUV;\n"
        "layout(location = 2) in vec4 vColor;\n"
        "out vec2 fUV;\n"
        "out vec4 fColor;\n";

static GLchar const vertexFooter[] =
        "void main() {\n"
        "gl_Position = projection * view * model * vec4(vPos * size, 0.1, 1.0);\n"
        /* textureRect is the quad's viewport: column 0 is the offset, column 1
         * the scale, both normalised. It was being uploaded but never applied,
         * so a quad shrank the rectangle it drew into without ever cropping the
         * texture - the whole image was squeezed into it. The default quad is
         * (0,0,1,1), which leaves the coordinates untouched. */
        "  fUV = textureRect[0] + vUV * textureRect[1];\n"
        "  fColor = vColor;\n"
        "}\n";

static GLchar const defaultFragmentSource[] =
        "vec4 effect( vec4 color, Image tex, vec2 texture_coords, vec2 screen_coords ) {\n"
        "  return Texel(tex, texture_coords) * color;\n"
        "}\n";

static GLchar const fragmentHeader[] =
        "#version 330 core\n"
        "#define Image sampler2D\n"
        "#define Texel(s, c) texture((s), (c))\n"
        "in vec2 fUV;\n"
        "in vec4 fColor;\n"
        "uniform Image " DEFAULT_SAMPLER ";\n"
        "uniform vec4 color;\n"
        "out vec4 FragColor;\n";

static GLchar const fragmentFooter[] =
        "void main() {\n"
        "  FragColor = effect(color * fColor, " DEFAULT_SAMPLER ", fUV,vec2(0.0, 0.0));\n"
        "}\n";

