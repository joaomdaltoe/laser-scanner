#include "LaserLineDetector.hpp"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <stdexcept>

LaserLineDetector::LaserLineDetector(int intensityThreshold)
    : intensityThreshold_(0)
{
    setIntensityThreshold(intensityThreshold);
}

void LaserLineDetector::setIntensityThreshold(int intensityThreshold)
{
    if (intensityThreshold < 0 || intensityThreshold > 255)
    {
        throw std::out_of_range("O limiar de intensidade deve estar entre 0 e 255.");
    }

    intensityThreshold_ = intensityThreshold;
}

int LaserLineDetector::intensityThreshold() const noexcept
{
    return intensityThreshold_;
}

std::vector<cv::Point2f> LaserLineDetector::detect(const cv::Mat& bgrFrame) const
{
    if (bgrFrame.empty())
    {
        return {};
    }

    if (bgrFrame.type() != CV_8UC3)
    {
        throw std::invalid_argument("A deteccao espera um frame BGR CV_8UC3.");
    }

    cv::Mat intensity;
    // A camera e monocromatica, portanto os tres canais BGR sao equivalentes.
    cv::extractChannel(bgrFrame, intensity, 0);
    cv::GaussianBlur(intensity, intensity, cv::Size(3, 3), 0.0);

    std::vector<cv::Point2f> centerline;
    centerline.reserve(static_cast<std::size_t>(intensity.cols));

    // A linha e horizontal: para cada coluna, seleciona a faixa continua mais
    // energetica acima do limiar e calcula seu centro ponderado pela intensidade.
    for (int x = 0; x < intensity.cols; ++x)
    {
        double bestWeight = 0.0;
        double bestWeightedY = 0.0;
        double runWeight = 0.0;
        double runWeightedY = 0.0;

        for (int y = 0; y <= intensity.rows; ++y)
        {
            const int value = y < intensity.rows
                ? static_cast<int>(intensity.at<unsigned char>(y, x))
                : 0;

            if (value > intensityThreshold_)
            {
                const double weight = static_cast<double>(
                    value - intensityThreshold_
                );
                runWeight += weight;
                runWeightedY += weight * static_cast<double>(y);
            }
            else
            {
                if (runWeight > bestWeight)
                {
                    bestWeight = runWeight;
                    bestWeightedY = runWeightedY;
                }

                runWeight = 0.0;
                runWeightedY = 0.0;
            }
        }

        if (bestWeight > 0.0)
        {
            centerline.emplace_back(
                static_cast<float>(x),
                static_cast<float>(bestWeightedY / bestWeight)
            );
        }
    }

    return centerline;
}

void LaserLineDetector::draw(
    cv::Mat& bgrFrame,
    const std::vector<cv::Point2f>& points
) const
{
    if (bgrFrame.empty() || points.empty())
    {
        return;
    }

    const cv::Scalar red(0, 0, 255);

    for (std::size_t index = 1; index < points.size(); ++index)
    {
        const cv::Point2f& previous = points[index - 1];
        const cv::Point2f& current = points[index];

        // Nao une trechos separados por colunas sem deteccao.
        if (std::lround(current.x - previous.x) == 1)
        {
            const cv::Point previousPixel(
                cvRound(previous.x),
                cvRound(previous.y)
            );
            const cv::Point currentPixel(
                cvRound(current.x),
                cvRound(current.y)
            );
            cv::line(
                bgrFrame,
                previousPixel,
                currentPixel,
                red,
                2,
                cv::LINE_AA
            );
        }
    }
}
