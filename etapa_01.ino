#include <WiFi.h>
#include <esp_now.h>
#include <esp_arduino_version.h>
#include <DHT22.h>
#include <string.h>

// ============================================================
// PROJETO DE PINTURA DE BLOCOS DE MADEIRA
// ESP32 1 - CHAO DE FABRICA
// SENAI CIMATEC - CAMADA DE SERVICO
// ============================================================

// ============================================================
// ENDERECO MAC DO ESP32 DE MONITORAMENTO
// ============================================================

uint8_t macMonitoramento[] = {
  0x80, 0xB5, 0x4E,
  0xC6, 0x5C, 0x0C
};

// ============================================================
// DEFINICAO DOS PINOS
// ============================================================

// Sensor DHT22
#define DHT_PIN 17
DHT22 dht22(DHT_PIN);

// Sensor LDR
#define LDR_PIN 35

// Limite de luminosidade
#define LIMIAR_LUMINOSIDADE 2000

// Sensor ultrassonico HC-SR04
#define TRIG_PIN 13
#define ECHO_PIN 12

// LEDs
#define LED_VERDE 4
#define LED_VERMELHO 2

// ============================================================
// CONFIGURACAO DO TANQUE
// ============================================================

#define DISTANCIA_TANQUE_VAZIO 30.0
#define DISTANCIA_TANQUE_CHEIO 5.0
#define NIVEL_MINIMO 20

// ============================================================
// VARIAVEIS
// ============================================================

float temperatura = 0.0;
float umidade = 0.0;
float distancia = 0.0;

int nivelTinta = 0;
int luminosidade = 0;

bool leituraDHTValida = false;
bool alertaNivelBaixo = false;
bool espNowPronto = false;

unsigned long ultimaLeitura = 0;

const unsigned long INTERVALO_LEITURA = 2000;

// ============================================================
// ESTRUTURA DOS DADOS ESP-NOW
// DEVE SER IDENTICA A DO RECEPTOR
// ============================================================

typedef struct {

  float temperatura;
  float umidade;
  float distancia;

  int nivelTinta;
  int luminosidade;

  bool alerta;

} DadosSensores;

DadosSensores dados;

// ============================================================
// CALLBACK DE ENVIO ESP-NOW
// ============================================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void OnDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status
) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println(
      "[ESP-NOW] Entrega confirmada na camada MAC."
    );
  } else {
    Serial.println(
      "[ESP-NOW] FALHA na entrega do pacote!"
    );
  }
}

#else

void OnDataSent(
  const uint8_t *mac,
  esp_now_send_status_t status
) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println(
      "[ESP-NOW] Entrega confirmada na camada MAC."
    );
  } else {
    Serial.println(
      "[ESP-NOW] FALHA na entrega do pacote!"
    );
  }
}

#endif

// ============================================================
// LEITURA DO DHT22
// ============================================================

bool lerDHT22(float &temp, float &hum) {

  temp = dht22.getTemperature();
  hum = dht22.getHumidity();

  if (dht22.getLastError() != dht22.OK) {

    Serial.print("ERRO DHT22, codigo: ");
    Serial.println(dht22.getLastError());

    return false;
  }

  return true;
}

// ============================================================
// LEITURA DO SENSOR ULTRASSONICO
// ============================================================

float medirDistancia() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duracao =
    pulseIn(ECHO_PIN, HIGH, 30000);

  if (duracao == 0) {
    return -1.0;
  }

  return duracao * 0.0343 / 2.0;
}

// ============================================================
// CALCULO DO NIVEL DE TINTA
// ============================================================

int calcularNivelTinta(float distanciaAtual) {

  if (distanciaAtual < 0) {
    return -1;
  }

  if (
    DISTANCIA_TANQUE_VAZIO <=
    DISTANCIA_TANQUE_CHEIO
  ) {
    return -1;
  }

  float nivel =
    (DISTANCIA_TANQUE_VAZIO - distanciaAtual) /
    (DISTANCIA_TANQUE_VAZIO - DISTANCIA_TANQUE_CHEIO);

  nivel *= 100.0;

  nivel = constrain(nivel, 0.0, 100.0);

  return (int)(nivel + 0.5);
}

