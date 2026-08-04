#pragma once

/**
 * @file VideoViewer.hpp
 * @brief Declara a interface de visualizacao da camera e das medicoes.
 *
 * @details
 * Este arquivo expoe a classe responsavel por iniciar a janela Qt que captura,
 * processa e exibe os frames da camera. É possível controlar:
 * Limiar: número que será passado para a classe ImagePathTrack para encontrar a linha laser.
 * Número de pontos: número de pontos que serão encontrados pelo algoritmo de pontos de inflexão.
 * Exposição da câmera: número em milissegundos de exposição que a câmera vai fornecer para a classe FlyCaptureCamera.
 */

#include <string>

class FlyCaptureCamera;

/**
 * @brief Controla a execucao da interface grafica de visualizacao.
 *
 * @details
 * A classe cria a janela principal, usa a camera fornecida para obter frames,
 * rastreia a linha do laser e mostra medidas calculadas em tempo real.
 * Para encerrar o sistema, feche a aba criada ou pare execução no terminal executado
 */
class VideoViewer final
{
public:
    /**
     * @brief Configura os parametros basicos da janela de visualizacao.
     *
     * @param windowName Nome exibido na janela principal.
     * @param targetFramesPerSecond Taxa alvo usada para temporizar a captura.
     *
     * @throws std::invalid_argument Se o nome da janela estiver vazio ou se a
     * taxa de frames for menor ou igual a zero.
     */
    explicit VideoViewer(
        std::string windowName = "Camera GigE FLEA",
        double targetFramesPerSecond = 30.0
    );

    /**
     * @brief Executa a janela de captura e visualizacao.
     *
     * @param camera Camera FlyCapture ja criada e pronta para ser iniciada.
     *
     * @throws std::logic_error Se nao existir uma QApplication ativa.
     * @throws std::exception Pode propagar falhas geradas durante a criacao da
     * janela ou a inicializacao da captura.
     *
     * @note Este metodo entra no loop de eventos do Qt e bloqueia ate a janela
     * ser fechada.
     */
    void run(FlyCaptureCamera& camera) const;

private:
    /// Nome da janela principal.
    std::string windowName_;

    /// Taxa alvo de captura e atualizacao da interface.
    double targetFramesPerSecond_;
};
