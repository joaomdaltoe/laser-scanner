#pragma once

/**
 * @file FlyCaptureCamera.hpp
 * @brief Declara um wrapper para cameras Point Grey/FLIR FlyCapture2.
 *
 * @details
 * Este arquivo isola a inicializacao, configuracao e captura de frames da
 * camera FlyCapture2 usada pelo scanner. Suporta o modelo de câmera usado
 * no laboratório: Flea3 FL3-GE-132S2M.
 */

#include <FlyCapture2.h>
#include <opencv2/core.hpp>

/**
 * @brief Gerencia conexao, configuracao e captura de uma camera FlyCapture2.
 *
 * @details
 * A classe conecta a camera no construtor, libera a conexao no destrutor e
 * converte cada frame capturado para cv::Mat BGR. Copia e atribuicao sao
 * desabilitadas porque a instancia possui recursos nativos da camera.
 */
class FlyCaptureCamera final
{
public:
    /**
     * @brief Conecta a camera pelo indice informado no barramento FlyCapture2.
     *
     * @param cameraIndex Indice da camera retornada pelo BusManager.
     *
     * @throws std::runtime_error Se nenhuma camera for encontrada ou se uma
     * chamada FlyCapture2 falhar.
     * @throws std::out_of_range Se o indice solicitado nao existir.
     */
    explicit FlyCaptureCamera(unsigned int cameraIndex = 0);

    /**
     * @brief Para a captura e desconecta a camera.
     */
    ~FlyCaptureCamera() noexcept;

    /// Copia desabilitada porque a classe possui conexao nativa exclusiva.
    FlyCaptureCamera(const FlyCaptureCamera&) = delete;

    /// Atribuicao por copia desabilitada porque a classe possui conexao nativa exclusiva.
    FlyCaptureCamera& operator=(const FlyCaptureCamera&) = delete;

    /**
     * @brief Configura a taxa de frames absoluta da camera.
     *
     * @param framesPerSecond Taxa desejada em frames por segundo.
     * @return true se a propriedade existir e aceitar o valor solicitado.
     *
     * @throws std::invalid_argument Se framesPerSecond for menor ou igual a zero.
     */
    bool configureFrameRate(float framesPerSecond);

    /**
     * @brief Consulta o intervalo absoluto de exposicao suportado.
     *
     * @param[out] minimumMilliseconds Menor exposicao suportada, em milissegundos.
     * @param[out] maximumMilliseconds Maior exposicao suportada, em milissegundos.
     * @param[out] currentMilliseconds Exposicao atual, em milissegundos.
     * @return true se a camera informar controle absoluto de shutter/exposicao.
     */
    bool getExposureRange(
        float& minimumMilliseconds,
        float& maximumMilliseconds,
        float& currentMilliseconds
    );

    /**
     * @brief Configura a exposicao absoluta da camera.
     *
     * @param milliseconds Exposicao desejada em milissegundos.
     * @return true se a camera aceitar a configuracao.
     *
     * @throws std::invalid_argument Se milliseconds for menor ou igual a zero.
     *
     * @note Valores validos sao limitados ao intervalo informado pela camera.
     */
    bool configureExposure(float milliseconds);

    /**
     * @brief Inicia a captura de frames.
     *
     * @throws std::runtime_error Se a API FlyCapture2 falhar ao iniciar captura.
     *
     * @note Chamadas repetidas enquanto a captura ja esta ativa nao tem efeito.
     */
    void start();

    /**
     * @brief Para a captura de frames, se estiver ativa.
     */
    void stop() noexcept;

    /**
     * @brief Refaz a conexao com a mesma camera usada na construcao.
     *
     * @details A camera e localizada pelo numero serial, pois o indice no
     * barramento pode mudar depois que o cabo for removido e conectado.
     * A configuracao de timeout de captura tambem e reaplicada.
     *
     * @throws std::runtime_error Se a camera ainda nao estiver disponivel ou
     * alguma operacao FlyCapture2 falhar.
     */
    void reconnect();

    /**
     * @brief Captura um frame e o converte para BGR OpenCV.
     *
     * @return Matriz CV_8UC3 com copia propria dos dados do frame.
     *
     * @throws std::logic_error Se a captura ainda nao tiver sido iniciada.
     * @throws std::runtime_error Se RetrieveBuffer ou Convert falharem.
     */
    cv::Mat grabFrameBgr();

    /**
     * @brief Retorna as informacoes da camera conectada.
     *
     * @return Referencia para a estrutura FlyCapture2::CameraInfo armazenada.
     */
    const FlyCapture2::CameraInfo& info() const noexcept;

private:
    /**
     * @brief Converte erros FlyCapture2 em excecoes C++.
     *
     * @param error Resultado retornado pela chamada FlyCapture2.
     * @param operation Nome da operacao usado na mensagem de erro.
     *
     * @throws std::runtime_error Se error indicar falha.
     */
    static void throwIfError(
        const FlyCapture2::Error& error,
        const char* operation
    );

    /**
     * @brief Limita o bloqueio de RetrieveBuffer quando os frames cessarem.
     *
     * @throws std::runtime_error Se a configuracao nao puder ser consultada ou
     * aplicada pela camera.
     */
    void configureGrabTimeout();

    /// Objeto nativo de acesso a camera.
    FlyCapture2::Camera camera_;

    /// Metadados retornados pela camera conectada.
    FlyCapture2::CameraInfo cameraInfo_;

    /// Numero serial usado para reencontrar a mesma camera apos desconexao.
    unsigned int cameraSerialNumber_ = 0;

    /// Buffer bruto reutilizado durante a captura.
    FlyCapture2::Image rawImage_;

    /// Buffer BGR reutilizado apos conversao pela API FlyCapture2.
    FlyCapture2::Image bgrImage_;

    /// Indica se a conexao com a camera esta ativa.
    bool connected_ = false;

    /// Indica se StartCapture foi executado com sucesso.
    bool capturing_ = false;
};
