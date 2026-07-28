#include "VideoViewer.hpp"

#include "FlyCaptureCamera.hpp"
#include "ImagePathTrack.hpp"
#include "Measurements.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
constexpr int measurementsPanelWidth = 360;
constexpr int measurementsPanelMargin = 14;
constexpr int measurementsLineHeight = 22;

std::string formatMeasurement(double value, const char* unit)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value << ' ' << unit;
    return stream.str();
}

void drawMeasurementsPanel(
    cv::Mat& displayFrame,
    int panelLeft,
    const Measurements& measurements
)
{
    const cv::Scalar white(255, 255, 255);
    const cv::Scalar gray(140, 140, 140);
    const cv::Scalar green(0, 255, 0);
    const int textLeft = panelLeft + measurementsPanelMargin;
    int textY = measurementsPanelMargin + measurementsLineHeight;

    cv::line(
        displayFrame,
        cv::Point(panelLeft, 0),
        cv::Point(panelLeft, displayFrame.rows - 1),
        gray,
        1,
        cv::LINE_AA
    );

    const auto drawLine = [&](const std::string& text, const cv::Scalar& color)
    {
        if (textY < displayFrame.rows - measurementsPanelMargin)
        {
            cv::putText(
                displayFrame,
                text,
                cv::Point(textLeft, textY),
                cv::FONT_HERSHEY_SIMPLEX,
                0.52,
                color,
                1,
                cv::LINE_AA
            );
        }

        textY += measurementsLineHeight;
    };

    drawLine("Medicoes", green);

    if (measurements.empty())
    {
        drawLine("Sem pontos para medir", white);
        return;
    }

    drawLine("Y: " + formatMeasurement(measurements.get_y(), "mm"), white);
    drawLine("Z: " + formatMeasurement(measurements.get_z(), "mm"), white);
    drawLine("Gap: " + formatMeasurement(measurements.get_gap(), "mm"), white);
    drawLine("Area: " + formatMeasurement(measurements.get_area(), "mm^2"), white);

    textY += measurementsLineHeight / 2;
    const std::vector<Measurements::Point>& points = measurements.get_points();
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        std::ostringstream line;
        line << "Ponto " << index
             << "  Y: " << std::fixed << std::setprecision(2)
             << points[index].y << " mm"
             << "  Z: " << points[index].z << " mm";
        drawLine(line.str(), white);
    }
}
}

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
    Measurements measurements;
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

            std::vector<cv::Point2f> measurementImagePoints;

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

                    const cv::Point2f measurementImagePoint(
                        static_cast<float>(firstX + pathIndex),
                        path[static_cast<std::size_t>(pathIndex)]
                    );
                    measurementImagePoints.push_back(measurementImagePoint);

                    cv::circle(
                        frame,
                        cv::Point(
                            cvRound(measurementImagePoint.x),
                            cvRound(measurementImagePoint.y)
                        ),
                        5,
                        cv::Scalar(0, 255, 0),
                        cv::FILLED,
                        cv::LINE_AA
                    );
                }
            }

            measurements.update(measurementImagePoints);

            cv::Mat displayFrame(
                frame.rows,
                frame.cols + measurementsPanelWidth,
                frame.type(),
                cv::Scalar(0, 0, 0)
            );
            frame.copyTo(displayFrame(cv::Rect(0, 0, frame.cols, frame.rows)));
            drawMeasurementsPanel(displayFrame, frame.cols, measurements);

            cv::imshow(windowName_, displayFrame);

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