// ============================================================
// INICIALIZACAO DO ESP-NOW
// ============================================================

void iniciarESPNow() {

  WiFi.mode(WIFI_STA);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" INICIALIZANDO COMUNICACAO ESP-NOW");
  Serial.println("========================================");

  Serial.print("MAC do chao de fabrica: ");
  Serial.println(WiFi.macAddress());

  Serial.println(
    "MAC do monitoramento: 80:B5:4E:C6:5C:0C"
  );

  // Inicializa ESP-NOW
  if (esp_now_init() != ESP_OK) {

    Serial.println(
      "ERRO: nao foi possivel iniciar ESP-NOW!"
    );

    espNowPronto = false;
    return;
  }

  // Registra callback de envio
  esp_err_t callbackResultado =
    esp_now_register_send_cb(OnDataSent);

  if (callbackResultado != ESP_OK) {

    Serial.printf(
      "ERRO ao registrar callback: %d\n",
      callbackResultado
    );

    espNowPronto = false;
    return;
  }

  // Configura o receptor
  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    macMonitoramento,
    6
  );

  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  // Cadastra o receptor
  esp_err_t resultado =
    esp_now_add_peer(&peerInfo);

  if (
    resultado == ESP_OK ||
    resultado == ESP_ERR_ESPNOW_EXIST
  ) {

    espNowPronto = true;

    Serial.println(
      "Receptor ESP-NOW cadastrado com sucesso!"
    );

    Serial.println(
      "Modo de envio: UNICAST"
    );

    Serial.println(
      "Destino: 80:B5:4E:C6:5C:0C"
    );

  } else {

    espNowPronto = false;

    Serial.printf(
      "ERRO ao cadastrar receptor: %d\n",
      resultado
    );
  }
}

// ============================================================
// CONTROLE DOS LEDS
// ============================================================

void atualizarLEDs() {

  bool nivelBaixo =
    (nivelTinta >= 0 &&
     nivelTinta < NIVEL_MINIMO);

  bool nivelInvalido =
    (nivelTinta < 0);

  bool poucaLuminosidade =
    (luminosidade < LIMIAR_LUMINOSIDADE);

  if (
    nivelBaixo ||
    nivelInvalido ||
    poucaLuminosidade
  ) {

    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);

  } else {

    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_VERMELHO, LOW);
  }
}

// ============================================================
// LEITURA DE TODOS OS SENSORES
// ============================================================

void lerSensores() {

  // DHT22
  float novaTemperatura;
  float novaUmidade;

  leituraDHTValida = lerDHT22(
    novaTemperatura,
    novaUmidade
  );

  if (leituraDHTValida) {

    temperatura = novaTemperatura;
    umidade = novaUmidade;
  }

  // LDR
  luminosidade = analogRead(LDR_PIN);

  Serial.print("[LDR] Valor analogico: ");
  Serial.print(luminosidade);

  Serial.print(" | Classificacao: ");

  if (luminosidade < LIMIAR_LUMINOSIDADE) {
    Serial.println("BAIXA");
  } else {
    Serial.println("ADEQUADA");
  }

  // Sensor ultrassonico
  distancia = medirDistancia();

  // Nivel do tanque
  nivelTinta = calcularNivelTinta(distancia);

  alertaNivelBaixo =
    (nivelTinta >= 0 &&
     nivelTinta < NIVEL_MINIMO);

  // Atualizar LEDs
  atualizarLEDs();
}

// ============================================================
// EXIBICAO DAS INFORMACOES
// ============================================================

