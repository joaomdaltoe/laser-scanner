#include "VideoViewer.hpp"

#include "FlyCaptureCamera.hpp"
#include "ImagePathTrack.hpp"
#include "Measurements.hpp"

#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSizePolicy>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
class FrameWidget final : public QLabel
{
public:
    explicit FrameWidget(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(640, 480);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet("background: #111; color: #ddd;");
        setText("Aguardando frame");
    }

    void setFrame(const QImage& image)
    {
        sourcePixmap_ = QPixmap::fromImage(image);
        updateScaledPixmap();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateScaledPixmap();
    }

private:
    void updateScaledPixmap()
    {
        if (sourcePixmap_.isNull())
        {
            return;
        }

        setPixmap(sourcePixmap_.scaled(
            size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        ));
    }

    QPixmap sourcePixmap_;
};

QLabel* createValueLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setMinimumWidth(150);
    return label;
}

QSlider* createHorizontalSlider(int minimum, int maximum, int value)
{
    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setTracking(true);
    return slider;
}

QString formatMillimeters(double value)
{
    return QString("%1 mm").arg(value, 0, 'f', 2);
}

QString formatSquareMillimeters(double value)
{
    return QString("%1 mm^2").arg(value, 0, 'f', 2);
}

QImage matToImage(const cv::Mat& bgrFrame)
{
    cv::Mat rgbFrame;
    cv::cvtColor(bgrFrame, rgbFrame, cv::COLOR_BGR2RGB);

    return QImage(
        rgbFrame.data,
        rgbFrame.cols,
        rgbFrame.rows,
        static_cast<int>(rgbFrame.step),
        QImage::Format_RGB888
    ).copy();
}

std::vector<cv::Point2f> drawInflectionPoints(
    cv::Mat& frame,
    const std::vector<cv::Point2f>& laserPoints,
    int requestedPointCount,
    const ImagePathTracker& imagePathTracker
)
{
    std::vector<cv::Point2f> measurementImagePoints;

    if (laserPoints.empty() || requestedPointCount <= 0)
    {
        return measurementImagePoints;
    }

    const int firstX = cvRound(laserPoints.front().x);
    const int lastX = cvRound(laserPoints.back().x);
    if (lastX < firstX)
    {
        return measurementImagePoints;
    }

    std::vector<float> path(static_cast<std::size_t>(lastX - firstX + 1));
    path.front() = laserPoints.front().y;

    for (std::size_t index = 1; index < laserPoints.size(); ++index)
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
                previous.y + interpolationFactor * (current.y - previous.y);
        }
    }

    const int pointCount = std::min(
        requestedPointCount,
        static_cast<int>(path.size())
    );

    std::vector<int> inflectionPoints;
    if (pointCount == 1)
    {
        inflectionPoints.push_back(static_cast<int>(path.size() / 2));
    }
    else
    {
        // findInflectionPoints inclui os dois extremos. Portanto,
        // solicita-se somente a quantidade de pontos internos.
        inflectionPoints = imagePathTracker.findInflectionPoints(
            path,
            pointCount - 2
        );
    }

    measurementImagePoints.reserve(inflectionPoints.size());
    for (const int pathIndex : inflectionPoints)
    {
        if (pathIndex < 0 || pathIndex >= static_cast<int>(path.size()))
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

    return measurementImagePoints;
}

class CameraMainWindow final : public QMainWindow
{
public:
    CameraMainWindow(
        FlyCaptureCamera& camera,
        const QString& windowName,
        double targetFramesPerSecond,
        QWidget* parent = nullptr
    )
        : QMainWindow(parent),
          camera_(camera),
          framePeriodMilliseconds_(
              std::max(1, static_cast<int>(std::lround(
                  1000.0 / targetFramesPerSecond
              )))
          )
    {
        setWindowTitle(windowName);
        resize(1200, 760);

        configureExposureControl();
        buildUi();

        imagePathTracker_.setIntensityThreshold(thresholdSlider_->value());

        camera_.start();
        connectedLabel_->setText("Camera: conectada");

        connect(&frameTimer_, &QTimer::timeout, this, [this]() {
            processFrame();
        });
        frameTimer_.start(framePeriodMilliseconds_);
        fpsClock_ = Clock::now();
    }

