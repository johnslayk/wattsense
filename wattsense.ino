#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_ADS1X15.h>

// Wi-Fi --------------------------------------------------------
const char* ssid     = "Jonathan";
const char* password = "12345678";

// Broker MQTT
const char* mqttServer = "broker.hivemq.com";
const int   mqttPort   = 1883;

// Tópicos MQTT --------------------------------------------------
const char* topico_faseA  = "unifeob/lab1/energia/faseA";
const char* topico_faseB  = "unifeob/lab1/energia/faseB";
const char* topico_faseC  = "unifeob/lab1/energia/faseC";
const char* topico_total  = "unifeob/lab1/energia/total";
const char* topico_kwh    = "unifeob/lab1/energia/kwh";
const char* topico_status = "unifeob/lab1/energia/status";

// Parâmetros elétricos ------------------------------------------
const float TENSAO_NOMINAL = 127.0;  //
const float FATOR_POTENCIA = 0.92;

// Calibração ADS1115 + TC ---------------------------------------
// GAIN_ONE → ±4.096V → 1 bit = 0.125 mV — seguro para 2.5V AC
// 3 voltas no TC → divide resultado por 3
//#define ADS_SCALE     0.000125f    // V/bit com GAIN_ONE
#define ADS_SCALE  0.0000078f      // PARA GAIN_SIXTEEN

#define BURDEN_OHM    33.0f
#define TC_RATIO      2000.0f      // 100A / 50mA
#define NUM_VOLTAS    3            // Quantidade de voltas do fio no TC para teste
#define NUM_SAMPLES   300

#define OFFSET_TC1  0.047f  // medido no A0
#define OFFSET_TC2  0.047f  // medido no A1
#define OFFSET_TC3  0.047f  // medido no A2

//#define LED_BUILTIN 2


// Intervalo de publicação ---------------------------------------
const unsigned long INTERVALO_ENVIO = 5000;

// Objetos -------------------------------------------------------
Adafruit_ADS1115  ads;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient        espClient;
PubSubClient      client(espClient);

// Variáveis de controle ------------------------------------------
float         energia_kwh  = 0.0;
unsigned long ultimo_tempo = 0;
unsigned long ultimo_envio = 0;
unsigned long ultimo_swap  = 0;
int           canal_display = 0;


//  Lê corrente RMS de um canal do ADS1115
//  Divide por NUM_VOLTAS para compensar as voltas extras no TC
float lerCorrente(uint8_t canal) {
  double sumSq = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float v = ads.readADC_SingleEnded(canal) * ADS_SCALE;
    sumSq += (double)v * v;
  }
  float vrms = sqrt(sumSq / NUM_SAMPLES);
  float irms = (vrms / BURDEN_OHM) * TC_RATIO / NUM_VOLTAS;

  // aplica offset individual por canal
  float offset = 0;
  if (canal == 0) offset = OFFSET_TC1;
  if (canal == 1) offset = OFFSET_TC2;
  if (canal == 2) offset = OFFSET_TC3;

  irms = irms - offset;
  return irms < 0 ? 0 : irms;
}

float calcularPotencia(float corrente) {
  return TENSAO_NOMINAL * corrente * FATOR_POTENCIA;
}

// ─────────────────────────────────────────────────────────────
void atualizarLCD(int canal, float corrente, float potencia) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TC"); lcd.print(canal);
  lcd.print(": "); lcd.print(corrente, 2); lcd.print("A");
  lcd.setCursor(12, 0);
  lcd.print((int)TENSAO_NOMINAL); lcd.print("V");
  lcd.setCursor(0, 1);
  lcd.print("P:"); lcd.print((int)potencia); lcd.print("W ");
  lcd.print(energia_kwh, 3); lcd.print("k");
}

// ─────────────────────────────────────────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT recebido] "); Serial.println(topic);
}

// ─────────────────────────────────────────────────────────────
void conectarWiFi() {
  Serial.println("\n\n=== BOOT ESP32 ===");

  Serial.println("[1] Iniciando I2C...");
  Wire.begin(21, 22);
  Serial.println("[1] I2C OK");

  Serial.println("[2] Iniciando LCD...");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Iniciando...");
  Serial.println("[2] LCD OK");

  Serial.println("[3] Iniciando ADS1115...");
  if (!ads.begin(0x48)) {
    Serial.println("[ERRO] ADS1115 nao encontrado!");
    lcd.clear(); lcd.print("ERRO: ADS1115");
    while (1) delay(1000);
  }
  ads.setGain(GAIN_SIXTEEN);  // ±4.096V — seguro para 2.5V AC com 3 voltas
  Serial.println("[3] ADS1115 OK — GAIN_ONE, addr 0x48");

  Serial.println("[4] Conectando WiFi...");
  lcd.clear(); lcd.print("Conectando WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int t = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print("."); t++;
    if (t > 20) { Serial.println("\n[ERRO] WiFi falhou!"); return; }
  }
  Serial.println("\n[4] WiFi OK — IP: " + WiFi.localIP().toString());
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("WiFi OK!");
  lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
  delay(2000);
  //lcd.setCursor(0,1); lcd.print(WiFi.macAddress());
  Serial.println(WiFi.macAddress());
  //delay(2000);
  for(int i = 17 ; i > -3; i--){
    lcd.clear();
  lcd.setCursor(0,0); lcd.print("Endereço MAC");
  lcd.setCursor(i,1); lcd.print(WiFi.macAddress());
  delay(750);}
  delay(2000);
}

