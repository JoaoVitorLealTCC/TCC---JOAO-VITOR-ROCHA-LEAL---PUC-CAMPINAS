#include "EmonLib.h"
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>

// LCD setup
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Constants for calibration
const float vCalibration = 192.0;
const float currCalibration = 2.14;

const int numReadings = 2;  // Número de leituras para a média
float readings[numReadings]; // Array para armazenar as leituras
int readIndex = 0;           // Índice atual para o array de leituras
float total = 0.0;           // Soma de todas as leituras
float average = 0.0;         // Média das leituras

// Filtro de média móvel para corrente
const int numReadingsCurr = 2;  // Número de leituras para a corrente
float readingsCurr[numReadingsCurr]; // Array para armazenar as leituras da corrente
int readIndexCurr = 0;           // Índice atual para o array de leituras da corrente
float totalCurr = 0.0;           // Soma de todas as leituras da corrente
float averageCurr = 0.0;         // Média das leituras da corrente

// Variáveis de calibração para diferentes tensões
float fator_calibracao_120V = 0.11283;
float fator_calibracao_220V = 0.18541;
float fator_calibracao_atual = 1.0;  // Variável que armazenará o fator de calibração atual
float kWhPhantom = 0.0; // Nova variável fantasma para cálculos
bool ignoreReadings = true;  // Variável para ignorar leituras iniciais
unsigned long startMillis; // Para controlar o tempo de inicialização

float tensao_pico = 0.0; // Variável para armazenar a tensão de pico

// EnergyMonitor instance
EnergyMonitor emon;

// Timer for regular updates
unsigned long previousMillis = 0;
const long interval = 5000; // 5 seconds

// Variables for ZMPT101B
double menor_valor = 0;

// Variables for energy calculation
float kWh = 0.0;
float SALVAR = 1.0; // Default value for division
float kWhDiv = 0.0; // New variable to hold the result of kWh / SALVAR
unsigned long lastMillis = millis();

// Variaveis para calculo de conta de luz
float TUSD = 0.5;           // Tarifa de Uso do Sistema de Distribuição (valor aproximado, ajustar conforme sua distribuidora)
float TE = 0.4;             // Tarifa de Energia (valor aproximado, ajustar conforme sua distribuidora)
float Custo_Bandeira = 0.03; // Custo da bandeira tarifária (valor aproximado, ajustar conforme bandeira atual)
float ICMS = 0.18;          // Percentual de ICMS (valor aproximado)
float PIS = 0.0165;         // Percentual de PIS (valor aproximado)
float COFINS = 0.076;       // Percentual de COFINS (valor aproximado)
float Contribuicao = 5.0;   // Contribuição para iluminação pública ou outro custo fixo em R$



// EEPROM addresses for each variable
const int addrKWh = 12;
const int addrSALVAR = 20; // Address for SALVAR
const int addrTUSD = 20; // Address for TUSD
const int addrTE = 20; // Address for TE
const int addrCusto_Bandeira = 20; // Address for Custo_Bandeira
const int addrICMS = 20; // Address for ICMS
const int addrPIS = 20; // Address for PIS
const int addrCOFINS = 20; // Address for COFINS
const int addrContribuicao = 20; // Address for Contribuicao
const int addrkWhDiv = sizeof(float); // Endereço inicial para kWhDiv, após o espaço de kWh

// WiFi and MQTT setup
const char* ssid = "JOAO";
const char* password = "jv24092002";
const char* mqtt_server = "broker.mqtt-dashboard.com";

WiFiClient espClient;
PubSubClient client(espClient);

// Button pin
const int buttonPin = 12;
int lastButtonState = HIGH;

