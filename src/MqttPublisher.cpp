/**
 * @file MqttPublisher.cpp
 * @brief Implementa o publicador MQTT baseado em libmosquitto.
 */

#include "MqttPublisher.hpp"

#include <mosquitto.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <utility>

namespace
{
constexpr const char* defaultHost = "192.168.0.5";
constexpr int defaultPort = 1883;
constexpr const char* defaultTopicPrefix = "laser-lab";
constexpr const char* defaultClientId = "laser-scanner";
constexpr int defaultKeepAliveSeconds = 60;
constexpr int defaultPublishIntervalMilliseconds = 1000;
constexpr std::size_t requiredPointCount = 5;
constexpr int mqttQualityOfService = 0;
constexpr bool mqttRetain = false;

std::string environmentValueOrDefault(
    const char* variableName,
    const char* defaultValue
)
{
    const char* value = std::getenv(variableName);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }

    return value;
}

int environmentIntegerOrDefault(
    const char* variableName,
    int defaultValue,
    int minimumValue,
    int maximumValue
)
{
    const char* text = std::getenv(variableName);
    if (text == nullptr || text[0] == '\0')
    {
        return defaultValue;
    }

    errno = 0;
    char* end = nullptr;
    const long parsedValue = std::strtol(text, &end, 10);
    if (
        errno != 0 || end == text || *end != '\0' ||
        parsedValue < minimumValue || parsedValue > maximumValue
    )
    {
        std::cerr
            << "Aviso MQTT: " << variableName << "='" << text
            << "' e invalido; usando " << defaultValue << "."
            << std::endl;
        return defaultValue;
    }

    return static_cast<int>(parsedValue);
}

std::string serializePoints(
    const Measurements::Point& point
)
{
    std::ostringstream payload;
    payload.imbue(std::locale::classic());
    payload << std::setprecision(2)
            << "{\"y\":" << point.y
            << ",\"z\":" << point.z
            << '}';

    return payload.str();
}
}

MqttConfig MqttConfig::fromEnvironment()
{
    MqttConfig config;
    config.host = environmentValueOrDefault("MQTT_HOST", defaultHost);
    config.port = environmentIntegerOrDefault(
        "MQTT_PORT",
        defaultPort,
        1,
        65535
    );
    config.topicPrefix = environmentValueOrDefault("MQTT_TOPIC_PREFIX", defaultTopicPrefix);
    config.clientId = environmentValueOrDefault(
        "MQTT_CLIENT_ID",
        defaultClientId
    );
    config.keepAliveSeconds = environmentIntegerOrDefault(
        "MQTT_KEEPALIVE_SECONDS",
        defaultKeepAliveSeconds,
        5,
        INT_MAX
    );
    config.publishIntervalMilliseconds = environmentIntegerOrDefault(
        "MQTT_PUBLISH_INTERVAL_MS",
        defaultPublishIntervalMilliseconds,
        1,
        INT_MAX
    );
    return config;
}

class MqttPublisher::Impl final
{
public:
    explicit Impl(MqttConfig config)
        : config_(std::move(config))
    {
        const int initializeResult = mosquitto_lib_init();
        if (initializeResult != MOSQ_ERR_SUCCESS)
        {
            logError("inicializar a libmosquitto", initializeResult);
            return;
        }
        libraryInitialized_ = true;

        client_ = mosquitto_new(config_.clientId.c_str(), true, this);
        if (client_ == nullptr)
        {
            std::cerr
                << "Aviso MQTT: nao foi possivel criar o cliente Mosquitto. "
                << "A camera continuara funcionando sem publicacao."
                << std::endl;
            return;
        }

        mosquitto_connect_callback_set(client_, &Impl::onConnect);
        mosquitto_disconnect_callback_set(client_, &Impl::onDisconnect);

        // Reconecta em segundo plano, com espera exponencial entre 1 e 30 s.
        const int reconnectDelayResult = mosquitto_reconnect_delay_set(
            client_,
            1,
            30,
            true
        );
        if (reconnectDelayResult != MOSQ_ERR_SUCCESS)
        {
            logError("configurar a reconexao", reconnectDelayResult);
        }

        const int connectResult = mosquitto_connect_async(
            client_,
            config_.host.c_str(),
            config_.port,
            config_.keepAliveSeconds
        );
        if (connectResult != MOSQ_ERR_SUCCESS)
        {
            logError("iniciar a conexao", connectResult);
            return;
        }

        const int loopResult = mosquitto_loop_start(client_);
        if (loopResult != MOSQ_ERR_SUCCESS)
        {
            logError("iniciar o loop em segundo plano", loopResult);
            return;
        }
        loopStarted_ = true;

        std::cout
            << "MQTT configurado: " << config_.host << ':' << config_.port
            << " | prefixo dos topicos: " << config_.topicPrefix
            << " | intervalo: " << config_.publishIntervalMilliseconds
            << " ms | QoS: 0 | retain: false"
            << std::endl;
    }

