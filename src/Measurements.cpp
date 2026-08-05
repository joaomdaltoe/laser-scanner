/**
 * @file Measurements.cpp
 * @brief Implementa a conversao de pontos de imagem em medidas geometricas.
 */

#include "Measurements.hpp"

#include <cmath>
#include <stdexcept>

Measurements::Measurements(
    double millimetersPerPixel,
    double coordinateOffsetMillimeters
)
    : millimetersPerPixel_(millimetersPerPixel),
      coordinateOffsetMillimeters_(coordinateOffsetMillimeters)
{
    if (!std::isfinite(millimetersPerPixel_))
    {
        throw std::invalid_argument("A escala de calibracao deve ser finita.");
    }

    if (!std::isfinite(coordinateOffsetMillimeters_))
    {
        throw std::invalid_argument("O deslocamento de calibracao deve ser finito.");
    }
}

void Measurements::update(const std::vector<cv::Point2f>& imagePoints)
{
    points_.clear();
    points_.reserve(imagePoints.size());

    for (const cv::Point2f& imagePoint : imagePoints)
    {
        points_.push_back({
            convertPixelCoordinate(static_cast<double>(imagePoint.x)),
            convertPixelCoordinate(static_cast<double>(imagePoint.y))
        });
    }

    y_ = 0.0;
    z_ = 0.0;
    gap_ = 0.0;
    hilo_ = 0.0;
    area_ = polygonArea(points_);

    if (points_.empty())
    {
        return;
    }

    const std::size_t pointCount = points_.size();
    if (pointCount % 2 == 0)
    {
        const std::size_t leftIndex = pointCount / 2 - 1;
        const std::size_t rightIndex = leftIndex + 1;

        y_ = (points_[leftIndex].y + points_[rightIndex].y) * 0.5;
        z_ = (points_[leftIndex].z + points_[rightIndex].z) * 0.5;
        gap_ = std::abs(points_[leftIndex].y - points_[rightIndex].y);

        // Regra da aplicacao C#: para quantidades pares, Hilo usa sempre
        // o quarto e o terceiro pontos, independentemente do par central.
        if (pointCount >= 4)
        {
            hilo_ = std::abs(points_[3].z - points_[2].z);
        }
    }
    else
    {
        const std::size_t middleIndex = pointCount / 2;
        y_ = points_[middleIndex].y;
        z_ = points_[middleIndex].z;

        if (pointCount >= 3)
        {
            gap_ = std::abs(
                points_[middleIndex - 1].y -
                points_[middleIndex + 1].y
            );
            hilo_ = std::abs(
                points_[middleIndex - 1].z -
                points_[middleIndex + 1].z
            );
        }
    }
}

bool Measurements::empty() const noexcept
{
    return points_.empty();
}

double Measurements::get_y() const noexcept
{
    return y_;
}

double Measurements::get_z() const noexcept
{
    return z_;
}

double Measurements::get_gap() const noexcept
{
    return gap_;
}

double Measurements::get_hilo() const noexcept
{
    return hilo_;
}

double Measurements::get_area() const noexcept
{
    return area_;
}

const std::vector<Measurements::Point>& Measurements::get_points() const noexcept
{
    return points_;
}

double Measurements::convertPixelCoordinate(double coordinate) const noexcept
{
    return coordinate * millimetersPerPixel_ +
        coordinateOffsetMillimeters_;
}

double Measurements::polygonArea(
    const std::vector<Measurements::Point>& points
) noexcept
{
    if (points.size() < 5)
    {
        return 0.0;
    }

    const std::size_t firstIndex = 1;
    const std::size_t endIndex = points.size() - 1;
    double signedArea = 0.0;

    for (std::size_t index = firstIndex; index < endIndex; ++index)
    {
        const std::size_t nextIndex = index + 1 < endIndex
            ? index + 1
            : firstIndex;

        signedArea +=
            (points[nextIndex].y - points[index].y) *
            (points[nextIndex].z + points[index].z) * 0.5;
    }

    return std::abs(signedArea);
}