// Function prototypes
void saveEnergyDataToEEPROM();
void readEnergyDataFromEEPROM();
void readSALVARFromEEPROM();
void readEnergyDataFromEEPROM();
void readTUSDFromEEPROM();
void readTEFromEEPROM();
void readCusto_BandeiraFromEEPROM();
void readPISFromEEPROM();
void readICMSFromEEPROM();
void readCOFINSFromEEPROM();
void readContribuicaoFromEEPROM();
void updateLCD();
void setup_wifi();
void reconnect();
void checkButtonPress();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Constants for calibration
void setup() {
    Serial.begin(115200);
    ignoreReadings = true; // Iniciar ignorando leituras

    // Inicialize o LCD
    lcd.init();
    lcd.backlight();

    // Initialize EEPROM
    EEPROM.begin(64);

    // Read the stored data from EEPROM
    readEnergyDataFromEEPROM();
    readSALVARFromEEPROM();
    readTUSDFromEEPROM();
    readTEFromEEPROM();
    readCusto_BandeiraFromEEPROM();
    readICMSFromEEPROM();
    readPISFromEEPROM();
    readCOFINSFromEEPROM();
    readContribuicaoFromEEPROM();

    // Setup voltage and current inputs
    emon.voltage(35, vCalibration, 1.7); // Calibração da tensão
    emon.current(34, currCalibration);   // Calibração da corrente com valor ajustado

    // Setup WiFi and MQTT
    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);

    // Initialize button input
    pinMode(buttonPin, INPUT_PULLUP);

    delay(1000);  // Delay para estabilizar

    startMillis = millis(); // Inicia o cronômetro para ignorar leituras nos primeiros 10 segundos

    // Inicializa o array de leituras da tensão com zeros
    for (int i = 0; i < numReadings; i++) {
        readings[i] = 0.0;
    }

    // Inicializa o array de leituras da corrente com zeros
    for (int i = 0; i < numReadingsCurr; i++) {
        readingsCurr[i] = 0.0;
    }
}
void loop() {
    unsigned long currentMillis = millis();

    // Reconnect to MQTT broker if necessary
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    checkButtonPress();  // Check if the button is pressed to reset kWh

    // Execute the update every 5 seconds
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

// Calcula a tensão, corrente e potência usando EmonLib
emon.calcVI(20, 2000); // Calcula todos os valores (Vrms, Irms, Power)

// Ignora as leituras nos primeiros 100 segundos
if (millis() - startMillis < 100000) {
    return; // Retorna sem calcular para ignorar as leituras
}

// Aqui você adiciona a verificação da corrente
if (emon.Irms < 0.05) {  
    emon.Vrms = 0.0;  // Se a corrente for quase zero, define a tensão como 0V
    emon.Irms = 0.0;  // Também ajusta a corrente para 0
    emon.apparentPower = 0.0;  // Ajusta a potência para 0

    // Limpa as leituras para que o filtro não afete as próximas leituras
    for (int i = 0; i < numReadings; i++) {
        readings[i] = 0.0; // Zera todas as leituras
    }
    total = 0.0; // Reseta o total
    readIndex = 0; // Reseta o índice
    average = 0.0; // Reseta a média
    return; // Sai da função
}

// Aqui você adiciona a lógica de filtro de média móvel
total = total - readings[readIndex];   // Subtrai a leitura mais antiga do total
readings[readIndex] = emon.Vrms;       // Substitui pela nova leitura
total = total + readings[readIndex]; // **CORRIGIDO**
readIndex = (readIndex + 1) % numReadings; // Move o índice, e o reseta no final do array

totalCurr = totalCurr - readingsCurr[readIndexCurr];  // Subtrai a leitura mais antiga
readingsCurr[readIndexCurr] = emon.Irms;  // Substitui pela nova leitura de corrente
totalCurr = totalCurr + readingsCurr[readIndexCurr];  // Adiciona a nova leitura
readIndexCurr = (readIndexCurr + 1) % numReadingsCurr;  // Atualiza o índice
averageCurr = totalCurr / numReadingsCurr;  // Calcula a média das leituras de corrente

average = total / numReadings;  // Calcula a média das leituras
        // Substitua a leitura direta por average
        emon.Vrms = average;

        // Aqui você adiciona a verificação da corrente
        if (emon.Irms < 0.05) {  
            emon.Vrms = 0.0;  // Se a corrente for quase zero, define a tensão como 0V
            emon.Irms = 0.0;  // Também ajusta a corrente para 0
            emon.apparentPower = 0.0;  // Ajusta a potência para 0
        }

        // Verificações para ajustar leituras abaixo de um certo limite
        if (emon.Vrms < 60.0) {
            emon.Vrms = 0.0;  // Se for menor que 60V, ajusta para zero
        }

        if (emon.Irms < 0.50) {
            emon.Irms = 0.0;  // Se for menor que 0.10A, ajusta para zero
        }

        // Verifique se Irms ou Vrms é igual a 0.0
        if (emon.Irms == 0.0 || emon.Vrms == 0.0) {
            emon.apparentPower = 0.0;  // Se um dos valores for 0, ajusta apparentPower para zero
        } else {
            emon.apparentPower = (emon.Vrms * emon.Irms)*2.14; // Calcule apparentPower normalmente
        }

        // Calcula a energia consumida em kWh
        unsigned long elapsedMillis = millis() - lastMillis; // Tempo decorrido desde o último cálculo
        kWh += (emon.apparentPower * fator_calibracao_atual * (elapsedMillis / 3600000.0)); // Converte milissegundos para horas
        lastMillis = millis(); // Atualiza lastMillis para o próximo cálculo

        // Salva os valores atualizados na EEPROM
        saveEnergyDataToEEPROM();

        // Atualiza o LCD com os novos valores
        updateLCD();

        // Envia os dados via MQTT
        char kWhStr[10];
        dtostrf(kWh, 6, 3, kWhStr); // Converte o float para string
        client.publish("outTopic/kWh", kWhStr);

        // Calcula kWh dividido por SALVAR e publica
        if (SALVAR != 0) {
        float custoEnergia = kWh * (TUSD + TE);
        float custoBandeira = (kWh / 100.0) * Custo_Bandeira;
        float custoComImpostos = custoEnergia * (1 + ICMS + PIS + COFINS);
        float contaDeLuz = (custoComImpostos + custoBandeira) + Contribuicao;
        kWhDiv = contaDeLuz;
        char kWhDivStr[10];
        dtostrf(kWhDiv, 6, 3, kWhDivStr); // Converte o resultado para string
        client.publish("outTopic/kWhDiv", kWhDivStr);
        }
    }
}

