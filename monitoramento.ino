
#include <WiFi.h>
#include <esp_now.h>
#include <esp_arduino_version.h>
#include <HTTPClient.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <string.h>
#include <math.h>

// ============================================
// ESP32 2 - MONITORAMENTO
// ============================================

// WI-FI
const char* ssid = "CIMATEC-LABS-2.1.6";
const char* password = "SUA_SENHA_AQUI";

// ENDERECO DA API FASTAPI
const char* servidor =
  "http://172.28.53.56:8000/dados";

// MAC DO ESP32 DO CHAO DE FABRICA
const uint8_t macChaoFabrica[6] = {
  0x1C, 0x69, 0x20,
  0xE9, 0x37, 0x4C
};

// ============================================
// MATRIZ MAX7219
// ============================================

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define DATA_PIN 11
#define CLK_PIN 12
#define CS_PIN 10

#define LED_VERDE 7
#define LED_VERMELHO 6

MD_Parola display(
  HARDWARE_TYPE,
  DATA_PIN,
  CLK_PIN,
  CS_PIN,
  MAX_DEVICES
);

char textoDisplay[40];

// ============================================
// ESTRUTURA DOS DADOS
// DEVE SER IDENTICA AO TRANSMISSOR
// ============================================

typedef struct {
  float temperatura;
  float umidade;
  float distancia;
  int nivelTinta;
  int luminosidade;
  bool alerta;
} DadosSensores;

DadosSensores dadosRecebidos = {};

portMUX_TYPE muxDados =
  portMUX_INITIALIZER_UNLOCKED;

volatile bool pacoteNovo = false;
volatile unsigned long ultimaRecepcao = 0;

// ============================================
// INTERVALOS
// ============================================

const unsigned long INTERVALO_TELA = 2000;
const unsigned long TIMEOUT_RX = 5000;
const unsigned long INTERVALO_WIFI = 10000;

unsigned long ultimaTrocaTela = 0;
unsigned long ultimaVerificacao = 0;
unsigned long ultimaTentativaWiFi = 0;

int telaAtual = 0;

bool comunicacaoOK = false;
bool estadoAnterior = false;

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
// VERIFICAR WI-FI
// ============================================

void verificarWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (
    millis() - ultimaTentativaWiFi >=
    INTERVALO_WIFI
  ) {

    ultimaTentativaWiFi = millis();

    Serial.println("[WIFI] Tentando reconectar...");

    WiFi.reconnect();
  }
}

// ============================================
// EXIBIR TEXTO NA MATRIZ
// ============================================

void mostrarTexto(const char* texto) {

  strncpy(
    textoDisplay,
    texto,
    sizeof(textoDisplay) - 1
  );

  textoDisplay[sizeof(textoDisplay) - 1] = '\0';

  display.displayClear();

  display.displayText(
    textoDisplay,
    PA_CENTER,
    100,
    0,
    PA_SCROLL_LEFT,
    PA_SCROLL_LEFT
  );

  display.displayReset();
}

// ============================================
// EXIBIR DADOS NA MATRIZ
// ============================================

void mostrarTela(int tela) {

  DadosSensores d;

  portENTER_CRITICAL(&muxDados);
  memcpy(&d, &dadosRecebidos, sizeof(d));
  portEXIT_CRITICAL(&muxDados);

  char texto[40];

  switch (tela) {

    case 0:
      if (d.nivelTinta >= 0) {
        snprintf(texto, sizeof(texto), "NVL %d%%", d.nivelTinta);
      } else {
        snprintf(texto, sizeof(texto), "NVL ERRO");
      }
      break;

    case 1:
      snprintf(
        texto, sizeof(texto),
        "TMP %.1fC", d.temperatura
      );
      break;

    case 2:
      snprintf(
        texto, sizeof(texto),
        "UMD %.0f%%", d.umidade
      );
      break;

    case 3:
      snprintf(
        texto, sizeof(texto),
        "LUX %d", d.luminosidade
      );
      break;

    case 4:
      snprintf(
        texto, sizeof(texto),
        "ALR %s", d.alerta ? "ON" : "OFF"
      );
      break;

    default:
      return;
  }

  Serial.print("[MATRIZ] ");
  Serial.println(texto);

  mostrarTexto(texto);
}

