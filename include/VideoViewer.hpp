#pragma once

#include <string>

class FlyCaptureCamera;

class VideoViewer final
{
public:
    explicit VideoViewer(
        std::string windowName = "Camera GigE FLEA",
        double targetFramesPerSecond = 30.0
    );

    void run(FlyCaptureCamera& camera) const;

private:
    std::string windowName_;
    double targetFramesPerSecond_;
};
