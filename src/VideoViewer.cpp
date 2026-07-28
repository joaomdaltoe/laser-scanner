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

    int intensityThreshold = 180;
    cv::createTrackbar(
        "Limiar",
        windowName_,
        &intensityThreshold,
        255
    );

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
            "Exposicao (us)",
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

            // Aplica a simplificacao separadamente em cada trecho continuo.
            // Assim, colunas sem deteccao nao sao interpretadas como vizinhas.
            std::size_t segmentBegin = 0;
            while (segmentBegin < laserPoints.size())
            {
                std::size_t segmentEnd = segmentBegin + 1;
                while (
                    segmentEnd < laserPoints.size() &&
                    std::lround(
                        laserPoints[segmentEnd].x -
                        laserPoints[segmentEnd - 1].x
                    ) == 1
                )
                {
                    ++segmentEnd;
                }

                if (segmentEnd - segmentBegin >= 3)
                {
                    std::vector<float> path;
                    path.reserve(segmentEnd - segmentBegin);

                    for (
                        std::size_t index = segmentBegin;
                        index < segmentEnd;
                        ++index
                    )
                    {
                        path.push_back(laserPoints[index].y);
                    }

                    const std::vector<int> inflectionPoints =
                        imagePathTracker.findInflectionPoints(
                            path,
                            nInflectionPoints
                        );

                    for (const int localIndex : inflectionPoints)
                    {
                        if (
                            localIndex < 0 ||
                            localIndex >= static_cast<int>(path.size())
                        )
                        {
                            continue;
                        }

                        const cv::Point2f& point = laserPoints[
                            segmentBegin +
                            static_cast<std::size_t>(localIndex)
                        ];

                        cv::circle(
                            frame,
                            cv::Point(cvRound(point.x), cvRound(point.y)),
                            5,
                            cv::Scalar(0, 255, 0),
                            cv::FILLED,
                            cv::LINE_AA
                        );
                    }
                }

                segmentBegin = segmentEnd;
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
