#include "SceneUBO.hpp"

bool SceneUBO::create()
{
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneData), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, BINDING, ubo_);
    return ubo_ != 0;
}

void SceneUBO::destroy()
{
    if (ubo_)
    {
        glDeleteBuffers(1, &ubo_);
        ubo_ = 0;
    }
}

void SceneUBO::upload(const SceneData &data)
{
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneData), &data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