void mostrarInformacoes() {

  Serial.println();
  Serial.println("========================================");
  Serial.println(" MONITORAMENTO DO TANQUE DE TINTA");
  Serial.println("========================================");

  // Nivel
  Serial.print("Nivel do tanque: ");

  if (nivelTinta >= 0) {

    Serial.print(nivelTinta);
    Serial.println("%");

  } else {

    Serial.println("Leitura invalida");
  }

  // Distancia
  Serial.print("Distancia da superficie: ");

  if (distancia >= 0) {

    Serial.print(distancia, 1);
    Serial.println(" cm");

  } else {

    Serial.println("Sem leitura");
  }

  // Temperatura e umidade
  if (leituraDHTValida) {

    Serial.print("Temperatura: ");
    Serial.print(temperatura, 1);

    Serial.print(" C | Umidade: ");
    Serial.print(umidade, 1);

    Serial.println("%");

  } else {

    Serial.println(
      "Temperatura/umidade: falha na leitura do DHT22"
    );
  }

  // Luminosidade
  Serial.print("Luminosidade (ADC): ");
  Serial.println(luminosidade);

  Serial.print("Estado da luminosidade: ");

  if (luminosidade < LIMIAR_LUMINOSIDADE) {
    Serial.println("BAIXA");
  } else {
    Serial.println("ADEQUADA");
  }

  // Estado do sistema
  Serial.print("Estado do sistema: ");

  if (nivelTinta < 0) {

    Serial.println(
      "ALERTA - verificar sensor ultrassonico"
    );

  } else if (alertaNivelBaixo) {

    Serial.println(
      "ALERTA - verificar tanque de tinta"
    );

    Serial.println(
      "Alerta! Nivel de tinta baixo."
    );

  } else {

    Serial.println(
      "Operacao normal"
    );
  }

  // LEDs
  Serial.print("LED verde: ");

  Serial.println(
    digitalRead(LED_VERDE) ?
    "ACESO" : "APAGADO"
  );

  Serial.print("LED vermelho: ");

  Serial.println(
    digitalRead(LED_VERMELHO) ?
    "ACESO" : "APAGADO"
  );

  Serial.println("========================================");
}

// ============================================================
// ENVIO DOS DADOS VIA ESP-NOW
// ============================================================

void enviarDados() {

  // Verificar inicializacao
  if (!espNowPronto) {

    Serial.println(
      "[ESP-NOW] ERRO: comunicacao nao inicializada."
    );

    return;
  }

  // Preencher pacote
  dados.temperatura = temperatura;
  dados.umidade = umidade;
  dados.distancia = distancia;

  dados.nivelTinta = nivelTinta;
  dados.luminosidade = luminosidade;

  dados.alerta =
    alertaNivelBaixo ||
    nivelTinta < 0 ||
    luminosidade < LIMIAR_LUMINOSIDADE;

  // Enviar para o monitoramento
  esp_err_t resultado = esp_now_send(
    macMonitoramento,
    (uint8_t *)&dados,
    sizeof(dados)
  );

  if (resultado == ESP_OK) {

    Serial.println(
      "[ESP-NOW] Pacote encaminhado para transmissao."
    );

  } else {

    Serial.printf(
      "[ESP-NOW] Falha ao solicitar envio. Codigo: %d\n",
      resultado
    );
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" SISTEMA IOT - PROCESSO DE PINTURA");
  Serial.println(" ESP32 DO CHAO DE FABRICA");
  Serial.println("========================================");

  // Configuracao dos pinos
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  pinMode(LDR_PIN, INPUT);

  // Resolucao ADC de 12 bits
  analogReadResolution(12);

  // Estado inicial
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  // Inicializar ESP-NOW
  iniciarESPNow();

  Serial.println();
  Serial.println("Sensores inicializados.");
  Serial.println("Leituras a cada 2 segundos.");
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================

void loop() {

  if (
    millis() - ultimaLeitura >=
    INTERVALO_LEITURA
  ) {

    ultimaLeitura = millis();

    // 1. Ler sensores
    lerSensores();

    // 2. Mostrar informacoes
    mostrarInformacoes();

    // 3. Enviar dados via ESP-NOW
    enviarDados();
  }
}