// ============================================
// VALIDAR MAC DO TRANSMISSOR
// ============================================

bool validarMAC(const uint8_t* mac) {

  if (mac == nullptr) {
    return false;
  }

  return memcmp(mac, macChaoFabrica, 6) == 0;
}

// ============================================
// PROCESSAR RECEPCAO ESP-NOW
// ============================================

void processarRecepcao(
  const uint8_t* mac,
  const uint8_t* dados,
  int tamanho
) {

  if (!validarMAC(mac)) {
    return;
  }

  if (tamanho != sizeof(DadosSensores)) {
    Serial.println("[ESP-NOW] Tamanho de pacote invalido.");
    return;
  }

  DadosSensores temporario;

  memcpy(
    &temporario,
    dados,
    sizeof(temporario)
  );

  if (
    !isfinite(temporario.temperatura) ||
    !isfinite(temporario.umidade) ||
    !isfinite(temporario.distancia) ||
    temporario.nivelTinta < -1 ||
    temporario.nivelTinta > 100 ||
    temporario.distancia < -1 ||
    temporario.umidade < 0 ||
    temporario.umidade > 100 ||
    temporario.luminosidade < 0 ||
    temporario.luminosidade > 4095
  ) {

    Serial.println("[ESP-NOW] Pacote com valores invalidos.");
    return;
  }

  portENTER_CRITICAL(&muxDados);

  memcpy(
    &dadosRecebidos,
    &temporario,
    sizeof(temporario)
  );

  ultimaRecepcao = millis();
  pacoteNovo = true;

  portEXIT_CRITICAL(&muxDados);
}

// ============================================
// CALLBACK ESP-NOW
// COMPATIVEL COM CORE 2.X E 3.X
// ============================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void OnDataRecv(
  const esp_now_recv_info_t* info,
  const uint8_t* dados,
  int tamanho
) {

  if (info == nullptr) {
    return;
  }

  processarRecepcao(
    info->src_addr,
    dados,
    tamanho
  );
}

#else

void OnDataRecv(
  const uint8_t* mac,
  const uint8_t* dados,
  int tamanho
) {

  processarRecepcao(
    mac,
    dados,
    tamanho
  );
}

#endif

// ============================================
// ENVIAR DADOS PARA A API FASTAPI
// ============================================

void enviarParaAPI(DadosSensores d) {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("[HTTP] Wi-Fi desconectado. Dados nao enviados.");
    return;
  }

  WiFiClient cliente;
  HTTPClient http;

  http.setTimeout(3000);

  if (!http.begin(cliente, servidor)) {

    Serial.println("[HTTP] Falha ao iniciar requisicao.");
    return;
  }

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  char json[250];

  snprintf(
    json,
    sizeof(json),
    "{\"nivel_tinta\":%d,"
    "\"temperatura\":%.2f,"
    "\"umidade\":%.2f,"
    "\"distancia\":%.2f,"
    "\"luminosidade\":%d,"
    "\"alerta\":%s}",
    d.nivelTinta,
    d.temperatura,
    d.umidade,
    d.distancia,
    d.luminosidade,
    d.alerta ? "true" : "false"
  );

  Serial.println("[HTTP] Enviando dados:");
  Serial.println(json);

  int codigoHTTP = http.POST(json);

  if (codigoHTTP > 0) {

    Serial.print("[HTTP] Codigo de resposta: ");
    Serial.println(codigoHTTP);

    String resposta = http.getString();
    Serial.println("[HTTP] Resposta:");
    Serial.println(resposta);

    if (codigoHTTP == 200) {
      Serial.println("[HTTP] Dados armazenados com sucesso!");
    }

  } else {

    Serial.print("[HTTP] Erro: ");
    Serial.println(http.errorToString(codigoHTTP));
  }

  http.end();
}

