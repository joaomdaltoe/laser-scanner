#pragma once

/**
 * @file ImagePathTrack.hpp
 * @brief Declara o rastreador da linha do laser em imagens OpenCV.
 *
 * @details
 * Este arquivo define a interface usada para detectar, desenhar e simplificar
 * o caminho do laser em frames BGR. Considera câmera monocromática, localizada
 * verticalmente acima do chanfro a ser analisado.
 */

#include <opencv2/core.hpp>

#include <vector>

/**
 * @brief Detecta e manipula o caminho da linha laser em uma imagem.
 *
 * @details
 * A deteccao usa limiar de intensidade, integracao por colunas e refinamento
 * por centroide para produzir uma linha central com possivel precisao subpixel.
 * Tambem oferece rotinas auxiliares para marcar pontos de inflexao.
 */
class ImagePathTracker final
{
public:
    /**
     * @brief Cria o rastreador com um limiar inicial de intensidade.
     *
     * @param intensityThreshold Limiar entre 0 e 255 usado para aceitar pixels
     * como parte do laser.
     *
     * @throws std::out_of_range Se o limiar estiver fora do intervalo [0, 255].
     */
    explicit ImagePathTracker(int intensityThreshold = 180);

    /**
     * @brief Atualiza o limiar de intensidade usado na deteccao.
     *
     * @param intensityThreshold Novo limiar entre 0 e 255.
     *
     * @throws std::out_of_range Se o limiar estiver fora do intervalo [0, 255].
     */
    void setIntensityThreshold(int intensityThreshold);

    /**
     * @brief Retorna o limiar de intensidade atual.
     *
     * @return Valor inteiro no intervalo [0, 255].
     */
    int intensityThreshold() const noexcept;

    /**
     * @brief Detecta a linha central do laser em um frame BGR.
     *
     * @param bgrFrame Frame OpenCV no formato CV_8UC3.
     * @return Pontos da linha central detectada, ordenados por coordenada X.
     *
     * @throws std::invalid_argument Se o frame nao estiver no formato CV_8UC3.
     *
     * @note Um frame vazio retorna uma lista vazia.
     */
    std::vector<cv::Point2f> detect(const cv::Mat& bgrFrame) const;

    /**
     * @brief Desenha a linha detectada sobre um frame BGR.
     *
     * @param bgrFrame Frame que recebera o desenho.
     * @param points Pontos da linha central, normalmente retornados por detect().
     *
     * @note Lacunas entre colunas consecutivas sem deteccao nao sao conectadas.
     */
    void draw(cv::Mat& bgrFrame, const std::vector<cv::Point2f>& points) const;

    /**
     * @brief Seleciona pontos de interesse ao longo de um caminho unidimensional.
     *
     * @param path Valores Y do caminho amostrados por indice X.
     * @param nInflectionPoints Quantidade maxima de pontos internos desejados.
     * @return Indices selecionados, incluindo os extremos quando o caminho tem
     * mais de um ponto.
     *
     * @details
     * A selecao usa repetidamente o maior erro retornado por RDP() para inserir
     * novos pontos entre segmentos ja escolhidos.
     *
     */
    std::vector<int> findInflectionPoints(const std::vector<float> path, int nInflectionPoints) const;

    /**
     * @brief Calcula o ponto de maior erro em um segmento do caminho.
     *
     * @param points Valores Y do caminho.
     * @param x0 Indice inicial do segmento.
     * @param x1 Indice final do segmento.
     * @return Par contendo o indice de maior erro e o valor desse erro.
     *
     * @pre x0 e x1 devem ser indices validos, e x1 deve ser maior que x0.
     *
     * @note Algoritmo Ramer-Douglas-Pecker 
     */
    std::pair<int,float> RDP(const std::vector<float> points, int x0, int x1) const;
private:
    /// Limiar minimo de intensidade para considerar um pixel parte do laser.
    int intensityThreshold_;
};