// ─────────────────────────────────────────────────────────────
void reconectarMQTT() {
  while (!client.connected()) {
    Serial.println("[5] Conectando ao broker HiveMQ...");
    lcd.clear(); lcd.print("Conectando MQTT");

    String clientId = "ESP32_UNIFEOB_" + String(random(0xFFFF), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("[5] MQTT conectado!");
      client.publish(topico_status, "online");
      client.subscribe(topico_status);
      lcd.clear(); lcd.print("MQTT OK!");
      lcd.setCursor(0, 1); lcd.print("hivemq.com");
      delay(2000);
    } else {
      Serial.printf("[5] MQTT falhou rc=%d\n", client.state());
      lcd.clear(); lcd.print("Falha MQTT");
      lcd.setCursor(0, 1); lcd.print("rc="); lcd.print(client.state());
      delay(5000);
    }
  }
}

// ─────────────────────────────────────────────────────────────
void publicarMQTT(float cA, float cB, float cC, float pTotal) {
  char buf[12];

  dtostrf(cA,          4, 2, buf); client.publish(topico_faseA, buf);
  dtostrf(cB,          4, 2, buf); client.publish(topico_faseB, buf);
  dtostrf(cC,          4, 2, buf); client.publish(topico_faseC, buf);
  dtostrf(pTotal,      6, 2, buf); client.publish(topico_total, buf);
  dtostrf(energia_kwh, 6, 4, buf); client.publish(topico_kwh,   buf);

  Serial.println("──── Dados publicados no MQTT ────");
  Serial.printf("  FA:%.2fA  FB:%.2fA  FC:%.2fA\n", cA, cB, cC);
  Serial.printf("  Ptotal:%.1fW  kWh:%.5f\n", pTotal, energia_kwh);
  Serial.println("────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  conectarWiFi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  reconectarMQTT();

  ultimo_tempo = millis();
  ultimo_envio = millis();
  ultimo_swap  = millis();

  Serial.println("=== SETUP CONCLUIDO ===\n");
}

void loop() {
  if (!client.connected()) reconectarMQTT();
  client.loop();

  // 1. Leitura RMS dos 3 TCs via ADS1115
  float cA = lerCorrente(0)*100;  // A0 → TC1 - ADICIONADO *100 PARA AMPLIFICAR O VALOR FINAL DAS 3 FASES
  float cB = lerCorrente(1)*100;  // A1 → TC2
  float cC = lerCorrente(2)*100;  // A2 → TC3

  // 2. Potência por fase e total
  float pA     = calcularPotencia(cA);
  float pB     = calcularPotencia(cB);
  float pC     = calcularPotencia(cC);
  float pTotal = pA + pB + pC;

  // 3. kWh acumulado
  unsigned long agora = millis();
  float delta_h  = (agora - ultimo_tempo) / 3600000.0;
  energia_kwh   += (pTotal / 1000.0) * delta_h;
  ultimo_tempo   = agora;

  // 4. LCD — alterna canal a cada 2s
  if (agora - ultimo_swap >= 2000) {
    canal_display = (canal_display + 1) % 3;
    ultimo_swap = agora;
  }
  float correntes[] = {cA, cB, cC};
  float potencias[]  = {pA, pB, pC};
  atualizarLCD(canal_display + 1, correntes[canal_display], potencias[canal_display]);

  // 5. Serial monitor
  Serial.printf("TC1:%.3fA  TC2:%.3fA  TC3:%.3fA | P:%.1fW | kWh:%.5f\n",
                cA, cB, cC, pTotal, energia_kwh);

  // 6. Publica no intervalo configurado
  if (agora - ultimo_envio >= INTERVALO_ENVIO) {
    publicarMQTT(cA, cB, cC, pTotal);
    /*
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("MQTT enviado!");
    lcd.setCursor(0, 1); lcd.print(pTotal, 1); lcd.print("W ");
    lcd.print(energia_kwh, 3); lcd.print("k");
    //digitalWrite(LED_BUILTIN,HIGH);
    delay(1500);
    //digitalWrite(LED_BUILTIN,LOW);
    */

    ultimo_envio = agora;
  }

  delay(100);
}