void updateLCD()
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(emon.Vrms, 0);  // Use diretamente o valor de Vrms
    lcd.print("V VL:R$");
    lcd.print(kWhDiv, 2);
    
    lcd.setCursor(0, 1);
    lcd.print("C: ");
    lcd.print(emon.Irms * currCalibration, 2); // Aplica a calibração
    lcd.print("A");
  
    lcd.setCursor(0, 2);
    lcd.print("Pw:");
    lcd.print(emon.apparentPower, 2);
    lcd.print("W:");

    lcd.setCursor(0, 3);
    lcd.print("kWh: ");
    lcd.print(kWh, 2);
    lcd.print(" kWh");
}

void readEnergyDataFromEEPROM()
{
  // Read the stored kWh value from EEPROM
  EEPROM.get(addrKWh, kWh);

  // Check if the read value is a valid float. If not, initialize it to zero
  if (isnan(kWh))
  {
  
  }
}

void readSALVARFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrSALVAR, SALVAR);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(SALVAR) || SALVAR == 0.0)
  {
    SALVAR = 1.0;
    saveSALVARToEEPROM(); // Save initialized value to EEPROM
  }
}

void readTUSDFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrTUSD, TUSD);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(TUSD) || TUSD == 0.0)
  {
    TUSD = 0.5;
    saveTUSDToEEPROM(); // Save TUSD initialized value to EEPROM
  }
}

void readTEFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrTE, TE);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(TE) || TE == 0.0)
  {
    TE = 0.4;
    saveTEToEEPROM(); // Save TE initialized value to EEPROM
  }
}

void readCusto_BandeiraFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrCusto_Bandeira, Custo_Bandeira);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(Custo_Bandeira) || Custo_Bandeira == 0.0)
  {
    Custo_Bandeira = 0.03;
    saveCusto_BandeiraToEEPROM(); // Save Custo_Bandeira initialized value to EEPROM
  }
}

void readICMSFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrICMS, ICMS);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(ICMS) || ICMS == 0.0)
  {
    ICMS = 0.18;
    saveICMSToEEPROM(); // Save ICMS initialized value to EEPROM
  }
}

void readPISFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrPIS, PIS);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(PIS) || PIS == 0.0)
  {
    ICMS = 0.0165;
    savePISToEEPROM(); // Save PIS initialized value to EEPROM
  }
}

void readCOFINSFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrCOFINS, COFINS);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(COFINS) || COFINS == 0.0)
  {
    COFINS = 0.076;
    saveCOFINSToEEPROM(); // Save COFINS initialized value to EEPROM
  }
}

void readContribuicaoFromEEPROM()
{
  // Read the stored persistent variable from EEPROM
  EEPROM.get(addrContribuicao, Contribuicao);

  // Check if the read value is a valid float. If not, initialize it to 1 to avoid division by 0
  if (isnan(Contribuicao) || Contribuicao == 0.0)
  {
    Contribuicao = 5.0;
    saveContribuicaoToEEPROM(); // Save Contribuicao initialized value to EEPROM
  }
}


