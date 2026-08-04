#pragma once

/**
 * @file Measurements.hpp
 * @brief Declara o conversor de pontos de imagem em medidas geometricas.
 *
 * @details
 * Este arquivo concentra a representacao das medidas derivadas dos pontos
 * detectados na imagem do laser. Cálculos de conversão pixel -> mm foram
 * realizados com base em calibração experimental e regressão linear.
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
 * convertidos e calcula valores agregados como posicao central, gap e area.
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

    /**
     * @brief Inicializa a conversao de pixels para milimetros.
     *
     * @param millimetersPerPixel Escala aplicada a cada coordenada em pixels.
     * @param coordinateOffsetMillimeters Deslocamento somado apos a escala.
     *
     * @throws std::invalid_argument Se algum parametro de calibracao nao for
     * finito.
     *
     */
    explicit Measurements(
        double millimetersPerPixel = -0.09719,
        double coordinateOffsetMillimeters = 30.04304
    );

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
     * @brief Converte uma coordenada de pixel para milimetros.
     *
     * @param coordinate Coordenada original em pixels.
     * @return Coordenada convertida em milimetros.
     */
    double convertPixelCoordinate(double coordinate) const noexcept;

    /**
     * @brief Calcula a area absoluta do poligono formado pelos pontos.
     *
     * @param points Pontos ja convertidos para milimetros.
     * @return Area calculada em milimetros quadrados.
     *
     */
    static double polygonArea(const std::vector<Point>& points) noexcept;

    /// Escala linear usada na conversao de pixels para milimetros.
    double millimetersPerPixel_;

    /// Deslocamento linear usado na conversao de pixels para milimetros.
    double coordinateOffsetMillimeters_;

    /// Coordenada Y central calculada para o perfil atual.
    double y_ = 0.0;

    /// Coordenada Z central calculada para o perfil atual.
    double z_ = 0.0;

    /// Abertura calculada entre pontos vizinhos ao centro do perfil.
    double gap_ = 0.0;

    /// Area calculada para o perfil atual.
    double area_ = 0.0;

    /// Pontos convertidos da ultima atualizacao.
    std::vector<Point> points_;
};
