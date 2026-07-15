#include "VideoViewer.hpp"

#include "FlyCaptureCamera.hpp"

#include <opencv2/highgui.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
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
    camera.start();

    try
    {
        Clock::time_point nextFrameDeadline = Clock::now();

        while (true)
        {
            const cv::Mat frame = camera.grabFrameBgr();
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