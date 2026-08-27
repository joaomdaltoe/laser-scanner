# Laser Scanner — inicialização rápida

Este projeto captura imagens de uma câmera GigE compatível com FlyCapture2,
exibe o processamento em uma janela Qt e publica os resultados por MQTT. A
aplicação e o broker Mosquitto são executados com Docker Compose.

> Os comandos abaixo devem ser executados no **Linux**, a partir da pasta do
> projeto.

## 1. Requisitos do sistema

- computador Linux `x86_64` com ambiente gráfico X11 ou XWayland;
- câmera GigE compatível com FlyCapture2 conectada à rede do computador;
- Docker Engine;
- Docker Compose V2 (comando `docker compose`);
- comando `xhost` (pacote `x11-xserver-utils` no Ubuntu);
- usuário autorizado a executar Docker sem `sudo`.

No Ubuntu, instale o utilitário do X11 caso ele ainda não exista:

```bash
sudo apt update
sudo apt install x11-xserver-utils
```

Instale o Docker seguindo a [documentação oficial do Docker
Engine](https://docs.docker.com/engine/install/) e confirme os requisitos:

```bash
uname -m
docker --version
docker compose version
echo "$DISPLAY"
```

O primeiro comando deve mostrar `x86_64` e o último não pode retornar vazio.

## 2. Conferir o IP usado pelo MQTT

O arquivo `docker-compose.yml` está configurado para usar o IP
`150.162.23.134`. Confirme se esse IP pertence ao computador:

```bash
ip -br addr
```

Se o computador usar outro IP, altere **as duas ocorrências** de
`150.162.23.134` no `docker-compose.yml`:

- `MQTT_HOST`, no serviço `laser-scanner`;
- `ports`, no serviço `mqtt-broker`.

## 3. Configurar a rede da câmera

Aumente o buffer de recepção do Linux para 10 MiB:

```bash
sudo sysctl -w net.core.rmem_max=10485760
sudo sysctl -w net.core.rmem_default=10485760
```

Descubra a interface de rede ligada à câmera:

```bash
ip -br addr
```

Ative jumbo frames nessa interface. Troque `enp30s0` pelo nome encontrado no
comando anterior:

```bash
sudo ip link set dev enp30s0 mtu 9000
```

Esses ajustes de buffer e MTU são temporários e precisam ser repetidos após
reiniciar o computador.

## 4. Autorizar a janela Qt

No mesmo terminal em que a aplicação será iniciada, execute:

```bash
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
xhost +SI:localuser:"$(id -un)"
```

Não use `sudo docker compose`: o contêiner precisa da identidade do usuário da
sessão gráfica para abrir a janela Qt e gravar arquivos na pasta pessoal.

## 5. Baixar e iniciar os contêineres

Baixe as imagens e inicie a aplicação e o broker MQTT:

```bash
docker compose pull
docker compose up
```

A janela da aplicação deve abrir e o terminal deve indicar que a câmera e o
MQTT foram conectados. Para encerrar, feche a janela ou pressione `Q` ou `Esc`.
Em seguida, se necessário, finalize os contêineres com `Ctrl+C`.

Para iniciar novamente depois da primeira execução, repita os ajustes das
seções 3 e 4 e execute:

```bash
docker compose up
```

## Problemas comuns

- **A janela Qt não abre:** confirme `echo "$DISPLAY"` e repita o comando
  `xhost` da seção 4.
- **A câmera não é encontrada ou perde imagens:** confira o cabo, o IP da
  interface, o MTU e os valores de `net.core.rmem_*`.
- **O MQTT não conecta:** confira se as duas ocorrências do IP no
  `docker-compose.yml` correspondem ao IP do computador e use
  `docker compose ps` para verificar os contêineres.
