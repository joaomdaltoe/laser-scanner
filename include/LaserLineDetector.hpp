#pragma once

#include <opencv2/core.hpp>

#include <vector>

class LaserLineDetector final
{
public:
    explicit LaserLineDetector(int intensityThreshold = 180);

    void setIntensityThreshold(int intensityThreshold);
    int intensityThreshold() const noexcept;

    std::vector<cv::Point2f> detect(const cv::Mat& bgrFrame) const;
    void draw(cv::Mat& bgrFrame, const std::vector<cv::Point2f>& points) const;

private:
    int intensityThreshold_;
};
