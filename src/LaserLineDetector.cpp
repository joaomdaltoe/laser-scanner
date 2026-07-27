#include "LaserLineDetector.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
constexpr int pixelsToSearch = 2;
constexpr int smoothWindowRadius = 2;
constexpr float distancePenalty = 1.0e-2F;
constexpr float movementPenalty = 1.0e-2F;
constexpr float inverseMaximumIntensity = 1.0F / 255.0F;
}

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

    const int width = intensity.cols;
    const int height = intensity.rows;

    // Cada linha desta matriz corresponde a uma coluna da imagem original. Isso
    // deixa os pixels percorridos pelo rastreador contiguos na memoria.
    cv::Mat intensityByColumn;
    cv::transpose(intensity, intensityByColumn);

    std::vector<float> previousScores(static_cast<std::size_t>(height));
    std::vector<float> currentScores(static_cast<std::size_t>(height));

    // Guarda, para cada estado (x, y), qual deslocamento levou ao melhor caminho.
    // O deslocamento [-2, 2] e armazenado como [0, 4].
    cv::Mat1b predecessors(
        width,
        height,
        static_cast<unsigned char>(pixelsToSearch)
    );

    const unsigned char* firstColumn = intensityByColumn.ptr<unsigned char>(0);
    for (int y = 0; y < height; ++y)
    {
        previousScores[static_cast<std::size_t>(y)] =
            firstColumn[y] > intensityThreshold_
            ? static_cast<float>(firstColumn[y]) * inverseMaximumIntensity
            : 0.0F;
    }

    std::array<float, 2 * pixelsToSearch + 1> transitionPenalties;
    for (int offset = -pixelsToSearch; offset <= pixelsToSearch; ++offset)
    {
        transitionPenalties[static_cast<std::size_t>(offset + pixelsToSearch)] =
            std::sqrt(1.0F + static_cast<float>(offset * offset)) *
                distancePenalty +
            (offset == 0 ? 0.0F : movementPenalty);
    }

    for (int x = 1; x < width; ++x)
    {
        const unsigned char* column =
            intensityByColumn.ptr<unsigned char>(x);

        for (int y = 0; y < height; ++y)
        {
            float bestScore = -std::numeric_limits<float>::infinity();
            int bestOffset = 0;

            for (
                int offset = -pixelsToSearch;
                offset <= pixelsToSearch;
                ++offset
            )
            {
                const int previousY = y + offset;
                if (previousY < 0 || previousY >= height)
                {
                    continue;
                }

                const float candidateScore =
                    previousScores[static_cast<std::size_t>(previousY)] -
                    transitionPenalties[static_cast<std::size_t>(
                        offset + pixelsToSearch
                    )];

                if (candidateScore > bestScore)
                {
                    bestScore = candidateScore;
                    bestOffset = offset;
                }
            }

            const float pixelScore = column[y] > intensityThreshold_
                ? static_cast<float>(column[y]) * inverseMaximumIntensity
                : 0.0F;

            currentScores[static_cast<std::size_t>(y)] =
                bestScore + pixelScore;
            predecessors(x, y) = static_cast<unsigned char>(
                bestOffset + pixelsToSearch
            );
        }

        previousScores.swap(currentScores);
    }

    const std::vector<float>::const_iterator bestLastPosition =
        std::max_element(previousScores.cbegin(), previousScores.cend());

    std::vector<int> coarsePath(static_cast<std::size_t>(width));
    coarsePath.back() = static_cast<int>(
        std::distance(previousScores.cbegin(), bestLastPosition)
    );

    // Reconstrucao do melhor caminho global usando exatamente as transicoes que
    // venceram durante a integracao.
    for (int x = width - 1; x > 0; --x)
    {
        const int currentY = coarsePath[static_cast<std::size_t>(x)];
        const int offset =
            static_cast<int>(predecessors(x, currentY)) - pixelsToSearch;
        coarsePath[static_cast<std::size_t>(x - 1)] = currentY + offset;
    }

    std::vector<float> refinedPath(static_cast<std::size_t>(width));
    std::vector<unsigned char> validPath(
        static_cast<std::size_t>(width),
        0
    );

    // Refina o pixel escolhido pelo caminho global calculando o centroide da
    // faixa continua que contem esse pixel. O resultado pode ser subpixel.
    for (int x = 0; x < width; ++x)
    {
        const unsigned char* column =
            intensityByColumn.ptr<unsigned char>(x);
        const int coarseY = coarsePath[static_cast<std::size_t>(x)];

        if (column[coarseY] <= intensityThreshold_)
        {
            continue;
        }

        int firstY = coarseY;
        int lastY = coarseY;
        while (
            firstY > 0 &&
            column[firstY - 1] > intensityThreshold_
        )
        {
            --firstY;
        }
        while (
            lastY + 1 < height &&
            column[lastY + 1] > intensityThreshold_
        )
        {
            ++lastY;
        }

        double totalWeight = 0.0;
        double weightedY = 0.0;
        for (int y = firstY; y <= lastY; ++y)
        {
            const double weight = static_cast<double>(
                static_cast<int>(column[y]) - intensityThreshold_
            );
            totalWeight += weight;
            weightedY += weight * static_cast<double>(y);
        }

        if (totalWeight > 0.0)
        {
            refinedPath[static_cast<std::size_t>(x)] = static_cast<float>(
                weightedY / totalWeight
            );
            validPath[static_cast<std::size_t>(x)] = 1;
        }
    }

    std::vector<cv::Point2f> centerline;
    centerline.reserve(static_cast<std::size_t>(width));

    // Pontos ausentes nao participam da media e continuam formando lacunas no desenho.
    for (int x = 0; x < width; ++x)
    {
        if (validPath[static_cast<std::size_t>(x)] == 0)
        {
            continue;
        }

        const int firstX = std::max(0, x - smoothWindowRadius);
        const int lastX = std::min(width - 1, x + smoothWindowRadius);
        double ySum = 0.0;
        int sampleCount = 0;

        for (int neighborX = firstX; neighborX <= lastX; ++neighborX)
        {
            if (validPath[static_cast<std::size_t>(neighborX)] != 0)
            {
                ySum += refinedPath[static_cast<std::size_t>(neighborX)];
                ++sampleCount;
            }
        }

        if (sampleCount > 0)
        {
            centerline.emplace_back(
                static_cast<float>(x),
                static_cast<float>(ySum / static_cast<double>(sampleCount))
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
