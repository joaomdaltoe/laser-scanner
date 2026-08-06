#pragma once

/**
 * @file MqttPublisher.hpp
 * @brief Publicacao MQTT das coordenadas fisicas dos pontos de inflexao.
 */

#include "Measurements.hpp"

#include <memory>
#include <string>
#include <vector>

/**
 * @brief Configuracoes do publicador carregadas das variaveis do container.
 */
struct MqttConfig final
{
    std::string host;
    int port = 1883;
    std::string topic;
    std::string clientId;
    int keepAliveSeconds = 60;

    /**
     * Intervalo entre publicacoes MQTT.
     *
     * O valor vem de MQTT_PUBLISH_INTERVAL_MS, definido no docker-compose.yml.
     * Para alterar futuramente a taxa do topico, mude 1000 ms (1 segundo) no
     * Compose. A captura da camera continuara usando seu timer independente.
     */
    int publishIntervalMilliseconds = 1000;

    /**
     * @brief Le host, porta, topico e temporizacao das variaveis de ambiente.
     * @return Configuracao valida, usando valores padrao quando uma variavel
     * estiver ausente ou for invalida.
     */
    static MqttConfig fromEnvironment();
};

/**
 * @brief Mantem a conexao MQTT em segundo plano e publica pontos com QoS 0.
 *
 * Falhas de inicializacao, conexao e publicacao sao apenas registradas no
 * terminal. Elas nunca sao propagadas para o fluxo de captura da camera.
 */
class MqttPublisher final
{
public:
    explicit MqttPublisher(MqttConfig config);
    ~MqttPublisher();

    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    /**
     * @brief Publica exatamente cinco pontos em milimetros.
     *
     * @param points Pontos Y/Z convertidos pela classe Measurements.
     * @return true quando a mensagem foi aceita pela biblioteca MQTT.
     *
     * @note A mensagem usa QoS 0 e retain=false. Uma lista com quantidade
     * diferente de cinco pontos nao e publicada.
     */
    bool publishPoints(const std::vector<Measurements::Point>& points) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
