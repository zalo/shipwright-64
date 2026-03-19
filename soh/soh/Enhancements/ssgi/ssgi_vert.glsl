// GT-VBGI fullscreen triangle vertex shader (no VBO needed)
#ifdef GL_ES
precision highp float;
#endif

out vec2 v_uv;

void main() {
    // Emit a single triangle that covers the full NDC [-1,1] x [-1,1] quad.
    // VertexID  NDC         UV
    //   0       (-1, -1)    (0, 0)
    //   1       ( 3, -1)    (2, 0)
    //   2       (-1,  3)    (0, 2)
    float x = float((gl_VertexID & 1) * 4) - 1.0;
    float y = float((gl_VertexID >> 1) * 4) - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
}
