# Laser Scanner Linux

Captura imagens de uma camera compativel com FlyCapture2, converte cada quadro
para BGR (`cv::Mat`) e exibe o fluxo em uma janela do OpenCV a 30 FPS.

## Estrutura

```text
include/
  FlyCaptureCamera.hpp
  VideoViewer.hpp
src/
  FlyCaptureCamera.cpp
  VideoViewer.cpp
  main.cpp
CMakeLists.txt
```

`FlyCaptureCamera` controla conexao, captura e conversao para BGR.
`VideoViewer` controla a janela, o teclado e a cadencia de exibicao.

## Compilacao

A partir da raiz do repositório, rode

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Configurar bufer de recepção

É necessário aumentar a capacidade do buffer da placa de rede para no mínimo 10Mb, para isso, rode:

```bash
sudo sysctl -w net.core.rmem_max=10485760
sudo sysctl -w net.core.rmem_default=10485760
```

## Execucao

```bash
./build/laser_scanner_linux
```

Pressione `Q` ou `Esc` para encerrar.

O programa tenta configurar a camera para 30 FPS. Se o modelo ou o modo atual
nao aceitar essa propriedade, a janela permanece limitada a 30 FPS, mas a taxa
real nao pode ultrapassar a taxa entregue pela camera.
