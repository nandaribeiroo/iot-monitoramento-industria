
#include <WiFi.h>
#include <esp_now.h>
#include <esp_arduino_version.h>
#include <DHT22.h>
#include <string.h>

// ============================================
// ESP32 1 - CHAO DE FABRICA
// ============================================

// WI-FI
const char* ssid = "CIMATEC-LABS-2.1.6";
const char* password = "cimateceixo";

// MAC DO ESP32 DE MONITORAMENTO
uint8_t macMonitoramento[] = {
  0x80, 0xB5, 0x4E,
  0xC6, 0x5C, 0x0C
};

// PINOS DOS SENSORES
#define DHT_PIN 17
#define LDR_PIN 35
#define TRIG_PIN 13
#define ECHO_PIN 12

// PINOS DOS LEDS
#define LED_VERDE 4
#define LED_VERMELHO 2

// CONFIGURACAO DO TANQUE
#define DISTANCIA_TANQUE_VAZIO 30.0
#define DISTANCIA_TANQUE_CHEIO 5.0
#define NIVEL_MINIMO 20

// LIMIAR DE LUMINOSIDADE
#define LIMIAR_LUMINOSIDADE 2000

DHT22 dht22(DHT_PIN);

// INTERVALOS
const unsigned long INTERVALO_LEITURA = 2000;
const unsigned long INTERVALO_RECONEXAO = 10000;

unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaWiFi = 0;

// VARIAVEIS DOS SENSORES
float temperatura = 0.0;
float umidade = 0.0;
float distancia = -1.0;

int nivelTinta = -1;
int luminosidade = 0;

bool leituraDHTValida = false;
bool espNowPronto = false;

// ESTRUTURA DOS DADOS
// DEVE SER IDENTICA NO RECEPTOR
typedef struct {
  float temperatura;
  float umidade;
  float distancia;
  int nivelTinta;
  int luminosidade;
  bool alerta;
} DadosSensores;

DadosSensores dados;

// ============================================
// CONEXAO WI-FI
// ============================================

void conectarWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  Serial.println();
  Serial.println("[WIFI] Conectando...");
  Serial.print("[WIFI] Rede: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int tentativas = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    tentativas < 30
  ) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("[WIFI] CONECTADO!");

    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("[WIFI] MAC: ");
    Serial.println(WiFi.macAddress());

    Serial.print("[WIFI] Canal: ");
    Serial.println(WiFi.channel());

  } else {

    Serial.println("[WIFI] FALHA NA CONEXAO!");
    Serial.print("[WIFI] Status: ");
    Serial.println(WiFi.status());
    Serial.println("[WIFI] Nova tentativa em ate 10 segundos.");
  }
}

// ============================================
// VERIFICAR E RECONECTAR WI-FI
// ============================================

void verificarWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (
    millis() - ultimaTentativaWiFi >=
    INTERVALO_RECONEXAO
  ) {

    ultimaTentativaWiFi = millis();

    Serial.println("[WIFI] Tentando reconectar...");

    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

// ============================================
// CALLBACK DE ENVIO ESP-NOW
// ============================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void OnDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status
) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] Entrega confirmada.");
  } else {
    Serial.println("[ESP-NOW] FALHA na entrega!");
  }
}

#else

void OnDataSent(
  const uint8_t *mac,
  esp_now_send_status_t status
) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] Entrega confirmada.");
  } else {
    Serial.println("[ESP-NOW] FALHA na entrega!");
  }
}

#endif

// ============================================
// LEITURA DO DHT22
// ============================================

bool lerDHT22(float &temp, float &hum) {

  temp = dht22.getTemperature();
  hum = dht22.getHumidity();

  if (dht22.getLastError() != dht22.OK) {

    Serial.print("[DHT22] Erro: ");
    Serial.println(dht22.getLastError());

    return false;
  }

  return true;
}

// ============================================
// MEDIR DISTANCIA - HC-SR04
// ============================================

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

// ============================================
// CALCULAR NIVEL DE TINTA
// ============================================

int calcularNivelTinta(float distanciaAtual) {

  if (distanciaAtual < 0) {
    return -1;
  }

  float nivel =
    (DISTANCIA_TANQUE_VAZIO - distanciaAtual) /
    (DISTANCIA_TANQUE_VAZIO - DISTANCIA_TANQUE_CHEIO);

  nivel *= 100.0;

  nivel = constrain(nivel, 0.0, 100.0);

  return (int)(nivel + 0.5);
}

// ============================================
// INICIALIZAR ESP-NOW
// ============================================

