# Laser Scanner Linux

Aplicação C++ para uma câmera compatível com FlyCapture2. O programa captura
quadros continuamente, converte cada imagem para BGR (`cv::Mat`) e exibe o vídeo
em uma janela do OpenCV limitada a aproximadamente 30 FPS.

A compilação e a execução são feitas exclusivamente com Docker. Não é necessário
instalar o FlyCapture2, o OpenCV ou ferramentas de compilação diretamente no host.
O CMake permanece no repositório porque é utilizado internamente durante a criação
da imagem Docker, mas não deve ser executado manualmente.

## Responsabilidades das classes

- `FlyCaptureCamera`: conexão, captura, tratamento do SDK e conversão para BGR.
- `ImagePathTrack`: detecção subpixel do centro da faixa laser por intensidade e pontos de inflexão.
- `VideoViewer`: janela do OpenCV, controles, cadência e comandos de teclado.

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

## Pré-requisitos do host

- Computador x86-64 conectado à câmera GigE.
- Docker Engine.
- `docker-compose`.
- Servidor gráfico X11 ou XWayland para a janela do OpenCV.

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

Configure o MTU da interface encontrada, substituindo `enp3s0` quando necessário:

```bash
sudo ip link set dev enp3s0 mtu 9000
ip link show dev enp3s0
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

## Permitir a janela do OpenCV

Autorize temporariamente o usuário `root` do container a acessar o X11 local:

```bash
xhost +SI:localuser:root
```

Não use `xhost +`, pois isso libera acesso indiscriminado ao servidor gráfico.

## Executar

```bash
docker-compose up
```

Para reconstruir a imagem e executar em um único comando:

```bash
docker-compose up --build
```

Pressione `Q` ou `Esc` na janela do OpenCV para encerrar o vídeo.

### Detecção da linha laser

A janela desenha em vermelho o centro detectado da faixa laser. Como a câmera é
monocromática, a detecção utiliza apenas a intensidade dos pixels; o formato BGR
é mantido para permitir a sobreposição colorida no vídeo.

O rastreamento combina três etapas: programação dinâmica para escolher uma linha
brilhante e contínua ao longo da imagem, centroide ponderado para localizar o
centro subpixel da espessura do laser e uma suavização curta contra ruído. O
caminho global pode variar até dois pixels verticalmente entre colunas vizinhas.

Durante a execução, dois controles ficam disponíveis na janela:

- `Limiar`: intensidade mínima, entre 0 e 255, usada para reconhecer o laser.
- `Exposição (us)`: tempo de exposição em microssegundos. O intervalo permitido
  é consultado diretamente na câmera pelo FlyCapture2.
