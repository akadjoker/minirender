#pragma once

#include "Core.hpp"
#include "Device.hpp"
class Demo
{
public:
    virtual ~Demo() = default;

    virtual const char *name() const = 0;
    virtual void build(Scene &scene, Device &device) = 0;
    virtual void update(float dt) { (void)dt; }
    virtual void drawImGui(Renderer &renderer) { (void)renderer; }

    Camera *camera() const { return camera_; }

protected:
    Camera *createMainCamera(Scene &scene, Device &device,
                             const glm::vec3 &position = {0.f, 6.f, 16.f},
                             const glm::vec3 &target = {0.f, 1.5f, 0.f})
    {
        camera_ = scene.createCamera("main_camera");
        camera_->fov = 60.f;
        camera_->nearPlane = 0.1f;
        camera_->farPlane = 1000.f;
        camera_->setAspect(device.GetWidth(), device.GetHeight());
        camera_->setViewport(0, 0, device.GetWidth(), device.GetHeight());
        camera_->clearColor = true;
        camera_->clearColorVal = {0.08f, 0.10f, 0.14f, 1.0f};
        camera_->clearDepth = true;
        camera_->setPosition(position);
        camera_->lookAt(target);

        auto *controller = new FreeCameraController();
        controller->moveSpeed = 12.f;
        camera_->setController(controller);
        scene.setCurrentCamera(camera_);
        return camera_;
    }

    FreeCameraController *freeCamera() const
    {
        if (!camera_)
            return nullptr;
        return static_cast<FreeCameraController *>(camera_->getController());
    }

    void configureFreeCamera(float moveSpeed,
                             float mouseSensitivity = 0.15f,
                             float sprintMultiplier = 2.5f)
    {
        FreeCameraController *controller = freeCamera();
        if (!controller)
            return;

        controller->moveSpeed = moveSpeed;
        controller->mouseSensitivity = mouseSensitivity;
        controller->sprintMultiplier = sprintMultiplier;
    }

    void createSun(Scene &scene,
                   const glm::vec3 &euler = {50.f, -35.f, 0.f},
                   const glm::vec3 &color = {1.0f, 0.96f, 0.90f},
                   float intensity = 1.35f,
                   const glm::vec3 &ambient = {0.10f, 0.11f, 0.14f})
    {
        auto *sun = scene.createLight<DirectionalLight>("sun");
        sun->setEulerAngles(euler);
        sun->color = color;
        sun->intensity = intensity;
        sun->ambient = ambient;
    }

private:
    Camera *camera_ = nullptr;
};