// ============================================
// SETUP
// ============================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 DE MONITORAMENTO");
  Serial.println("========================================");

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  // INICIALIZAR MATRIZ
  display.begin();
  display.setIntensity(3);
  display.displayClear();

  mostrarTexto("AGUARDANDO");

  // CONECTAR AO WI-FI
  conectarWiFi();

  Serial.println("[ESP-NOW] Inicializando...");

  if (esp_now_init() != ESP_OK) {

    Serial.println("[ESP-NOW] ERRO ao inicializar!");

    mostrarTexto("ERRO ESPNOW");

    while (true) {
      display.displayAnimate();
      delay(10);
    }
  }

  esp_err_t resultado =
    esp_now_register_recv_cb(OnDataRecv);

  if (resultado != ESP_OK) {

    Serial.println("[ESP-NOW] Erro ao registrar callback!");

    mostrarTexto("ERRO RX");

    while (true) {
      display.displayAnimate();
      delay(10);
    }
  }

  Serial.println("[ESP-NOW] Inicializado!");
  Serial.println("[ESP-NOW] Aguardando dados do chao de fabrica.");

  Serial.print("[HTTP] API configurada: ");
  Serial.println(servidor);
}

// ============================================
// LOOP PRINCIPAL
// ============================================

void loop() {

  unsigned long agora = millis();

  // ANIMACAO DA MATRIZ
  if (display.displayAnimate()) {
    display.displayReset();
  }

  // VERIFICAR WI-FI
  verificarWiFi();

  // ==========================================
  // VERIFICAR COMUNICACAO ESP-NOW
  // ==========================================

  if (agora - ultimaVerificacao >= 100) {

    ultimaVerificacao = agora;

    unsigned long ultima;

    portENTER_CRITICAL(&muxDados);
    ultima = ultimaRecepcao;
    portEXIT_CRITICAL(&muxDados);

    comunicacaoOK =
      (ultima != 0 && agora - ultima <= TIMEOUT_RX);

    if (comunicacaoOK != estadoAnterior) {

      estadoAnterior = comunicacaoOK;

      digitalWrite(
        LED_VERDE,
        comunicacaoOK ? HIGH : LOW
      );

      digitalWrite(
        LED_VERMELHO,
        comunicacaoOK ? LOW : HIGH
      );

      if (comunicacaoOK) {

        Serial.println("[STATUS] Comunicacao ESP-NOW OK.");

        telaAtual = 0;
        mostrarTela(telaAtual);
        ultimaTrocaTela = agora;

      } else {

        Serial.println("[STATUS] Sem comunicacao ESP-NOW.");

        mostrarTexto("SEM DADOS");
      }
    }
  }

  // ==========================================
  // PROCESSAR NOVO PACOTE
  // ==========================================

  bool recebeu = false;
  DadosSensores d = {};

  portENTER_CRITICAL(&muxDados);

  if (pacoteNovo) {

    pacoteNovo = false;
    recebeu = true;

    memcpy(
      &d,
      &dadosRecebidos,
      sizeof(d)
    );
  }

  portEXIT_CRITICAL(&muxDados);

  if (recebeu) {

    Serial.println();
    Serial.println("========================================");
    Serial.println(" PACOTE RECEBIDO VIA ESP-NOW");
    Serial.println("========================================");

    Serial.printf(
      "Nivel do tanque: %d%%\n",
      d.nivelTinta
    );

    Serial.printf(
      "Temperatura: %.1f C\n",
      d.temperatura
    );

    Serial.printf(
      "Umidade: %.1f%%\n",
      d.umidade
    );

    Serial.printf(
      "Distancia: %.1f cm\n",
      d.distancia
    );

    Serial.printf(
      "Luminosidade: %d\n",
      d.luminosidade
    );

    Serial.printf(
      "Alerta: %s\n",
      d.alerta ? "SIM" : "NAO"
    );

    Serial.println("========================================");

    // ENVIAR PARA A API
    enviarParaAPI(d);
  }

  // ==========================================
  // ALTERNAR TELAS DA MATRIZ
  // ==========================================

  agora = millis();

  if (
    comunicacaoOK &&
    agora - ultimaTrocaTela >= INTERVALO_TELA
  ) {

    ultimaTrocaTela = agora;

    telaAtual = (telaAtual + 1) % 5;

    mostrarTela(telaAtual);
  }

  delay(10);
}
