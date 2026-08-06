/**
 * @file VideoViewer.cpp
 * @brief Implementa a interface Qt de visualizacao, controle e medicao.
 */

#include "VideoViewer.hpp"

#include "FlyCaptureCamera.hpp"
#include "ImagePathTrack.hpp"
#include "Measurements.hpp"
#include "MqttPublisher.hpp"

#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QAbstractButton>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QImageWriter>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QTimeZone>
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
QDateTime saoPauloDateTime()
{
    const QDateTime utcNow = QDateTime::currentDateTimeUtc();
    const QTimeZone saoPauloZone(QByteArrayLiteral("America/Sao_Paulo"));
    if (saoPauloZone.isValid())
    {
        return utcNow.toTimeZone(saoPauloZone);
    }

    // Fallback para imagens Linux sem a base de fusos horarios. Sao Paulo
    // permanece em UTC-3 desde 2019.
    return utcNow.addSecs(-3 * 60 * 60);
}

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
          targetFramesPerSecond_(targetFramesPerSecond),
          mqttConfig_(MqttConfig::fromEnvironment()),
          mqttPublisher_(mqttConfig_),
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

        // Este timer controla exclusivamente o rate do topico MQTT. O valor
        // pode ser alterado em MQTT_PUBLISH_INTERVAL_MS no docker-compose.yml,
        // sem recompilação necessária.
        connect(&mqttPublishTimer_, &QTimer::timeout, this, [this]() {
            publishMeasurements();
        });
        mqttPublishTimer_.setTimerType(Qt::PreciseTimer);
        mqttPublishTimer_.start(mqttConfig_.publishIntervalMilliseconds);

        fpsClock_ = Clock::now();
    }

    ~CameraMainWindow() override
    {
        mqttPublishTimer_.stop();
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

    void publishMeasurements()
    {
        const std::vector<Measurements::Point>& points =
            measurements_.get_points();

        // Por requisito, frames com menos (ou mais) de cinco pontos nao geram
        // mensagem. Nao se repete a ultima medicao valida.
        if (points.size() != 5)
        {
            return;
        }

        mqttPublisher_.publishPoints(points);
    }

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
        pointsSlider_ = createHorizontalSlider(1, 10, 5);
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
        controlsLayout->addWidget(new QLabel("nPoints", controlsGroup));
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
        hiloLabel_ = createValueLabel("Hilo: -");
        areaLabel_ = createValueLabel("Area: -");
        measurementsLayout->addWidget(detectedPointsLabel_);
        measurementsLayout->addWidget(inflectionPointsLabel_);
        measurementsLayout->addWidget(frameSizeLabel_);
        measurementsLayout->addSpacing(8);
        measurementsLayout->addWidget(yLabel_);
        measurementsLayout->addWidget(zLabel_);
        measurementsLayout->addWidget(gapLabel_);
        measurementsLayout->addWidget(hiloLabel_);
        measurementsLayout->addWidget(areaLabel_);

        measurementsLayout->addSpacing(8);
        measurementsLayout->addWidget(new QLabel(
            "Coordenadas dos pontos",
            measurementsGroup
        ));

        pointsListWidget_ = new QWidget(measurementsGroup);
        pointsListLayout_ = new QVBoxLayout(pointsListWidget_);
        pointsListLayout_->setContentsMargins(0, 0, 0, 0);
        pointsListLayout_->setSpacing(4);
        noPointsLabel_ = createValueLabel("Nenhum ponto disponivel");
        pointsListLayout_->addWidget(noPointsLabel_);

        QScrollArea* pointsScrollArea = new QScrollArea(measurementsGroup);
        pointsScrollArea->setWidgetResizable(true);
        pointsScrollArea->setHorizontalScrollBarPolicy(
            Qt::ScrollBarAlwaysOff
        );
        pointsScrollArea->setMinimumHeight(100);
        pointsScrollArea->setMaximumHeight(190);
        pointsScrollArea->setWidget(pointsListWidget_);
        measurementsLayout->addWidget(pointsScrollArea);

        sideLayout->addWidget(controlsGroup);
        sideLayout->addWidget(measurementsGroup);
        printscreenButton_ = new QPushButton("Salvar Imagem", sidePanel);
        printscreenButton_->setEnabled(false);
        connect(printscreenButton_, &QPushButton::clicked, this, [this]() {
            savePrintscreen();
        });
        sideLayout->addWidget(printscreenButton_);
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

            lastFrameImage_ = matToImage(frame);
            lastFrameCapturedAt_ = saoPauloDateTime();
            lastDetectedPointCount_ = laserPoints.size();
            lastMarkedPointCount_ = measurementImagePoints.size();
            frameWidget_->setFrame(lastFrameImage_);
            printscreenButton_->setEnabled(true);
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
            showCameraDisconnectedDialog(
                QString::fromLocal8Bit(exception.what())
            );
        }
    }

    void showCameraDisconnectedDialog(const QString& errorMessage)
    {
        if (recoveryDialogOpen_)
        {
            return;
        }

        recoveryDialogOpen_ = true;

        QMessageBox dialog(
            QMessageBox::Critical,
            "Camera desconectada",
            QString("A comunicacao com a camera foi interrompida."
                    "\n\nDetalhes: %1").arg(errorMessage),
            QMessageBox::NoButton,
            this
        );
        QAbstractButton* reconnectButton = dialog.addButton(
            "Reconectar",
            QMessageBox::AcceptRole
        );
        QAbstractButton* closeButton = dialog.addButton(
            "Fechar aplicativo",
            QMessageBox::RejectRole
        );
        dialog.setDefaultButton(
            qobject_cast<QPushButton*>(reconnectButton)
        );
        dialog.exec();

        const bool reconnectRequested =
            dialog.clickedButton() == reconnectButton;
        recoveryDialogOpen_ = false;

        if (!reconnectRequested || dialog.clickedButton() == closeButton)
        {
            close();
            return;
        }

        reconnectCamera();
    }

    void reconnectCamera()
    {
        connectedLabel_->setText("Camera: reconectando...");
        statusBar()->showMessage("Procurando a camera pelo numero serial...");
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        try
        {
            camera_.reconnect();
            if (!camera_.configureFrameRate(
                static_cast<float>(targetFramesPerSecond_)
            ))
            {
                std::cerr
                    << "Aviso: a camera reconectada nao aceitou o frame rate."
                    << std::endl;
            }

            configureExposureControl();
            refreshExposureControls();
            camera_.start();

            connectedLabel_->setText("Camera: conectada");
            laserLabel_->setText("Laser: inativo");
            framesSinceLastFpsUpdate_ = 0;
            fpsClock_ = Clock::now();
            frameTimer_.start(framePeriodMilliseconds_);
            statusBar()->showMessage("Camera reconectada.", 3000);
            QApplication::restoreOverrideCursor();
        }
        catch (const std::exception& exception)
        {
            QApplication::restoreOverrideCursor();
            connectedLabel_->setText("Camera: desconectada");
            std::cerr
                << "Falha ao reconectar camera: " << exception.what()
                << std::endl;

            // Adia o novo dialogo para fora do fechamento do dialogo anterior.
            const QString details = QString::fromLocal8Bit(exception.what());
            QTimer::singleShot(0, this, [this, details]() {
                showCameraDisconnectedDialog(details);
            });
        }
    }

    void refreshExposureControls()
    {
        exposureSlider_->setRange(0, exposureSliderMaximum_);
        exposureSlider_->setValue(exposureMicroseconds_);
        exposureSlider_->setEnabled(exposureControlAvailable_);
        updateControlLabels();
    }

    QImage createPrintscreenImage() const
    {
        constexpr int panelWidth = 330;
        constexpr int margin = 18;
        constexpr int lineHeight = 24;

        const int pointLines = static_cast<int>(
            measurements_.get_points().size()
        );
        const int requiredPanelHeight =
            margin * 2 + lineHeight * (10 + pointLines);
        const int outputHeight = std::max(
            lastFrameImage_.height(),
            requiredPanelHeight
        );

        QImage output(
            lastFrameImage_.width() + panelWidth,
            outputHeight,
            QImage::Format_RGB32
        );
        output.fill(QColor("#111111"));

        QPainter painter(&output);
        painter.drawImage(0, 0, lastFrameImage_);
        painter.setPen(QColor("#dddddd"));

        int y = margin;
        const int x = lastFrameImage_.width() + margin;
        auto drawLine = [&](const QString& text) {
            painter.drawText(x, y, panelWidth - margin * 2, lineHeight,
                Qt::AlignLeft | Qt::AlignVCenter, text);
            y += lineHeight;
        };

        painter.setPen(QColor("#55dd88"));
        drawLine("Laser Scanner - medicoes");
        painter.setPen(QColor("#dddddd"));
        drawLine(lastFrameCapturedAt_.toString("yyyy-MM-dd HH:mm:ss"));
        drawLine(QString("Frame: %1 x %2").arg(
            lastFrameImage_.width()
        ).arg(lastFrameImage_.height()));
        drawLine(QString("Pontos detectados: %1").arg(
            static_cast<qulonglong>(lastDetectedPointCount_)
        ));
        drawLine(QString("Pontos marcados: %1").arg(
            static_cast<qulonglong>(lastMarkedPointCount_)
        ));

        if (measurements_.empty())
        {
            drawLine("Sem medicoes disponiveis");
            painter.end();
            return output;
        }

        drawLine("Y: " + formatMillimeters(measurements_.get_y()));
        drawLine("Z: " + formatMillimeters(measurements_.get_z()));
        drawLine("Gap: " + formatMillimeters(measurements_.get_gap()));
        drawLine("Hilo: " + formatMillimeters(measurements_.get_hilo()));
        drawLine("Area: " + formatSquareMillimeters(measurements_.get_area()));

        const std::vector<Measurements::Point>& points =
            measurements_.get_points();
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            drawLine(QString("Ponto %1: Y %2 mm, Z %3 mm")
                .arg(static_cast<qulonglong>(index + 1))
                .arg(points[index].y, 0, 'f', 2)
                .arg(points[index].z, 0, 'f', 2));
        }

        painter.end();
        return output;
    }

    void savePrintscreen()
    {
        if (lastFrameImage_.isNull())
        {
            QMessageBox::information(
                this,
                "Printscreen",
                "Ainda nao existe um frame disponivel para salvar."
            );
            return;
        }

        // Congela frame e medicoes antes de abrir o dialogo. O seletor de
        // arquivos possui seu proprio loop de eventos e a captura continua ao
        // fundo enquanto o usuario escolhe o destino.
        const QImage printscreenImage = createPrintscreenImage();
        const QString captureTimestamp = lastFrameCapturedAt_.toString(
            "yyyyMMdd_HHmmss"
        );

        QString exportDirectory = QString::fromLocal8Bit(
            qgetenv("LASER_SCANNER_EXPORT_DIR")
        );
        if (exportDirectory.isEmpty())
        {
            exportDirectory = QDir::homePath();
        }
        exportDirectory = QDir::cleanPath(
            QFileInfo(exportDirectory).absoluteFilePath()
        );

        if (!QFileInfo(exportDirectory).isDir())
        {
            QMessageBox::critical(
                this,
                "Pasta de exportacao indisponivel",
                "O volume de exportacao nao existe dentro do container:\n" +
                    exportDirectory +
                    "\n\nConfira HOME e o volume /host-home no docker-compose."
            );
            return;
        }

        QString directory = QDir(exportDirectory).filePath("Pictures");
        if (!QFileInfo(directory).isDir())
        {
            directory = exportDirectory;
        }

        const QString suggestedPath = QDir(directory).filePath(
            "laser-scanner_" +
            captureTimestamp +
            ".png"
        );
        QString selectedFilter;
        QString filePath = QFileDialog::getSaveFileName(
            this,
            "Salvar printscreen",
            suggestedPath,
            "Imagem PNG (*.png);;Imagem JPEG (*.jpg *.jpeg)",
            &selectedFilter
        );
        if (filePath.isEmpty())
        {
            return;
        }

        QByteArray imageFormat;
        QString suffix = QFileInfo(filePath).suffix().toLower();
        if (suffix.isEmpty())
        {
            if (selectedFilter.startsWith("Imagem JPEG"))
            {
                filePath += ".jpg";
                suffix = "jpg";
            }
            else
            {
                filePath += ".png";
                suffix = "png";
            }
        }

        if (suffix == "jpg" || suffix == "jpeg")
        {
            imageFormat = "jpeg";
        }
        else if (suffix == "png")
        {
            imageFormat = "png";
        }
        else
        {
            QMessageBox::warning(
                this,
                "Formato invalido",
                "Use a extensao .png, .jpg ou .jpeg."
            );
            return;
        }

        if (!QImageWriter::supportedImageFormats().contains(imageFormat))
        {
            QMessageBox::critical(
                this,
                "Formato Qt indisponivel",
                "A imagem Qt instalada no container nao possui o codificador "
                "para " + QString::fromLatin1(imageFormat) + "."
            );
            return;
        }

        const QString absoluteFilePath = QDir::cleanPath(
            QFileInfo(filePath).absoluteFilePath()
        );
        const QString exportPrefix = exportDirectory.endsWith(QDir::separator())
            ? exportDirectory
            : exportDirectory + QDir::separator();
        if (!absoluteFilePath.startsWith(exportPrefix))
        {
            QMessageBox::warning(
                this,
                "Destino fora do volume exportado",
                "Aplicacoes em Docker somente conseguem exportar arquivos "
                "por diretorios montados do host. Escolha uma pasta dentro "
                "de:\n" + exportDirectory
            );
            return;
        }

        const QFileInfo targetInfo(absoluteFilePath);
        if (!targetInfo.dir().exists())
        {
            QMessageBox::critical(
                this,
                "Diretorio inexistente",
                "O diretorio escolhido nao existe:\n" +
                    targetInfo.dir().absolutePath()
            );
            return;
        }

        QSaveFile outputFile(absoluteFilePath);
        if (!outputFile.open(QIODevice::WriteOnly))
        {
            QMessageBox::critical(
                this,
                "Falha ao abrir o arquivo",
                "Nao foi possivel criar o arquivo no volume do host:\n" +
                    absoluteFilePath + "\n\nMotivo: " +
                    outputFile.errorString()
            );
            return;
        }

        QImageWriter writer(&outputFile, imageFormat);
        writer.setQuality(95);
        if (!writer.write(printscreenImage))
        {
            const QString writerError = writer.errorString();
            outputFile.cancelWriting();
            QMessageBox::critical(
                this,
                "Falha ao codificar a imagem",
                "O Qt nao conseguiu gerar a imagem no formato " +
                    QString::fromLatin1(imageFormat) +
                    ".\n\nMotivo: " + writerError
            );
            return;
        }

        if (!outputFile.commit())
        {
            QMessageBox::critical(
                this,
                "Falha ao salvar",
                "A imagem foi gerada, mas nao foi possivel concluir a escrita "
                "no host:\n" + absoluteFilePath + "\n\nMotivo: " +
                    outputFile.errorString()
            );
            return;
        }

        statusBar()->showMessage(
            "Printscreen salvo em " + absoluteFilePath,
            5000
        );
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
        pointsValueLabel_->setText(QString::number(
            pointsSlider_->value()
        ));
    }

    void updateMeasurementLabels()
    {
        const std::vector<Measurements::Point>& points =
            measurements_.get_points();

        while (pointCoordinateLabels_.size() < points.size())
        {
            QLabel* pointLabel = createValueLabel("");
            pointLabel->setParent(pointsListWidget_);
            pointsListLayout_->addWidget(pointLabel);
            pointCoordinateLabels_.push_back(pointLabel);
        }

        for (std::size_t index = 0; index < pointCoordinateLabels_.size(); ++index)
        {
            QLabel* pointLabel = pointCoordinateLabels_[index];
            const bool pointExists = index < points.size();
            pointLabel->setVisible(pointExists);

            if (pointExists)
            {
                pointLabel->setText(QString("Ponto %1: Y %2 | Z %3")
                    .arg(static_cast<qulonglong>(index + 1))
                    .arg(formatMillimeters(points[index].y))
                    .arg(formatMillimeters(points[index].z)));
            }
        }

        noPointsLabel_->setVisible(points.empty());

        if (measurements_.empty())
        {
            yLabel_->setText("Y: -");
            zLabel_->setText("Z: -");
            gapLabel_->setText("Gap: -");
            hiloLabel_->setText("Hilo: -");
            areaLabel_->setText("Area: -");
            return;
        }

        yLabel_->setText("Y: " + formatMillimeters(measurements_.get_y()));
        zLabel_->setText("Z: " + formatMillimeters(measurements_.get_z()));
        gapLabel_->setText("Gap: " + formatMillimeters(measurements_.get_gap()));
        hiloLabel_->setText(
            "Hilo: " + formatMillimeters(measurements_.get_hilo())
        );
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
    double targetFramesPerSecond_ = 30.0;
    ImagePathTracker imagePathTracker_;
    Measurements measurements_;
    QTimer frameTimer_;
    MqttConfig mqttConfig_;
    MqttPublisher mqttPublisher_;
    QTimer mqttPublishTimer_;
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
    QLabel* hiloLabel_ = nullptr;
    QLabel* areaLabel_ = nullptr;
    QWidget* pointsListWidget_ = nullptr;
    QVBoxLayout* pointsListLayout_ = nullptr;
    QLabel* noPointsLabel_ = nullptr;
    std::vector<QLabel*> pointCoordinateLabels_;
    QLabel* connectedLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* laserLabel_ = nullptr;
    QPushButton* printscreenButton_ = nullptr;
    QImage lastFrameImage_;
    QDateTime lastFrameCapturedAt_;
    std::size_t lastDetectedPointCount_ = 0;
    std::size_t lastMarkedPointCount_ = 0;
    bool recoveryDialogOpen_ = false;
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
