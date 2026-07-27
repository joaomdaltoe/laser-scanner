#pragma once

#include <FlyCapture2.h>
#include <opencv2/core.hpp>

class FlyCaptureCamera final
{
public:
    explicit FlyCaptureCamera(unsigned int cameraIndex = 0);
    ~FlyCaptureCamera() noexcept;

    FlyCaptureCamera(const FlyCaptureCamera&) = delete;
    FlyCaptureCamera& operator=(const FlyCaptureCamera&) = delete;

    bool configureFrameRate(float framesPerSecond);
    bool getExposureRange(
        float& minimumMilliseconds,
        float& maximumMilliseconds,
        float& currentMilliseconds
    );
    bool configureExposure(float milliseconds);
    void start();
    void stop() noexcept;

    cv::Mat grabFrameBgr();
    const FlyCapture2::CameraInfo& info() const noexcept;

private:
    static void throwIfError(
        const FlyCapture2::Error& error,
        const char* operation
    );

    FlyCapture2::Camera camera_;
    FlyCapture2::CameraInfo cameraInfo_;
    FlyCapture2::Image rawImage_;
    FlyCapture2::Image bgrImage_;
    bool connected_ = false;
    bool capturing_ = false;
};
