#pragma once

#include <opencv2/core.hpp>

#include <vector>

class Measurements final
{
public:
    struct Point final
    {
        double y;
        double z;
    };

    explicit Measurements(
        double millimetersPerPixel = -0.09719,
        double coordinateOffsetMillimeters = 30.04304
    );

    void update(const std::vector<cv::Point2f>& imagePoints);

    bool empty() const noexcept;
    double get_y() const noexcept;
    double get_z() const noexcept;
    double get_gap() const noexcept;
    double get_area() const noexcept;
    const std::vector<Point>& get_points() const noexcept;

private:
    double convertPixelCoordinate(double coordinate) const noexcept;
    static double polygonArea(const std::vector<Point>& points) noexcept;

    double millimetersPerPixel_;
    double coordinateOffsetMillimeters_;
    double y_ = 0.0;
    double z_ = 0.0;
    double gap_ = 0.0;
    double area_ = 0.0;
    std::vector<Point> points_;
};
