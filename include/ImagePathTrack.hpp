#pragma once

#include <opencv2/core.hpp>

#include <vector>

class ImagePathTracker final
{
public:
    explicit ImagePathTracker(int intensityThreshold = 180);

    void setIntensityThreshold(int intensityThreshold);
    int intensityThreshold() const noexcept;

    std::vector<cv::Point2f> detect(const cv::Mat& bgrFrame) const;
    void draw(cv::Mat& bgrFrame, const std::vector<cv::Point2f>& points) const;

    std::vector<int> findInflectionPoints(const std::vector<float> path, int nInflectionPoints) const;

    std::pair<int,float> RDP(const std::vector<float> points, int x0, int x1) const;
private:
    int intensityThreshold_;
};