    ~Impl()
    {
        if (client_ != nullptr)
        {
            if (loopStarted_)
            {
                mosquitto_loop_stop(client_, true);
            }
            mosquitto_destroy(client_);
        }

        if (libraryInitialized_)
        {
            mosquitto_lib_cleanup();
        }
    }

    bool publishPoints(
        const std::vector<Measurements::Point>& points
    ) noexcept
    {
        if (
            points.size() != requiredPointCount || client_ == nullptr ||
            !loopStarted_ || !connected_.load()
        )
        {
            return false;
        }

        try
        {
            bool allPublished = true;
            for (std::size_t index = 0; index < points.size(); index++) {
                
                const std::string topic =
                    config_.topicPrefix + "/p" + std::to_string(++index);
                
                const std::string payload = serializePoints(points[index]);
                const int publishResult = mosquitto_publish(
                    client_,
                    nullptr,
                    topic.c_str(),
                    static_cast<int>(payload.size()),
                    payload.data(),
                    mqttQualityOfService,
                    mqttRetain
                );

                if (publishResult != MOSQ_ERR_SUCCESS)
                {
                    allPublished = false;    

                    if (publishResult != MOSQ_ERR_NO_CONN)
                    {
                        logError("publica os ponto", publishResult);
                    }
                }
            }
            return allPublished;
        }
        catch (const std::exception& exception)
        {
            std::cerr
                << "Aviso MQTT: falha ao montar o payload: "
                << exception.what() << std::endl;
        }
        catch (...)
        {
            std::cerr
                << "Aviso MQTT: falha desconhecida ao montar o payload."
                << std::endl;
        }

        return false;
    }

private:
    static void onConnect(struct mosquitto*, void* userData, int result)
    {
        Impl* self = static_cast<Impl*>(userData);
        const bool connected = result == 0;
        self->connected_.store(connected);

        if (connected)
        {
            std::cout
                << "MQTT conectado a " << self->config_.host << ':'
                << self->config_.port << std::endl;
        }
        else
        {
            std::cerr
                << "Aviso MQTT: broker recusou a conexao (codigo "
                << result << "). Nova tentativa sera feita em segundo plano."
                << std::endl;
        }
    }

    static void onDisconnect(struct mosquitto*, void* userData, int result)
    {
        Impl* self = static_cast<Impl*>(userData);
        self->connected_.store(false);

        if (result != 0)
        {
            std::cerr
                << "Aviso MQTT: conexao perdida. A camera continuara ativa e "
                << "o cliente tentara reconectar em segundo plano."
                << std::endl;
        }
    }

    static void logError(const char* action, int errorCode)
    {
        std::cerr
            << "Aviso MQTT: falha ao " << action << ": "
            << mosquitto_strerror(errorCode)
            << ". A camera continuara funcionando."
            << std::endl;
    }

    MqttConfig config_;
    struct mosquitto* client_ = nullptr;
    std::atomic<bool> connected_{false};
    bool libraryInitialized_ = false;
    bool loopStarted_ = false;
};

MqttPublisher::MqttPublisher(MqttConfig config)
    : impl_(new Impl(std::move(config)))
{
}

MqttPublisher::~MqttPublisher() = default;

bool MqttPublisher::publishPoints(
    const std::vector<Measurements::Point>& points
) noexcept
{
    return impl_ != nullptr && impl_->publishPoints(points);
}
