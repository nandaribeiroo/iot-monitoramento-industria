# Sistema IoT de Monitoramento Industrial

Sistema de monitoramento IoT desenvolvido para acompanhar as condições de um processo industrial de pintura, utilizando microcontroladores ESP32, sensores, comunicação sem fio, uma API em Python e armazenamento de dados no InfluxDB.

O projeto permite coletar informações do ambiente e do tanque de tinta, transmitir os dados entre dispositivos ESP32, apresentar informações em uma matriz de LEDs e encaminhar as medições para armazenamento em banco de dados.

## 1. Arquitetura do projeto

O sistema é composto por dois dispositivos ESP32 com responsabilidades distintas:

### ESP32 – Chão de fábrica

Responsável pela coleta das informações do processo por meio dos sensores conectados ao microcontrolador.

As informações monitoradas incluem:

- Temperatura;
- Umidade;
- Distância medida pelo sensor ultrassônico;
- Nível estimado de tinta no tanque;
- Luminosidade;
- Estado de alerta do sistema.

O dispositivo realiza as leituras periodicamente, processa os valores, atualiza os LEDs indicadores e transmite os dados ao ESP32 de monitoramento por meio da comunicação ESP-NOW.

### ESP32 – Monitoramento

Responsável por receber os dados enviados pelo ESP32 do chão de fábrica.

Suas principais funções são:

- Receber os pacotes por ESP-NOW;
- Processar e apresentar as informações na matriz de LEDs MAX7219;
- Estabelecer conexão com a rede Wi-Fi;
- Encaminhar as informações para o backend por meio de requisições HTTP.

## 2. Componentes utilizados

Os principais componentes utilizados no sistema são:

| Componente | Finalidade |
|---|---|
| ESP32 | Processamento e comunicação dos dispositivos |
| Sensor DHT22 | Medição de temperatura e umidade |
| Sensor ultrassônico HC-SR04 | Medição da distância até a superfície da tinta |
| Sensor LDR | Leitura da luminosidade |
| LED verde | Indicação de condição normal |
| LED vermelho | Indicação de alerta |
| Matriz de LEDs MAX7219 | Exibição das informações de monitoramento |

## 3. Comunicação entre os dispositivos

A comunicação entre os dois ESP32 é realizada utilizando ESP-NOW, protocolo que permite a transmissão de dados diretamente entre dispositivos ESP32.

O ESP32 do chão de fábrica coleta as informações dos sensores e organiza os valores em uma estrutura de dados denominada `DadosSensores