    ~CameraMainWindow() override
    {
        frameTimer_.stop();
        camera_.stop();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Q)
        {
            close();
            return;
        }

        QMainWindow::keyPressEvent(event);
    }

private:
    using Clock = std::chrono::steady_clock;

    void configureExposureControl()
    {
        exposureControlAvailable_ = camera_.getExposureRange(
            minimumExposureMilliseconds_,
            maximumExposureMilliseconds_,
            currentExposureMilliseconds_
        );

        if (!exposureControlAvailable_)
        {
            std::cerr
                << "Aviso: a camera nao oferece controle absoluto de exposicao."
                << std::endl;
            return;
        }

        currentExposureMilliseconds_ = std::max(
            minimumExposureMilliseconds_,
            std::max(currentExposureMilliseconds_, 0.001F)
        );
        exposureControlAvailable_ = camera_.configureExposure(
            currentExposureMilliseconds_
        );

        const double maximumExposureMicroseconds = std::min(
            std::ceil(
                static_cast<double>(maximumExposureMilliseconds_) * 1000.0
            ),
            static_cast<double>(std::numeric_limits<int>::max())
        );

        exposureSliderMaximum_ = std::max(
            1,
            static_cast<int>(maximumExposureMicroseconds)
        );
        exposureMicroseconds_ = static_cast<int>(std::lround(
            static_cast<double>(currentExposureMilliseconds_) * 1000.0
        ));
        exposureMicroseconds_ = std::max(
            0,
            std::min(exposureMicroseconds_, exposureSliderMaximum_)
        );
        previousExposureMicroseconds_ = exposureMicroseconds_;
    }

    void buildUi()
    {
        frameWidget_ = new FrameWidget(this);

        QWidget* sidePanel = new QWidget(this);
        sidePanel->setMinimumWidth(260);
        sidePanel->setMaximumWidth(360);

        QVBoxLayout* sideLayout = new QVBoxLayout(sidePanel);
        sideLayout->setContentsMargins(14, 14, 14, 14);
        sideLayout->setSpacing(14);

        QGroupBox* controlsGroup = new QGroupBox("Controles", sidePanel);
        QVBoxLayout* controlsLayout = new QVBoxLayout(controlsGroup);

        exposureValueLabel_ = createValueLabel("");
        exposureSlider_ = createHorizontalSlider(
            0,
            exposureSliderMaximum_,
            exposureMicroseconds_
        );
        exposureSlider_->setEnabled(exposureControlAvailable_);
        connect(exposureSlider_, &QSlider::valueChanged, this, [this](int value) {
            exposureMicroseconds_ = value;
            updateControlLabels();
        });

        thresholdValueLabel_ = createValueLabel("");
        thresholdSlider_ = createHorizontalSlider(0, 255, 240);
        connect(thresholdSlider_, &QSlider::valueChanged, this, [this](int) {
            updateControlLabels();
        });

        pointsValueLabel_ = createValueLabel("");
        pointsSlider_ = createHorizontalSlider(0, 10, 3);
        connect(pointsSlider_, &QSlider::valueChanged, this, [this](int) {
            updateControlLabels();
        });

        controlsLayout->addWidget(new QLabel("Exposicao", controlsGroup));
        controlsLayout->addWidget(exposureSlider_);
        controlsLayout->addWidget(exposureValueLabel_);
        controlsLayout->addSpacing(8);
        controlsLayout->addWidget(new QLabel("Threshold", controlsGroup));
        controlsLayout->addWidget(thresholdSlider_);
        controlsLayout->addWidget(thresholdValueLabel_);
        controlsLayout->addSpacing(8);
        controlsLayout->addWidget(new QLabel("nPontos", controlsGroup));
        controlsLayout->addWidget(pointsSlider_);
        controlsLayout->addWidget(pointsValueLabel_);

        QGroupBox* measurementsGroup = new QGroupBox("Medicoes", sidePanel);
        QVBoxLayout* measurementsLayout = new QVBoxLayout(measurementsGroup);
        detectedPointsLabel_ = createValueLabel("Pontos detectados: 0");
        inflectionPointsLabel_ = createValueLabel("Pontos marcados: 0");
        frameSizeLabel_ = createValueLabel("Frame: -");
        yLabel_ = createValueLabel("Y: -");
        zLabel_ = createValueLabel("Z: -");
        gapLabel_ = createValueLabel("Gap: -");
        areaLabel_ = createValueLabel("Area: -");
        measurementsLayout->addWidget(detectedPointsLabel_);
        measurementsLayout->addWidget(inflectionPointsLabel_);
        measurementsLayout->addWidget(frameSizeLabel_);
        measurementsLayout->addSpacing(8);
        measurementsLayout->addWidget(yLabel_);
        measurementsLayout->addWidget(zLabel_);
        measurementsLayout->addWidget(gapLabel_);
        measurementsLayout->addWidget(areaLabel_);

        sideLayout->addWidget(controlsGroup);
        sideLayout->addWidget(measurementsGroup);
        sideLayout->addStretch(1);

        QWidget* central = new QWidget(this);
        QHBoxLayout* centralLayout = new QHBoxLayout(central);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);
        centralLayout->addWidget(frameWidget_, 1);
        centralLayout->addWidget(sidePanel);
        setCentralWidget(central);

        connectedLabel_ = new QLabel("Camera: desconectada", this);
        fpsLabel_ = new QLabel("FPS: 0.0", this);
        laserLabel_ = new QLabel("Laser: inativo", this);
        statusBar()->addPermanentWidget(connectedLabel_);
        statusBar()->addPermanentWidget(fpsLabel_);
        statusBar()->addPermanentWidget(laserLabel_);

        updateControlLabels();
        updateMeasurementLabels();
    }

    void processFrame()
    {
        try
        {
            imagePathTracker_.setIntensityThreshold(thresholdSlider_->value());
            applyExposureIfNeeded();

            cv::Mat frame = camera_.grabFrameBgr();
            const std::vector<cv::Point2f> laserPoints =
                imagePathTracker_.detect(frame);
            imagePathTracker_.draw(frame, laserPoints);

            const std::vector<cv::Point2f> measurementImagePoints =
                drawInflectionPoints(
                    frame,
                    laserPoints,
                    pointsSlider_->value(),
                    imagePathTracker_
                );
            measurements_.update(measurementImagePoints);

            frameWidget_->setFrame(matToImage(frame));
            detectedPointsLabel_->setText(QString("Pontos detectados: %1").arg(
                static_cast<qulonglong>(laserPoints.size())
            ));
            inflectionPointsLabel_->setText(QString("Pontos marcados: %1").arg(
                static_cast<qulonglong>(measurementImagePoints.size())
            ));
            frameSizeLabel_->setText(QString("Frame: %1 x %2").arg(
                frame.cols
            ).arg(frame.rows));
            updateMeasurementLabels();
            laserLabel_->setText(
                laserPoints.empty() ? "Laser: inativo" : "Laser: ativo"
            );
            connectedLabel_->setText("Camera: conectada");

            updateFps();
        }
        catch (const std::exception& exception)
        {
            std::cerr
                << "Falha ao capturar frame: " << exception.what()
                << std::endl;
            connectedLabel_->setText("Camera: desconectada");
            laserLabel_->setText("Laser: inativo");
            frameTimer_.stop();
            camera_.stop();
        }
    }

    void applyExposureIfNeeded()
    {
        if (
            !exposureControlAvailable_ ||
            exposureMicroseconds_ == previousExposureMicroseconds_
        )
        {
            return;
        }

        const float requestedExposureMilliseconds = std::max(
            std::max(minimumExposureMilliseconds_, 0.001F),
            static_cast<float>(exposureMicroseconds_) / 1000.0F
        );

        if (!camera_.configureExposure(requestedExposureMilliseconds))
        {
            std::cerr
                << "Aviso: a camera rejeitou a exposicao solicitada."
                << std::endl;
        }

        previousExposureMicroseconds_ = exposureMicroseconds_;
    }

    void updateControlLabels()
    {
        exposureValueLabel_->setText(
            exposureControlAvailable_
            ? QString("%1 us").arg(exposureSlider_->value())
            : QString("indisponivel")
        );
        thresholdValueLabel_->setText(QString::number(
            thresholdSlider_->value()
        ));
        pointsValueLabel_->setText(QString::number(pointsSlider_->value()));
    }

    void updateMeasurementLabels()
    {
        if (measurements_.empty())
        {
            yLabel_->setText("Y: -");
            zLabel_->setText("Z: -");
            gapLabel_->setText("Gap: -");
            areaLabel_->setText("Area: -");
            return;
        }

        yLabel_->setText("Y: " + formatMillimeters(measurements_.get_y()));
        zLabel_->setText("Z: " + formatMillimeters(measurements_.get_z()));
        gapLabel_->setText("Gap: " + formatMillimeters(measurements_.get_gap()));
        areaLabel_->setText(
            "Area: " + formatSquareMillimeters(measurements_.get_area())
        );
    }

    void updateFps()
    {
        ++framesSinceLastFpsUpdate_;
        const Clock::time_point now = Clock::now();
        const std::chrono::duration<double> elapsed = now - fpsClock_;
        if (elapsed.count() < 0.5)
        {
            return;
        }

        const double fps =
            static_cast<double>(framesSinceLastFpsUpdate_) / elapsed.count();
        fpsLabel_->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
        framesSinceLastFpsUpdate_ = 0;
        fpsClock_ = now;
    }

    FlyCaptureCamera& camera_;
    ImagePathTracker imagePathTracker_;
    Measurements measurements_;
    QTimer frameTimer_;
    FrameWidget* frameWidget_ = nullptr;
    QSlider* exposureSlider_ = nullptr;
    QSlider* thresholdSlider_ = nullptr;
    QSlider* pointsSlider_ = nullptr;
    QLabel* exposureValueLabel_ = nullptr;
    QLabel* thresholdValueLabel_ = nullptr;
    QLabel* pointsValueLabel_ = nullptr;
    QLabel* detectedPointsLabel_ = nullptr;
    QLabel* inflectionPointsLabel_ = nullptr;
    QLabel* frameSizeLabel_ = nullptr;
    QLabel* yLabel_ = nullptr;
    QLabel* zLabel_ = nullptr;
    QLabel* gapLabel_ = nullptr;
    QLabel* areaLabel_ = nullptr;
    QLabel* connectedLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* laserLabel_ = nullptr;
    int framePeriodMilliseconds_ = 33;
    int framesSinceLastFpsUpdate_ = 0;
    Clock::time_point fpsClock_ = Clock::now();
    float minimumExposureMilliseconds_ = 0.0F;
    float maximumExposureMilliseconds_ = 0.0F;
    float currentExposureMilliseconds_ = 0.0F;
    bool exposureControlAvailable_ = false;
    int exposureSliderMaximum_ = 1;
    int exposureMicroseconds_ = 0;
    int previousExposureMicroseconds_ = 0;
};
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
    QApplication* application = qobject_cast<QApplication*>(
        QApplication::instance()
    );
    if (application == nullptr)
    {
        throw std::logic_error("A aplicacao Qt deve ser criada antes da UI.");
    }

    CameraMainWindow mainWindow(
        camera,
        QString::fromStdString(windowName_),
        targetFramesPerSecond_
    );
    mainWindow.show();

    application->exec();
}
