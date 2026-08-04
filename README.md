# Laser Scanner Linux

Aplicação C++/Qt para uma câmera compatível com FlyCapture2. O programa captura
quadros continuamente, converte cada imagem para BGR (`cv::Mat`), preserva o
processamento e os desenhos em OpenCV e exibe o vídeo em uma janela Qt limitada
a aproximadamente 30 FPS.

A compilação e a execução são feitas exclusivamente com Docker. Não é necessário
instalar o FlyCapture2, o OpenCV, o Qt ou ferramentas de compilação diretamente
no host. O CMake permanece no repositório porque é utilizado internamente durante
a criação da imagem Docker, mas não deve ser executado manualmente.

## Preparar os pacotes FlyCapture2

O Docker só acessa arquivos dentro da pasta usada como contexto de build. Copie
os pacotes do SDK para o projeto:

```bash
cd ~/Documents/laser-scanner
mkdir -p third_party/flycapture2
cp ~/Documents/flycapture2-2.13.3.31-amd64/*.deb third_party/flycapture2/
```

Confira se os dois pacotes necessários estão presentes:

```bash
ls -lh third_party/flycapture2/libflycapture-2*.deb
ls -lh third_party/flycapture2/libflycapture-dev*.deb
```

## Pré-requisitos

- Computador x86-64 conectado à câmera GigE.
- Docker Engine.
- Servidor gráfico X11 ou XWayland para a janela Qt.

Verifique as ferramentas:

```bash
docker --version
docker-compose --version
uname -m
echo "$DISPLAY"
```

O resultado de `uname -m` deve ser `x86_64`, pois o SDK utilizado é AMD64.

## Configurar a rede GigE no host

O container utiliza a rede do host. Portanto, MTU e buffers de recepção devem
ser configurados no host, não no Dockerfile.

Configure temporariamente buffers de recepção de 10 MiB:

```bash
sudo sysctl -w net.core.rmem_max=10485760
sudo sysctl -w net.core.rmem_default=10485760
```

Descubra a interface que alcança a câmera:

```bash
ip -br addr
```

Configure o MTU da interface encontrada, substituindo `enp30s0` quando necessário:

```bash
sudo ip link set dev enp30s0 mtu 9000
ip link show dev enp30s0
```

A câmera, a placa de rede e qualquer switch intermediário precisam suportar o
mesmo tamanho de pacote.

## Criar e compilar a imagem

Na primeira compilação, ou depois de alterar os pacotes FlyCapture2, execute:

```bash
docker-compose build --no-cache
```

Para recompilações normais após alterações no código:

```bash
docker-compose build
```

Confira a imagem criada:

```bash
docker image ls | grep laser-scanner
```

## Permitir a janela Qt

Informe ao Compose o UID e o GID do usuário da sessão gráfica:

```bash
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
```

O Compose monta automaticamente a variável `HOME` da sessão atual. Execute os
comandos como o usuário da sessão gráfica, sem `sudo`, para que ela corresponda
à pasta pessoal e ao `HOST_UID` informados.

Confira antes de executar:

```bash
test -n "$HOME" && test -d "$HOME" && test -w "$HOME"
```

O container executa a aplicação com essa identidade, evitando acesso como
`root` ao X11 e ao barramento de acessibilidade AT-SPI da sessão. Se o X11
ainda exigir autorização explícita, libere somente o usuário atual:

```bash
xhost +SI:localuser:"$(id -un)"
```

## Executar

```bash
docker-compose up
```

!!! warning "Atenção"
    No Ubuntu 18.04, a combinação Qt 5.9/libdbus 1.12 pode imprimir o aviso
    `last reference on a private connection was dropped without closing the
    connection` ao inicializar a acessibilidade AT-SPI. A aplicação configura esse
    aviso como não fatal antes de criar o `QApplication`; portanto, ele pode
    continuar no terminal, mas não deve mais produzir `SIGABRT` nem encerrar o
    container com código 134/139.

Feche a janela Qt, ou pressione `Q` ou `Esc`, para encerrar o vídeo.

