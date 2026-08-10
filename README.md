# Laser Scanner Linux

## Resumo

O Laser Scanner captura imagens de uma câmera GigE compatível com FlyCapture2,
processa o perfil do laser e exibe o resultado em uma janela Qt. As coordenadas
dos pontos de inflexão são publicadas via MQTT nos tópicos
`laser-lab/p1` até `laser-lab/p10`, dependendo de quantos pontos estão sendo exibidos.

A aplicação é executada com Docker Compose. Antes de iniciá-la, é necessário:

1. conectar e configurar a câmera GigE;
2. iniciar o broker Mosquitto no computador;
3. autorizar a janela Qt na sessão gráfica;
4. obter ou construir a imagem Docker.

## Pré-requisitos

- Computador x86-64 com Linux e sessão gráfica X11 ou XWayland.
- Câmera GigE compatível com FlyCapture2 conectada ao computador.
- Docker Engine com Docker Compose.
- Acesso de administrador (`sudo`) para configurar a rede e instalar o
  Mosquitto.

Confirme a arquitetura, o Docker e a sessão gráfica:

```bash
uname -m
docker --version
docker compose version
echo "$DISPLAY"
```

O resultado de `uname -m` deve ser `x86_64`, e a variável `DISPLAY` não deve
estar vazia.

## Configurar a rede da câmera

O contêiner utiliza diretamente a rede do computador. Execute os comandos
abaixo no host antes de iniciar a aplicação.

Aumente temporariamente os buffers de recepção para 10 MiB:

```bash
sudo sysctl -w net.core.rmem_max=10485760
sudo sysctl -w net.core.rmem_default=10485760
```

Identifique a interface conectada à câmera:

```bash
ip -br addr
```

Configure o MTU, substituindo `enp30s0` pelo nome encontrado no comando
anterior:

```bash
sudo ip link set dev enp30s0 mtu 9000
```

## Instalar e iniciar o Mosquitto

O Mosquitto deve ser instalado no computador host. A instalação existente
dentro da imagem Docker não cria um serviço no host.

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Confirme que o serviço está ativo e ouvindo na porta `1883`:

```bash
systemctl is-active mosquitto
ss -ltnp | grep 1883
```

O arquivo `docker-compose.yml` está configurado para acessar o broker do host
em `127.0.0.1:1883`. Para acompanhar as mensagens publicadas pela aplicação,
abra outro terminal e execute:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'laser-lab/#' -v
```

## Permitir a janela Qt

Execute os comandos abaixo no mesmo terminal que será usado para iniciar a
aplicação:

```bash
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
test -n "$HOME" && test -d "$HOME" && test -w "$HOME"
xhost +SI:localuser:"$(id -un)"
```

Não execute o Docker Compose com `sudo`. A aplicação precisa usar o usuário da
sessão gráfica para abrir a janela Qt e salvar arquivos na pasta pessoal.

## Obter a imagem Docker

Para usar a imagem pronta:

```bash
docker compose pull
```

## Executar o sistema

Com a câmera conectada, o Mosquitto ativo e a janela Qt autorizada, execute:

```bash
docker compose up
```

A inicialização correta mostra a câmera detectada e uma mensagem semelhante a:

```text
MQTT conectado a 127.0.0.1:1883
```

Feche a janela Qt ou pressione `Q` ou `Esc` para encerrar.

Se aparecer `Connection refused`, confirme novamente o serviço e a porta:

```bash
sudo systemctl status mosquitto
ss -ltnp | grep 1883
```
