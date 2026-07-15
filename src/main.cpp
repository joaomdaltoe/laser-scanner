#include <flycapture/FlyCapture2.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
bool checkError(const FlyCapture2::Error& error, const std::string& operation)
{
    if (error == FlyCapture2::PGRERROR_OK)
        return true;

    std::cerr
        << "Erro em " << operation << ": "
        << error.GetDescription()
        << std::endl;

    return false;
}

void shutdownCamera(
    FlyCapture2::Camera& camera,
    bool& capturing,
    bool& connected)
{
    if (capturing)
    {
        const FlyCapture2::Error error = camera.StopCapture();

        if (!checkError(error, "StopCapture"))
        {
            std::cerr
                << "Não foi possível encerrar a captura corretamente."
                << std::endl;
        }

        capturing = false;
    }

    if (connected)
    {
        const FlyCapture2::Error error = camera.Disconnect();

        if (!checkError(error, "Disconnect"))
        {
            std::cerr
                << "Não foi possível desconectar a câmera corretamente."
                << std::endl;
        }

        connected = false;
    }
}
}

int main()
{
    FlyCapture2::BusManager busManager;

    unsigned int cameraCount = 0;

    FlyCapture2::Error error = busManager.GetNumOfCameras(&cameraCount);

    if (!checkError(error, "GetNumOfCameras"))
        return 1;

    std::cout
        << "Câmeras encontradas: "
        << cameraCount
        << std::endl;

    if (cameraCount == 0)
    {
        std::cerr << "Nenhuma câmera encontrada." << std::endl;
        return 2;
    }

	// Usa primeira câmera encontrada
    FlyCapture2::PGRGuid cameraGuid;

    error = busManager.GetCameraFromIndex(0, &cameraGuid);

    if (!checkError(error, "GetCameraFromIndex"))
        return 3;

    FlyCapture2::Camera camera;

    bool connected = false;
    bool capturing = false;

    error = camera.Connect(&cameraGuid);

    if (!checkError(error, "Connect"))
        return 4;

    connected = true;

    FlyCapture2::CameraInfo cameraInfo;

    error = camera.GetCameraInfo(&cameraInfo);

    if (checkError(error, "GetCameraInfo"))
    {
        std::cout
            << "Modelo: " << cameraInfo.modelName
            << "\nSerial: " << cameraInfo.serialNumber
            << "\nInterface: " << cameraInfo.interfaceType
            << std::endl;
    }

    error = camera.StartCapture();

    if (!checkError(error, "StartCapture"))
    {
        shutdownCamera(camera, capturing, connected);
        return 5;
    }

    capturing = true;

    std::cout << "Captura iniciada." << std::endl;

    FlyCapture2::Image rawImage;
    FlyCapture2::Image bgrImage;

    error = camera.RetrieveBuffer(&rawImage);

    if (!checkError(error, "RetrieveBuffer"))
    {
        shutdownCamera(camera, capturing, connected);
        return 6;
    }

    std::cout
        << "Frame RAW recebido:"
        << "\n  largura: " << rawImage.GetCols()
        << "\n  altura: " << rawImage.GetRows()
        << "\n  stride: " << rawImage.GetStride()
        << "\n  bits por pixel: "
        << static_cast<unsigned int>(rawImage.GetBitsPerPixel())
        << std::endl;

    error = rawImage.Convert(FlyCapture2::PIXEL_FORMAT_BGR, &bgrImage);

    if (!checkError(error, "Convert PIXEL_FORMAT_BGR"))
    {
        shutdownCamera(camera, capturing, connected);
        return 7;
    }

    const int width = static_cast<int>(bgrImage.GetCols());
    const int height = static_cast<int>(bgrImage.GetRows());
    const std::size_t stride =
        static_cast<std::size_t>(bgrImage.GetStride());

    std::cout
        << "Frame convertido para BGR:"
        << "\n  largura: " << width
        << "\n  altura: " << height
        << "\n  stride: " << stride
        << "\n  bits por pixel: "
        << static_cast<unsigned int>(bgrImage.GetBitsPerPixel())
        << std::endl;

    /*
     * Cria apenas uma visualização da memória pertencente ao FlyCapture2.
     *
     * CV_8UC3:
     *   - 8 bits por canal;
     *   - unsigned char;
     *   - 3 canais;
     *   - ordem B, G, R.
     *
     * O parâmetro stride informa quantos bytes existem entre o começo
     * de duas linhas consecutivas.
     */
    cv::Mat bgrView(
        height,
        width,
        CV_8UC3,
        bgrImage.GetData(),
        stride
    );

    if (bgrView.empty())
    {
        std::cerr << "Não foi possível criar o cv::Mat." << std::endl;

        shutdownCamera(camera, capturing, connected);
        return 8;
    }

    /*
     * bgrView não é dona dos pixels.
     *
     * clone() cria uma imagem independente do FlyCapture2.
     * Esta é a imagem que pode ser armazenada, enviada para outra thread
     * ou processada depois que bgrImage sair de escopo.
     */
    cv::Mat bitmapBgr = bgrView.clone();

    if (bitmapBgr.empty())
    {
        std::cerr << "Não foi possível copiar o frame BGR." << std::endl;

        shutdownCamera(camera, capturing, connected);
        return 9;
    }

    std::cout
        << "cv::Mat criado:"
        << "\n  cols: " << bitmapBgr.cols
        << "\n  rows: " << bitmapBgr.rows
        << "\n  canais: " << bitmapBgr.channels()
        << "\n  bytes por linha: " << bitmapBgr.step
        << "\n  contínuo: "
        << (bitmapBgr.isContinuous() ? "sim" : "não")
        << std::endl;

    // Mostra o primeiro pixel apenas para validar o formato.
    const cv::Vec3b firstPixel = bitmapBgr.at<cv::Vec3b>(0, 0);

    std::cout
        << "Primeiro pixel:"
        << "\n  B: " << static_cast<unsigned int>(firstPixel[0])
        << "\n  G: " << static_cast<unsigned int>(firstPixel[1])
        << "\n  R: " << static_cast<unsigned int>(firstPixel[2])
        << std::endl;

    /*
     * Já podemos encerrar a câmera porque bitmapBgr possui sua própria
     * cópia dos pixels.
     */
    shutdownCamera(camera, capturing, connected);

    const std::string outputFile = "frame_bgr.png";

    try
    {
        const bool saved = cv::imwrite(outputFile, bitmapBgr);

        if (!saved)
        {
            std::cerr
                << "OpenCV não conseguiu salvar "
                << outputFile
                << std::endl;

            return 10;
        }
    }
    catch (const cv::Exception& exception)
    {
        std::cerr
            << "Erro do OpenCV ao salvar a imagem: "
            << exception.what()
            << std::endl;

        return 11;
    }

    std::cout
        << "Imagem salva em: "
        << outputFile
        << std::endl;

    return 0;
}