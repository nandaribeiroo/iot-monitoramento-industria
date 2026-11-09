#include <WiFi.h>
#include <esp_now.h>
#include <esp_arduino_version.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <string.h>
#include <math.h>

// =========================================
// ETAPA 2 - ESP32 DE MONITORAMENTO
// =========================================

// MAC DO ESP32 DO CHAO DE FABRICA
const uint8_t macChaoFabrica[6] = {
  0x1C, 0x69, 0x20,
  0xE9, 0x37, 0x4C
};

// =========================================
// MATRIZ MAX7219 8x32
// =========================================

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define DATA_PIN 11
#define CLK_PIN 12
#define CS_PIN 10

// LEDs de status
#define LED_VERDE 7
#define LED_VERMELHO 6

MD_Parola display(
  HARDWARE_TYPE,
  DATA_PIN,
  CLK_PIN,
  CS_PIN,
  MAX_DEVICES
);

// Buffer permanente da matriz
char textoDisplay[40] = "";

// =========================================
// ESTRUTURA IDENTICA AO TRANSMISSOR
// =========================================

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

// =========================================
// TEMPORIZADORES
// =========================================

const unsigned long INTERVALO_TELA = 2000;
const unsigned long TIMEOUT_RX = 5000;

unsigned long ultimaTrocaTela = 0;
unsigned long ultimaVerificacao = 0;

int telaAtual = 0;
bool comunicacaoOK = false;
bool estadoAnterior = false;

// =========================================
// EXIBIR TEXTO NA MATRIZ
// =========================================

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

// =========================================
// EXIBIR TELAS DOS SENSORES
// =========================================

void mostrarTela(int tela) {

  DadosSensores d;

  portENTER_CRITICAL(&muxDados);
  memcpy(&d, &dadosRecebidos, sizeof(d));
  portEXIT_CRITICAL(&muxDados);

  char texto[40];

  switch (tela) {

    case 0:
      snprintf(
        texto, sizeof(texto),
        "NVL %d%%", d.nivelTinta
      );
      Serial.println("Tela -> NVL");
      break;

    case 1:
      snprintf(
        texto, sizeof(texto),
        "TMP %.1fC", d.temperatura
      );
      Serial.println("Tela -> TMP");
      break;

    case 2:
      snprintf(
        texto, sizeof(texto),
        "UMD %.0f%%", d.umidade
      );
      Serial.println("Tela -> UMD");
      break;

    case 3:
      snprintf(
        texto, sizeof(texto),
        "LUX %d", d.luminosidade
      );
      Serial.println("Tela -> LUX");
      break;

    case 4:
      snprintf(
        texto, sizeof(texto),
        "ALR %s",
        d.alerta ? "ON" : "OFF"
      );
      Serial.println("Tela -> ALR");
      break;

    default:
      return;
  }

  mostrarTexto(texto);
}

// =========================================
// VALIDAR MAC DO CHAO DE FABRICA
// =========================================

bool validarMAC(const uint8_t* mac) {

  if (mac == nullptr) {
    return false;
  }

  return memcmp(
    mac,
    macChaoFabrica,
    6
  ) == 0;
}

// =========================================
// PROCESSAR DADOS RECEBIDOS
// =========================================

void processarRecepcao(
  const uint8_t* mac,
  const uint8_t* dados,
  int tamanho
) {

  // Aceitar somente o ESP32 autorizado
  if (!validarMAC(mac)) {
    return;
  }

  // Validar tamanho do pacote
  if (tamanho != sizeof(DadosSensores)) {
    return;
  }

  DadosSensores temporario;

  memcpy(
    &temporario,
    dados,
    sizeof(temporario)
  );

  // Validacao dos valores recebidos
  if (
    !isfinite(temporario.temperatura) ||
    !isfinite(temporario.umidade) ||
    !isfinite(temporario.distancia) ||
    temporario.nivelTinta < -1 ||
    temporario.nivelTinta > 100 ||
    temporario.umidade < 0 ||
    temporario.umidade > 100 ||
    temporario.luminosidade < 0 ||
    temporario.luminosidade > 4095
  ) {
    return;
  }

  // Salvar dados com protecao
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

// =========================================
// CALLBACK ESP-NOW
// COMPATIVEL COM ESP32 2.x E 3.x
// =========================================

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

// =========================================
// SETUP
// =========================================

void setup() {

  Serial.begin(115200);
  delay(1500);

  // LEDs de comunicacao
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  // Matriz MAX7219
  display.begin();
  display.setIntensity(3);
  display.displayClear();

  mostrarTexto("AGUARDANDO");

  // Inicializar Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 DE MONITORAMENTO");
  Serial.println("========================================");

  Serial.print("MAC do receptor: ");
  Serial.println(WiFi.macAddress());

  Serial.println("MAC do chao de fabrica autorizado:");
  Serial.println("1C:69:20:E9:37:4C");

  // Inicializar ESP-NOW
  if (esp_now_init() != ESP_OK) {

    Serial.println("ERRO ao iniciar ESP-NOW");

    mostrarTexto("ERRO ESPNOW");

    while (true) {
      display.displayAnimate();
      delay(10);
    }
  }

  // Registrar callback de recepcao
  esp_err_t resultado =
    esp_now_register_recv_cb(OnDataRecv);

  if (resultado != ESP_OK) {

    Serial.println("ERRO ao registrar callback!");

    mostrarTexto("ERRO RX");

    while (true) {
      display.displayAnimate();
      delay(10);
    }
  }

  Serial.println("ESP-NOW inicializado.");
  Serial.println("Validacao de MAC ativada.");
  Serial.println("Aguardando dados do chao de fabrica.");
}

// =========================================
// LOOP PRINCIPAL
// =========================================

void loop() {

  unsigned long agora = millis();

  // Manter animacao da matriz funcionando
  display.displayAnimate();

  // =====================================
  // VERIFICAR COMUNICACAO A CADA 100 ms
  // =====================================

  if (agora - ultimaVerificacao >= 100) {

    ultimaVerificacao = agora;

    unsigned long ultima;

    portENTER_CRITICAL(&muxDados);
    ultima = ultimaRecepcao;
    portEXIT_CRITICAL(&muxDados);

    comunicacaoOK =
      (ultima != 0 &&
       agora - ultima <= TIMEOUT_RX);

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

        Serial.println(
          "LED VERDE ON - dados recebidos"
        );

        telaAtual = 0;
        mostrarTela(telaAtual);
        ultimaTrocaTela = agora;

      } else {

        Serial.println(
          "LED VERMELHO ON - timeout de comunicacao"
        );

        mostrarTexto("SEM DADOS");
      }
    }
  }

  // =====================================
  // MOSTRAR PACOTE NO MONITOR SERIAL
  // =====================================

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

    Serial.println(
      "Origem: 1C:69:20:E9:37:4C"
    );

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
      "Estado: %s\n",
      d.alerta ? "ALERTA" : "NORMAL"
    );

    Serial.println("========================================");
  }

  // =====================================
  // ALTERNAR TELAS A CADA 2 SEGUNDOS
  // =====================================

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
