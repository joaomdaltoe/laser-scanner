#pragma once

/**
 * @file Measurements.hpp
 * @brief Declara o conversor de pontos de imagem em medidas geometricas.
 *
 * @details
 * Este arquivo concentra a representacao das medidas derivadas dos pontos
 * detectados na imagem do laser. Calculos de conversao pixel -> mm foram
 * realizados com base em calibracao experimental. Z usa regressao quadratica
 * e a escala de Y varia conforme a distancia calculada.
 * 
 * @note
 * Eixo Y: Largura horizontal que a câmera enxerga, com origem no centro da imagem
 * Eixo Z: Altura vertical que a câmera enxerga
 */

#include <opencv2/core.hpp>

#include <vector>

/**
 * @brief Calcula medidas em milimetros a partir de pontos detectados na imagem.
 *
 * @details
 * A classe converte coordenadas em pixels para milimetros, armazena os pontos
 * convertidos e calcula valores agregados como posicao central, gap, hilo e
 * area.
 */
class Measurements final
{
public:
    /**
     * @brief Representa um ponto convertido para o sistema de medidas.
     *
     */
    struct Point final
    {
        /// Coordenada Y em milimetros.
        double y;

        /// Coordenada Z em milimetros.
        double z;
    };

    Measurements() = default;

    /**
     * @brief Atualiza as medidas usando os pontos detectados na imagem.
     *
     * @param imagePoints Pontos em coordenadas de pixel, na ordem em que devem
     * compor o perfil medido.
     *
     * @details
     * Todos os pontos sao convertidos para milimetros. A posicao central e o gap
     * sao derivados do ponto central ou do par central, e a area e calculada pelo
     * contorno interno quando ha pontos suficientes.
     */
    void update(const std::vector<cv::Point2f>& imagePoints);

    /**
     * @brief Informa se nao ha pontos convertidos disponiveis.
     *
     * @return true quando a ultima atualizacao nao produziu pontos.
     */
    bool empty() const noexcept;

    /**
     * @brief Retorna a coordenada Y central calculada.
     *
     * @return Valor de Y em milimetros, ou 0.0 quando nao ha pontos.
     */
    double get_y() const noexcept;

    /**
     * @brief Retorna a coordenada Z central calculada.
     *
     * @return Valor de Z em milimetros, ou 0.0 quando nao ha pontos.
     */
    double get_z() const noexcept;

    /**
     * @brief Retorna a abertura medida entre pontos vizinhos ao centro.
     *
     * @return Gap em milimetros, ou 0.0 quando nao ha base suficiente.
     */
    double get_gap() const noexcept;

    /**
     * @brief Retorna o desalinhamento vertical entre os pontos de referencia.
     *
     * @return Hilo em milimetros, ou 0.0 quando nao ha pontos suficientes.
     */
    double get_hilo() const noexcept;

    /**
     * @brief Retorna a area do perfil convertido.
     *
     * @return Area em milimetros quadrados.
     */
    double get_area() const noexcept;

    /**
     * @brief Retorna os pontos convertidos da ultima atualizacao.
     *
     * @return Referencia somente leitura para os pontos em milimetros.
     */
    const std::vector<Point>& get_points() const noexcept;

private:
    /**
     * @brief Calcula a distancia camera-objeto a partir do pixel vertical.
     *
     * @param verticalPixel Coordenada V original em pixels.
     * @return Distancia camera-objeto em milimetros.
     */
    static double cameraObjectDistance(double verticalPixel) noexcept;

    /**
     * @brief Converte a coordenada horizontal usando a escala da profundidade.
     *
     * @param horizontalPixel Coordenada U original em pixels.
     * @param distanceMillimeters Distancia camera-objeto em milimetros.
     * @return Coordenada Y em milimetros, com origem no centro da imagem.
     */
    static double convertHorizontalPixelCoordinate(
        double horizontalPixel,
        double distanceMillimeters
    ) noexcept;

    /**
     * @brief Converte a distancia em altura relativa ao plano de 211 mm.
     *
     * @param distanceMillimeters Distancia camera-objeto em milimetros.
     * @return Coordenada Z em milimetros, positiva ao aproximar-se da camera.
     */
    static double heightFromDistance(double distanceMillimeters) noexcept;

    /**
     * @brief Calcula a area absoluta do poligono formado pelos pontos.
     *
     * @param points Pontos ja convertidos para milimetros.
     * @return Area calculada em milimetros quadrados.
     *
     */
    static double polygonArea(const std::vector<Point>& points) noexcept;

    /// Coordenada Y central calculada para o perfil atual.
    double y_ = 0.0;

    /// Coordenada Z central calculada para o perfil atual.
    double z_ = 0.0;

    /// Abertura calculada entre pontos vizinhos ao centro do perfil.
    double gap_ = 0.0;

    /// Desalinhamento vertical calculado entre os pontos de referencia.
    double hilo_ = 0.0;

    /// Area calculada para o perfil atual.
    double area_ = 0.0;

    /// Pontos convertidos da ultima atualizacao.
    std::vector<Point> points_;
};
