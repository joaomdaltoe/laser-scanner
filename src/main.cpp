#include "FlyCaptureCamera.hpp"
#include "VideoViewer.hpp"

#include <QApplication>
#include <QByteArray>
#include <QtGlobal>

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    constexpr float targetFramesPerSecond = 30.0F;

    try
    {
        // No Qt 5.9 do Ubuntu 18.04, a ponte de acessibilidade AT-SPI pode
        // liberar uma conexao D-Bus privada que falhou antes de fecha-la. O
        // libdbus 1.12 transforma essa verificacao em SIGABRT por padrao.
        // Defina isto antes do QApplication, pois o primeiro setText() de um
        // widget ja pode inicializar a ponte. Uma configuracao explicita do
        // ambiente continua tendo precedencia, inclusive para diagnostico.
        if (!qEnvironmentVariableIsSet("DBUS_FATAL_WARNINGS"))
        {
            qputenv("DBUS_FATAL_WARNINGS", QByteArrayLiteral("0"));
        }

        QApplication application(argc, argv);

        FlyCaptureCamera camera(0);
        const FlyCapture2::CameraInfo& cameraInfo = camera.info();

        std::cout
            << "Camera conectada: " << cameraInfo.modelName
            << "\nSerial: " << cameraInfo.serialNumber
            << std::endl;

        if (!camera.configureFrameRate(targetFramesPerSecond))
        {
            std::cerr
                << "Aviso: a camera nao aceitou a configuracao de 30 FPS. "
                << "A exibicao continuara limitada a 30 FPS, mas a taxa real "
                << "dependera da camera."
                << std::endl;
        }

        std::cout
            << "Video iniciado. Pressione Q ou Esc para encerrar."
            << std::endl;

        VideoViewer viewer("Camera GigE FLEA", targetFramesPerSecond);
        viewer.run(camera);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Falha na aplicacao: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
