#include "VideoViewer.hpp"

#include "FlyCaptureCamera.hpp"
#include "ImagePathTrack.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

VideoViewer::VideoViewer(
    std::string windowName,
    double targetFramesPerSecond
)
    : windowName_(std::move(windowName)),
      targetFramesPerSecond_(targetFramesPerSecond)
{
    if (windowName_.empty())
    {
        throw std::invalid_argument("O nome da janela nao pode ser vazio.");
    }

    if (targetFramesPerSecond_ <= 0.0)
    {
        throw std::invalid_argument("O frame rate deve ser maior que zero.");
    }
}

void VideoViewer::run(FlyCaptureCamera& camera) const
{
    using Clock = std::chrono::steady_clock;

    const Clock::duration framePeriod =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / targetFramesPerSecond_)
        );

    cv::namedWindow(windowName_, cv::WINDOW_AUTOSIZE);

    int intensityThreshold = 240;

    int nInflectionPoints = 3;
    cv::createTrackbar(
        "N Pontos de Inflexão",
        windowName_,
        &nInflectionPoints,
        10
    );

    float minimumExposureMilliseconds = 0.0F;
    float maximumExposureMilliseconds = 0.0F;
    float currentExposureMilliseconds = 0.0F;
    bool exposureControlAvailable = camera.getExposureRange(
        minimumExposureMilliseconds,
        maximumExposureMilliseconds,
        currentExposureMilliseconds
    );

    if (exposureControlAvailable)
    {
        currentExposureMilliseconds = std::max(
            minimumExposureMilliseconds,
            std::max(currentExposureMilliseconds, 0.001F)
        );
        exposureControlAvailable = camera.configureExposure(
            currentExposureMilliseconds
        );
    }

    int exposureMicroseconds = 0;
    int previousExposureMicroseconds = 0;
    if (exposureControlAvailable)
    {
        const double maximumExposureMicroseconds = std::min(
            std::ceil(static_cast<double>(maximumExposureMilliseconds) * 1000.0),
            static_cast<double>(std::numeric_limits<int>::max())
        );
        const int exposureTrackbarMaximum = std::max(
            1,
            static_cast<int>(maximumExposureMicroseconds)
        );

        exposureMicroseconds = static_cast<int>(std::lround(
            static_cast<double>(currentExposureMilliseconds) * 1000.0
        ));
        exposureMicroseconds = std::max(
            0,
            std::min(exposureMicroseconds, exposureTrackbarMaximum)
        );
        previousExposureMicroseconds = exposureMicroseconds;

        cv::createTrackbar(
            "Exposicao (µs)",
            windowName_,
            &exposureMicroseconds,
            exposureTrackbarMaximum
        );
    }
    else
    {
        std::cerr
            << "Aviso: a camera nao oferece controle absoluto de exposicao."
            << std::endl;
    }

    ImagePathTracker imagePathTracker(intensityThreshold);
    camera.start();

    try
    {
        Clock::time_point nextFrameDeadline = Clock::now();

        while (true)
        {
            imagePathTracker.setIntensityThreshold(intensityThreshold);

            if (
                exposureControlAvailable &&
                exposureMicroseconds != previousExposureMicroseconds
            )
            {
                const float requestedExposureMilliseconds = std::max(
                    std::max(minimumExposureMilliseconds, 0.001F),
                    static_cast<float>(exposureMicroseconds) / 1000.0F
                );

                if (!camera.configureExposure(requestedExposureMilliseconds))
                {
                    std::cerr
                        << "Aviso: a camera rejeitou a exposicao solicitada."
                        << std::endl;
                }

                previousExposureMicroseconds = exposureMicroseconds;
            }

            cv::Mat frame = camera.grabFrameBgr();
            const std::vector<cv::Point2f> laserPoints =
                imagePathTracker.detect(frame);
            imagePathTracker.draw(frame, laserPoints);

            // Reconstroi um unico caminho entre a primeira e a ultima deteccao.
            // Lacunas causadas pela exposicao sao preenchidas por interpolacao,
            // evitando executar a RDP uma vez para cada fragmento da linha.
            if (!laserPoints.empty() && nInflectionPoints > 0)
            {
                const int firstX = cvRound(laserPoints.front().x);
                const int lastX = cvRound(laserPoints.back().x);

                std::vector<float> path(
                    static_cast<std::size_t>(lastX - firstX + 1)
                );
                path.front() = laserPoints.front().y;

                for (
                    std::size_t index = 1;
                    index < laserPoints.size();
                    ++index
                )
                {
                    const cv::Point2f& previous = laserPoints[index - 1];
                    const cv::Point2f& current = laserPoints[index];
                    const int previousX = cvRound(previous.x);
                    const int currentX = cvRound(current.x);
                    const int horizontalDistance = currentX - previousX;

                    if (horizontalDistance <= 0)
                    {
                        continue;
                    }

                    for (int x = previousX + 1; x <= currentX; ++x)
                    {
                        const float interpolationFactor =
                            static_cast<float>(x - previousX) /
                            static_cast<float>(horizontalDistance);

                        path[static_cast<std::size_t>(x - firstX)] =
                            previous.y + interpolationFactor *
                            (current.y - previous.y);
                    }
                }

                const int pointCount = std::min(
                    nInflectionPoints,
                    static_cast<int>(path.size())
                );

                std::vector<int> inflectionPoints;
                if (pointCount == 1)
                {
                    inflectionPoints.push_back(
                        static_cast<int>(path.size() / 2)
                    );
                }
                else
                {
                    // findInflectionPoints inclui os dois extremos. Portanto,
                    // solicita-se somente a quantidade de pontos internos.
                    inflectionPoints =
                        imagePathTracker.findInflectionPoints(
                            path,
                            pointCount - 2
                        );
                }

                for (const int pathIndex : inflectionPoints)
                {
                    if (
                        pathIndex < 0 ||
                        pathIndex >= static_cast<int>(path.size())
                    )
                    {
                        continue;
                    }

                    cv::circle(
                        frame,
                        cv::Point(
                            firstX + pathIndex,
                            cvRound(path[static_cast<std::size_t>(pathIndex)])
                        ),
                        5,
                        cv::Scalar(0, 255, 0),
                        cv::FILLED,
                        cv::LINE_AA
                    );
                }
            }

            cv::imshow(windowName_, frame);

            nextFrameDeadline += framePeriod;
            const Clock::time_point now = Clock::now();

            int waitMilliseconds = 1;
            if (nextFrameDeadline > now)
            {
                const std::int64_t remainingMilliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        nextFrameDeadline - now
                    ).count();

                waitMilliseconds = static_cast<int>(
                    std::max<std::int64_t>(1, remainingMilliseconds)
                );
            }
            else
            {
                // Evita acumular atraso quando captura/conversao excede 33,3 ms.
                nextFrameDeadline = now;
            }

            const int key = cv::waitKey(waitMilliseconds);
            if (key == 27 || key == 'q' || key == 'Q')
            {
                break;
            }
        }
    }
    catch (...)
    {
        camera.stop();
        cv::destroyWindow(windowName_);
        throw;
    }

    camera.stop();
    cv::destroyWindow(windowName_);
}