void iniciarESPNow() {

  Serial.println();
  Serial.println("[ESP-NOW] Inicializando...");

  if (esp_now_init() != ESP_OK) {

    Serial.println("[ESP-NOW] ERRO ao inicializar!");
    espNowPronto = false;
    return;
  }

  esp_err_t callbackResultado =
    esp_now_register_send_cb(OnDataSent);

  if (callbackResultado != ESP_OK) {

    Serial.println("[ESP-NOW] Erro no callback!");
    espNowPronto = false;
    return;
  }

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    macMonitoramento,
    6
  );

  // Usa o canal atual da interface Wi-Fi
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  esp_err_t resultado =
    esp_now_add_peer(&peerInfo);

  if (
    resultado == ESP_OK ||
    resultado == ESP_ERR_ESPNOW_EXIST
  ) {

    espNowPronto = true;

    Serial.println("[ESP-NOW] Receptor cadastrado!");
    Serial.println("[ESP-NOW] Destino: 80:B5:4E:C6:5C:0C");

  } else {

    espNowPronto = false;

    Serial.print("[ESP-NOW] Erro ao cadastrar receptor: ");
    Serial.println(resultado);
  }
}

// ============================================
// LEITURA DOS SENSORES
// ============================================

void lerSensores() {

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

  // ULTRASSONICO
  distancia = medirDistancia();

  // NIVEL DO TANQUE
  nivelTinta = calcularNivelTinta(distancia);

  Serial.print("[LDR] Luminosidade: ");
  Serial.println(luminosidade);
}

// ============================================
// ATUALIZAR LEDS
// ============================================

void atualizarLEDs() {

  bool nivelBaixo =
    nivelTinta >= 0 &&
    nivelTinta < NIVEL_MINIMO;

  bool nivelInvalido = nivelTinta < 0;

  bool poucaLuminosidade =
    luminosidade < LIMIAR_LUMINOSIDADE;

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

// ============================================
// MOSTRAR INFORMACOES NO SERIAL
// ============================================

void mostrarInformacoes() {

  Serial.println();
  Serial.println("========================================");
  Serial.println(" MONITORAMENTO DO TANQUE DE TINTA");
  Serial.println("========================================");

  Serial.print("Nivel do tanque: ");

  if (nivelTinta >= 0) {
    Serial.print(nivelTinta);
    Serial.println("%");
  } else {
    Serial.println("Leitura invalida");
  }

  Serial.print("Distancia: ");

  if (distancia >= 0) {
    Serial.print(distancia, 1);
    Serial.println(" cm");
  } else {
    Serial.println("Sem leitura");
  }

  if (leituraDHTValida) {

    Serial.print("Temperatura: ");
    Serial.print(temperatura, 1);

    Serial.print(" C | Umidade: ");
    Serial.print(umidade, 1);

    Serial.println("%");

  } else {

    Serial.println("Falha na leitura do DHT22");
  }

  Serial.print("Luminosidade: ");
  Serial.println(luminosidade);

  Serial.print("Estado do sistema: ");

  if (nivelTinta < 0) {

    Serial.println("ALERTA - verificar ultrassonico");

  } else if (nivelTinta < NIVEL_MINIMO) {

    Serial.println("ALERTA - nivel de tinta baixo");

  } else {

    Serial.println("Operacao normal");
  }

  Serial.println("========================================");
}

// ============================================
// ENVIAR DADOS VIA ESP-NOW
// ============================================

void enviarDados() {

  if (!espNowPronto) {

    Serial.println("[ESP-NOW] Comunicacao nao inicializada.");
    return;
  }

  dados.temperatura = temperatura;
  dados.umidade = umidade;
  dados.distancia = distancia;
  dados.nivelTinta = nivelTinta;
  dados.luminosidade = luminosidade;

  dados.alerta =
    (nivelTinta >= 0 && nivelTinta < NIVEL_MINIMO) ||
    nivelTinta < 0 ||
    luminosidade < LIMIAR_LUMINOSIDADE;

  esp_err_t resultado = esp_now_send(
    macMonitoramento,
    (uint8_t *)&dados,
    sizeof(dados)
  );

  if (resultado == ESP_OK) {

    Serial.println("[ESP-NOW] Pacote encaminhado.");

  } else {

    Serial.print("[ESP-NOW] Falha no envio. Codigo: ");
    Serial.println(resultado);
  }
}

// ============================================
// SETUP
// ============================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 - CHAO DE FABRICA");
  Serial.println("========================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  pinMode(LDR_PIN, INPUT);

  analogReadResolution(12);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  // Conectar ao Wi-Fi antes do ESP-NOW
  conectarWiFi();

  Serial.print("[WIFI] Canal atual: ");
  Serial.println(WiFi.channel());

  iniciarESPNow();

  Serial.println();
  Serial.println("[SISTEMA] Sensores inicializados.");
  Serial.println("[SISTEMA] Leituras a cada 2 segundos.");
}

// ============================================
// LOOP
// ============================================

void loop() {

  verificarWiFi();

  if (
    millis() - ultimaLeitura >=
    INTERVALO_LEITURA
  ) {

    ultimaLeitura = millis();

    lerSensores();

    atualizarLEDs();

    mostrarInformacoes();

    enviarDados();
  }
}