void saveEnergyDataToEEPROM()
{
  // Write the current kWh value to EEPROM
  EEPROM.put(addrKWh, kWh);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveSALVARToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrSALVAR, SALVAR);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveTUSDToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrTUSD, TUSD);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveTEToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrTE, TE);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveCusto_BandeiraToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrCusto_Bandeira, Custo_Bandeira);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveICMSToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrICMS, ICMS);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void savePISToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrPIS, PIS);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveCOFINSToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrCOFINS, COFINS);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void saveContribuicaoToEEPROM()
{
  // Write the persistent variable value to EEPROM
  EEPROM.put(addrContribuicao, Contribuicao);
  // Commit changes to EEPROM
  EEPROM.commit();
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Tenta reconectar ao MQTT broker
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("inTopic");
      client.subscribe("inTopic/reset");  // Subscribe to the topic for RESET command
      client.subscribe("inTopic/salvar");  // Subscribe to the topic for SALVAR command
      client.subscribe("inTopic/TUSD");  // Subscribe to the topic for TUSD command
      client.subscribe("inTopic/TE");  // Subscribe to the topic for TE command
      client.subscribe("inTopic/Custo_Bandeira");  // Subscribe to the topic for Custo_Bandeira command
      client.subscribe("inTopic/ICMS");  // Subscribe to the topic for ICMS command
      client.subscribe("inTopic/PIS");  // Subscribe to the topic for PIS command
      client.subscribe("inTopic/COFINS");  // Subscribe to the topic for COFINS command
      client.subscribe("inTopic/Contribuicao");  // Subscribe to the topic for Contribuicao command
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


void checkButtonPress() {
  // Read the state of the button
  int buttonState = digitalRead(buttonPin);

  // If the button is pressed (state goes from HIGH to LOW)
  if (buttonState == LOW && lastButtonState == HIGH) {
    Serial.println("Button pressed! Resetting kWh to 0.");
    kWh = 0.0;  // Reset the kWh value to zero
    saveEnergyDataToEEPROM();  // Save the reset value to EEPROM
    updateLCD();  // Update the LCD to reflect the reset value
  }

  // Save the current state as the last state for the next loop
  lastButtonState = buttonState;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Converte o payload para uma string
  String message = String((char*)payload).substring(0, length);

  // Faz o parse da mensagem recebida para um float
  float incomingValue = message.toFloat();

  // Handle inTopic
  if (String(topic) == "inTopic") {
    if (incomingValue > 0) {
      kWhPhantom += incomingValue;  // Atualiza kWhPhantom com o valor recebido
      Serial.print("Added to kWhPhantom: ");
      Serial.println(incomingValue);
      saveEnergyDataToEEPROM();  // Salva o novo valor de kWhPhantom no EEPROM
      updateLCD();  // Atualiza o LCD para refletir o novo valor
    }
  }


  // Handle inTopic/TUSD
  else if (String(topic) == "inTopic/TUSD") {
    TUSD = incomingValue;  // Atualiza a variável TUSD
    Serial.print("Updated TUSD: ");
    Serial.println(TUSD);
    saveTUSDToEEPROM();  // Salva a nova variável TUSD no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/TE
  else if (String(topic) == "inTopic/TE") {
    TE = incomingValue;  // Atualiza a variável TE
    Serial.print("Updated TE: ");
    Serial.println(TE);
    saveTEToEEPROM();  // Salva a nova variável TE no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/Custo_Bandeira
  else if (String(topic) == "inTopic/Custo_Bandeira") {
    Custo_Bandeira = incomingValue;  // Atualiza a variável Custo_Bandeira
    Serial.print("Updated Custo_Bandeira: ");
    Serial.println(Custo_Bandeira);
    saveCusto_BandeiraToEEPROM();  // Salva a nova variável Custo_Bandeira no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/ICMS
  else if (String(topic) == "inTopic/ICMS") {
    ICMS = incomingValue;  // Atualiza a variável ICMS
    Serial.print("Updated ICMS: ");
    Serial.println(ICMS);
    saveICMSToEEPROM();  // Salva a nova variável ICMS no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/PIS
  else if (String(topic) == "inTopic/PIS") {
    PIS = incomingValue;  // Atualiza a variável PIS
    Serial.print("Updated PIS: ");
    Serial.println(PIS);
    savePISToEEPROM();  // Salva a nova variável PIS no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/COFINS
  else if (String(topic) == "inTopic/COFINS") {
    COFINS = incomingValue;  // Atualiza a variável COFINS
    Serial.print("Updated COFINS: ");
    Serial.println(COFINS);
    saveCOFINSToEEPROM();  // Salva a nova variável COFINS no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/Contribuicao
  else if (String(topic) == "inTopic/Contribuicao") {
    Contribuicao = incomingValue;  // Atualiza a variável Contribuicao
    Serial.print("Updated Contribuicao: ");
    Serial.println(Contribuicao);
    saveContribuicaoToEEPROM();  // Salva a nova variável Contribuicao no EEPROM
    updateLCD();  // Atualiza o LCD para refletir o novo valor
  }

  // Handle inTopic/reset for RESET command
  else if (String(topic) == "inTopic/reset") {
    if (message == "RESET") {
      kWh = 0.0;  // Reseta kWh para zero
      Serial.println("kWh reset to 0 via MQTT command");
      saveEnergyDataToEEPROM();  // Salva o valor resetado de kWh no EEPROM
      updateLCD();  // Atualiza o LCD para refletir o valor resetado
    }
  }

  // Handle inTopic/salvar for SALVAR command
  else if (String(topic) == "inTopic/salvar") {
    if (message == "SALVAR") {
      // Envia os valores para os tópicos permanentes
      char kWhPermanStr[10];
      dtostrf(kWh, 6, 3, kWhPermanStr);
      client.publish("outTopic/kWhperman", kWhPermanStr);

      char kWhDivPermanStr[10];
      dtostrf(kWhDiv, 6, 3, kWhDivPermanStr);
      client.publish("outTopic/kWhDivperman", kWhDivPermanStr);
    }
  }
} // Fecha a função mqttCallback
