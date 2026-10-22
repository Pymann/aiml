// Native fault injection and storage checks complement the AIML pixel/lifetime test.
#include <GL/glew.h>
#include "gl_runtime.h"
#include "engine_runtime.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct Bytes { int32_t size, capacity, read, write; uint8_t* data; };
template<class T, size_t N> Bytes bytes(T (&data)[N]) {
    return {sizeof(data), sizeof(data), 0, sizeof(data), reinterpret_cast<uint8_t*>(data)};
}
static void GLAPIENTRY failAllocation(GLenum, GLsizeiptr, const void*, GLenum) {
    // Deterministic allocation failure, without risking a host OOM.
    glEnable(0xdeadbeef);
}
int main() {
    void* win = aiml_engine_create_window(64,64,"VFX runtime regression");
    assert(win);
    float positions[] = {-1,-1, 1,-1, 0,1};
    uint32_t indices[] = {0,1,2};
    float instances[] = {0,0};
    auto pos=bytes(positions), idx=bytes(indices), ins=bytes(instances);
    void* vbo=aiml_gl_create_buffer(&pos);
    void* ibo=aiml_gl_create_index_buffer(&idx);
    void* instance=aiml_gl_create_buffer_dynamic(&ins);
    void* vao=aiml_gl_create_vao();
    aiml_gl_vao_bind_vbo(vao,vbo); aiml_gl_vao_attrib(vao,0,2,8,0);
    aiml_gl_vao_bind_vbo(vao,instance); aiml_gl_vao_attrib(vao,1,1,4,0);
    aiml_gl_vao_bind_ibo(vao,ibo);
    assert(aiml_gl_vao_attrib_divisor(vao,1,2));
    void* shader=aiml_gl_compile_shader(
        "#version 330 core\nlayout(location=0) in vec2 pos; layout(location=1) in float x; void main(){gl_Position=vec4(pos+vec2(x,0),0,1);}",
        "#version 330 core\nout vec4 col;void main(){col=vec4(1,0,0,1);}");
    assert(shader); aiml_gl_shader_use(shader);
    GLint before=0, after=0;
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&before);
    auto real=glBufferData; glBufferData=failAllocation;
    assert(!aiml_gl_buffer_reserve_bytes(vbo,128));
    glBufferData=real;
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&after);
    assert(before==after && glIsBuffer(before));
    assert(aiml_gl_draw_elements_instanced(vao,3,4));
    assert(!aiml_gl_draw_elements_instanced(vao,3,5));
    assert(!aiml_gl_draw_elements_instanced(vao,INT32_MAX,1));
    assert(!aiml_gl_draw_elements_instanced(vao,-1,0));
    assert(aiml_gl_draw_elements_instanced(nullptr,0,1));
    assert(!aiml_gl_draw_elements_instanced(nullptr,1,1));
    assert(aiml_gl_buffer_reserve_bytes(vbo,128));
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&after);
    assert(after!=before && !glIsBuffer(before));
    glBindBuffer(GL_COPY_READ_BUFFER,after);
    float preserved[6]={}; glGetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(preserved),preserved);
    assert(memcmp(positions,preserved,sizeof(positions))==0);
    // Reservation and updates leave the VAO's index binding intact.
    assert(aiml_gl_buffer_reserve_bytes(ibo,64));
    assert(aiml_gl_buffer_reserve_bytes(instance,64));
    assert(aiml_gl_draw_elements_instanced(vao,3,4));
    Bytes empty{0,0,0,0,nullptr};
    assert(aiml_gl_buffer_update_range(vbo,128,&empty));
    assert(!aiml_gl_buffer_update_range(vbo,129,&empty));
    assert(!aiml_gl_buffer_update_range(vbo,INT32_MAX,&pos));
    Bytes malformed{-1,0,0,0,nullptr};
    assert(!aiml_gl_buffer_update_range(vbo,0,&malformed));
    assert(!aiml_gl_buffer_update_range(vbo,0,nullptr));
    uint32_t invalidIndex[]={UINT32_MAX}; auto bad=bytes(invalidIndex);
    assert(aiml_gl_buffer_update_range(ibo,0,&bad));
    assert(!aiml_gl_draw_elements_instanced(vao,3,1));
    assert(aiml_gl_buffer_update_range(ibo,0,&idx));
    assert(aiml_gl_draw_elements_instanced(vao,3,1));
    // No retained C++ owner or stale name may survive early buffer release.
    aiml_gl_buffer_delete(instance);
    assert(!aiml_gl_draw_elements_instanced(vao,3,1));
    aiml_gl_vao_delete(vao);
    aiml_gl_buffer_delete(vbo); aiml_gl_buffer_delete(ibo);
    aiml_gl_shader_delete(shader);
    aiml_engine_destroy_window(win);
    puts("PASS native VFX: allocation rollback, preserved bindings/data, ranges, divisors, deleted borrow");
}
