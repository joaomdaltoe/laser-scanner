#include "FlyCaptureCamera.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

FlyCaptureCamera::FlyCaptureCamera(unsigned int cameraIndex)
{
    FlyCapture2::BusManager busManager;

    unsigned int cameraCount = 0;
    throwIfError(
        busManager.GetNumOfCameras(&cameraCount),
        "GetNumOfCameras"
    );

    if (cameraCount == 0)
    {
        throw std::runtime_error("Nenhuma camera FlyCapture2 foi encontrada.");
    }

    if (cameraIndex >= cameraCount)
    {
        throw std::out_of_range("O indice da camera solicitado nao existe.");
    }

    FlyCapture2::PGRGuid cameraGuid;
    throwIfError(
        busManager.GetCameraFromIndex(cameraIndex, &cameraGuid),
        "GetCameraFromIndex"
    );

    throwIfError(camera_.Connect(&cameraGuid), "Connect");
    connected_ = true;

    try
    {
        throwIfError(camera_.GetCameraInfo(&cameraInfo_), "GetCameraInfo");
    }
    catch (...)
    {
        camera_.Disconnect();
        connected_ = false;
        throw;
    }
}

FlyCaptureCamera::~FlyCaptureCamera() noexcept
{
    stop();

    if (connected_)
    {
        camera_.Disconnect();
        connected_ = false;
    }
}

bool FlyCaptureCamera::configureFrameRate(float framesPerSecond)
{
    if (framesPerSecond <= 0.0F)
    {
        throw std::invalid_argument("O frame rate deve ser maior que zero.");
    }

    FlyCapture2::Property frameRateProperty;
    frameRateProperty.type = FlyCapture2::FRAME_RATE;

    FlyCapture2::Error error = camera_.GetProperty(&frameRateProperty);
    if (error != FlyCapture2::PGRERROR_OK || !frameRateProperty.present)
    {
        return false;
    }

    frameRateProperty.autoManualMode = false;
    frameRateProperty.onOff = true;
    frameRateProperty.absControl = true;
    frameRateProperty.absValue = framesPerSecond;

    error = camera_.SetProperty(&frameRateProperty);
    return error == FlyCapture2::PGRERROR_OK;
}

bool FlyCaptureCamera::getExposureRange(
    float& minimumMilliseconds,
    float& maximumMilliseconds,
    float& currentMilliseconds
)
{
    FlyCapture2::PropertyInfo shutterInfo;
    shutterInfo.type = FlyCapture2::SHUTTER;

    FlyCapture2::Error error = camera_.GetPropertyInfo(&shutterInfo);
    if (
        error != FlyCapture2::PGRERROR_OK ||
        !shutterInfo.present ||
        !shutterInfo.absValSupported
    )
    {
        return false;
    }

    FlyCapture2::Property shutterProperty;
    shutterProperty.type = FlyCapture2::SHUTTER;
    error = camera_.GetProperty(&shutterProperty);
    if (error != FlyCapture2::PGRERROR_OK || !shutterProperty.present)
    {
        return false;
    }

    minimumMilliseconds = shutterInfo.absMin;
    maximumMilliseconds = shutterInfo.absMax;
    currentMilliseconds = shutterProperty.absValue;
    return maximumMilliseconds >= minimumMilliseconds;
}

bool FlyCaptureCamera::configureExposure(float milliseconds)
{
    if (milliseconds <= 0.0F)
    {
        throw std::invalid_argument("A exposicao deve ser maior que zero.");
    }

    FlyCapture2::PropertyInfo shutterInfo;
    shutterInfo.type = FlyCapture2::SHUTTER;

    FlyCapture2::Error error = camera_.GetPropertyInfo(&shutterInfo);
    if (
        error != FlyCapture2::PGRERROR_OK ||
        !shutterInfo.present ||
        !shutterInfo.absValSupported
    )
    {
        return false;
    }

    FlyCapture2::Property shutterProperty;
    shutterProperty.type = FlyCapture2::SHUTTER;
    error = camera_.GetProperty(&shutterProperty);
    if (error != FlyCapture2::PGRERROR_OK || !shutterProperty.present)
    {
        return false;
    }

    shutterProperty.autoManualMode = false;
    shutterProperty.onOff = true;
    shutterProperty.absControl = true;
    shutterProperty.absValue = std::max(
        shutterInfo.absMin,
        std::min(milliseconds, shutterInfo.absMax)
    );

    error = camera_.SetProperty(&shutterProperty);
    return error == FlyCapture2::PGRERROR_OK;
}

void FlyCaptureCamera::start()
{
    if (capturing_)
    {
        return;
    }

    throwIfError(camera_.StartCapture(), "StartCapture");
    capturing_ = true;
}

void FlyCaptureCamera::stop() noexcept
{
    if (!capturing_)
    {
        return;
    }

    camera_.StopCapture();
    capturing_ = false;
}

cv::Mat FlyCaptureCamera::grabFrameBgr()
{
    if (!capturing_)
    {
        throw std::logic_error("A captura da camera ainda nao foi iniciada.");
    }

    throwIfError(camera_.RetrieveBuffer(&rawImage_), "RetrieveBuffer");
    throwIfError(
        rawImage_.Convert(FlyCapture2::PIXEL_FORMAT_BGR, &bgrImage_),
        "Convert PIXEL_FORMAT_BGR"
    );

    cv::Mat bgrView(
        static_cast<int>(bgrImage_.GetRows()),
        static_cast<int>(bgrImage_.GetCols()),
        CV_8UC3,
        bgrImage_.GetData(),
        static_cast<std::size_t>(bgrImage_.GetStride())
    );

    // A copia separa a vida util do cv::Mat do buffer interno do FlyCapture2.
    return bgrView.clone();
}

const FlyCapture2::CameraInfo& FlyCaptureCamera::info() const noexcept
{
    return cameraInfo_;
}

void FlyCaptureCamera::throwIfError(
    const FlyCapture2::Error& error,
    const char* operation
)
{
    if (error == FlyCapture2::PGRERROR_OK)
    {
        return;
    }

    std::ostringstream message;
    message << "Erro em " << operation << ": " << error.GetDescription();
    throw std::runtime_error(message.str());
}
