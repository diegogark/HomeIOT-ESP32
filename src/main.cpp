/*
* O omando é realizado através de um inteiro. abaixo a tabela de significado
* 0 = sem comando / comando executado
* 1 = comando para ligar com verificação de estado
* 2 = comando para desligar com verificação de estado
* 3 = comando pulso para acionamento sem verificação de estado
* 
* Ao receber um comando do firebase, a tarefa de comando executa a ação determinada e retorna
* a informação que o cmando foi executado
*/

// Bibliotecas
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <Wifi.h>
#include <FirebaseESP32.h>
#include <time.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>

// Definições
#define FIREBASE_HOST "https://homeiot-87150.firebaseio.com/" // caminho do firebase
#define FIREBASE_AUTH "ChaveSecretaFirebase"
#define WIFI_SSID "NomeDaWIFI"
#define WIFI_PASSWORD "SenhaWIFI"
#define DHTPIN 23     // pino de dados do DHT11
#define DHTTYPE DHT11 // define o tipo de sensor, no caso DHT11
#define EXECUTADO 0   // sem comando ou comando executado
#define LIGAR 1       //
#define DESLIGAR 2    //
#define PULSAR 3      //

// Struct para comunicações entre tarefas

struct Gravar
{
  float sensorValor;
  float sensorValorMaximo;
  float sensorValorMinimo;
  float sensorValorMedio;
  String ID;
  String sensorUnidade;
  bool isComando;
  bool comandoExecutado;
  int comandoAcao;
};

struct Comando
{
  String nome;
  int acao;
};

// variaveis globais, precisam ser usadas com semaforo
UBaseType_t uxHighWaterMark;
// semaforos
SemaphoreHandle_t xSemafFirebase;
SemaphoreHandle_t xSemafTime;
// filas (Queues)
QueueHandle_t xFilaGravar;
QueueHandle_t xFilaComando;
// timer do freeRTOS
TimerHandle_t xTimer1m;
struct TempoLocal
{
  String totalEmSegundos;
  int hora;
  int minuto;
  String dataFormatada;
  String dataHistorico;
} vGlobalTempo;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -2;
const int daylightOffset_sec = -3600 * 3;

/* 
 * Base de tempo para as tarefas. Utilizei semaforo porque nãoconsegui fazer dar certo com queue.
 * deve ter um jeito mais elegante de contar o tempo sem a necessidade de uma variavel global
*/
//função para colocar 0 nos numeros
String zero(int num)
{
  if (num < 10)
  {
    return "0" + String(num);
  }
  return String(num);
}
static void Tempo1m(void *z)
{
  struct tm t;
  time_t tempoEmSegundos;
  String dataFormatada;
  String dataHistorico;
  if (getLocalTime(&t))
  {
    dataFormatada = zero(t.tm_mday) + "/" + zero(t.tm_mon + 1) + "/" +
                    zero(t.tm_year - 100) + " " + zero(t.tm_hour) + ":" + zero(t.tm_min);
    dataHistorico = zero(t.tm_year - 100) + zero(t.tm_mon + 1) + zero(t.tm_mday) + zero(t.tm_hour);
  }
  time(&tempoEmSegundos);
  String totalSegundos = String(tempoEmSegundos, DEC);
  Serial.println(totalSegundos);
  if (xSemaphoreTake(xSemafTime, pdMS_TO_TICKS(100)))
  {
    vGlobalTempo.minuto = t.tm_min;
    vGlobalTempo.hora = t.tm_hour;
    vGlobalTempo.dataFormatada = dataFormatada;
    vGlobalTempo.dataHistorico = dataHistorico;
    vGlobalTempo.totalEmSegundos = tempoEmSegundos;
    xSemaphoreGive(xSemafTime);
  }
  Serial.println(dataFormatada);
}
/* 
 * Abaixo estão as tarefas que interagem com o firebase.
 * Criei uma sinaleira para que o recurso não seja utilizado
 * por 2 tarefas ao mesmo tempo
*/
// tarefa que recebe os comandos do firebase
void taskRecebeComandoFirebase(void *arg)
{
  // variaveis locais
  Comando comando;
  FirebaseData fbDataComando;
  if (xSemaphoreTake(xSemafFirebase, pdMS_TO_TICKS(300))) // pede o semaforo para poder usar o firebase
  {
    if (!Firebase.beginStream(fbDataComando, "/RTS/COMANDOS"))
    {
      Serial.println("REASON: " + fbDataComando.errorReason());
    }
    xSemaphoreGive(xSemafFirebase);
  }
  // loop da tarefa
  while (1)
  {
    if (xSemaphoreTake(xSemafFirebase, pdMS_TO_TICKS(300))) // pede o semaforo para poder usar o firebase
    {
      if (!Firebase.readStream(fbDataComando))
      {
        Serial.println("erro na leitura de stream");
      }
      xSemaphoreGive(xSemafFirebase); // após o uso do firebase, libera o semaforo para outra tarefa poder usar
      if (fbDataComando.streamAvailable())
      {
        comando.nome = fbDataComando.dataPath();
        int inicio = comando.nome.indexOf("/") + 1;
        int fim = comando.nome.indexOf("/", inicio);
        comando.nome = comando.nome.substring(inicio, fim);
        if (fbDataComando.dataType() == "int")
        {
          comando.acao = fbDataComando.intData();
          xQueueSend(xFilaComando, &comando, pdMS_TO_TICKS(50));
        }
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        Serial.print("Memoria sobrando em taskRecebeComandoFirebase: ");
        Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(500)); // tempo liberado para outras tarefas neste nucleo
      }
    }
  }
}
// grava os sensores no firebase
void taskEnviaDadosFirebase(void *arg)
{
  FirebaseData fbDataGravar;
  FirebaseJson jsonSensores;
  FirebaseJson jSonHistorico;
  FirebaseJson jSonComando;
  Gravar sensor;
  TempoLocal tempoAtual;
  TempoLocal tempoPassado;

  while (1)
  {
    if (xQueueReceive(xFilaGravar, &sensor, pdMS_TO_TICKS(100)))
    {
      if (xSemaphoreTake(xSemafTime, pdMS_TO_TICKS(100)))
      {
        tempoAtual = vGlobalTempo;
        xSemaphoreGive(xSemafTime);
      }
      if (!sensor.isComando)
      {
        jsonSensores.set("/" + sensor.ID + "/sID", sensor.ID);
        jsonSensores.set("/" + sensor.ID + "/sTime", tempoAtual.dataFormatada);
        jsonSensores.set("/" + sensor.ID + "/sUnid", sensor.sensorUnidade);
        jsonSensores.set("/" + sensor.ID + "/sVMax", sensor.sensorValorMaximo);
        jsonSensores.set("/" + sensor.ID + "/sVMin", sensor.sensorValorMinimo);
        jsonSensores.set("/" + sensor.ID + "/sValor", sensor.sensorValor);
        jSonHistorico.set("/" + sensor.ID, sensor.sensorValorMedio);
        Serial.println(sensor.ID + " " + sensor.sensorValorMedio + sensor.sensorUnidade);
      }
      if (sensor.isComando)
      {
        jSonComando.set("/iComando", sensor.comandoAcao);
        jSonComando.set("/resposta", true);
        jSonComando.set("/sTime", tempoAtual.dataFormatada);
        if (xSemaphoreTake(xSemafFirebase, pdMS_TO_TICKS(300))) // pede o semaforo para poder usar o firebase
        {
          Firebase.updateNode(fbDataGravar, "/RTS/COMANDOS/" + sensor.ID, jSonComando);
          xSemaphoreGive(xSemafFirebase); // após o uso do firebase, libera o semaforo para outra tarefa poder usar
        }
      }
    }
    else
    {
      if (tempoAtual.minuto % 5 == 0 && tempoAtual.minuto != tempoPassado.minuto)
      {
        tempoPassado.minuto = tempoAtual.minuto;
        if (xSemaphoreTake(xSemafFirebase, pdMS_TO_TICKS(300))) // pede o semaforo para poder usar o firebase
        {
          Firebase.updateNode(fbDataGravar, "/RTS/MEDICAO", jsonSensores);
          xSemaphoreGive(xSemafFirebase); // após o uso do firebase, libera o semaforo para outra tarefa poder usar
        }
      }
      if (tempoAtual.hora != tempoPassado.hora)
      {
        tempoPassado.hora = tempoAtual.hora;
        //long totalEmSegundos = tempoAtual.totalEmSegundos;
        if (xSemaphoreTake(xSemafFirebase, pdMS_TO_TICKS(300))) // pede o semaforo para poder usar o firebase
        {
          Firebase.updateNode(fbDataGravar, "/DB/" + tempoAtual.totalEmSegundos, jSonHistorico);
          xSemaphoreGive(xSemafFirebase); // após o uso do firebase, libera o semaforo para outra tarefa poder usar
        }
      }
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      Serial.print("Memoria sobrando em taskEnviaDadosFirebase: ");
      Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
      vTaskDelay(pdMS_TO_TICKS(5000)); // tempo liberado para outras tarefas neste nucleo
    }
  }
}
// - As tarefas de interação com o firebase estão acima desta linha
/*
 * Neste campo estão as tarefas que realizam leituras dos sensores
*/
// Leitura do sensor de temperatura DHT11 - envia leitura pela queue a cada 1 minuto e le o sensor a cada 10s
void taskSensorDHT11(void *arg)
{
  DHT dht(23, DHT11); //Pino de ligação do DHT, Tipo do sensor
  dht.begin();
  int contador = 0;
  Gravar sTemperatura;
  Gravar sUmidade;
  Gravar sSensacao;
  sTemperatura.ID = "DHTtemperatura";
  sTemperatura.sensorUnidade = "°C";
  sTemperatura.sensorValorMinimo = 100;
  sTemperatura.isComando = false;
  sUmidade.ID = "DHTumidade";
  sUmidade.sensorUnidade = "%";
  sUmidade.sensorValorMinimo = 100;
  sUmidade.isComando = false;
  sSensacao.ID = "DHTsensacao";
  sSensacao.sensorUnidade = "°C";
  sSensacao.sensorValorMinimo = 100;
  sSensacao.isComando = false;
  int horaReset = 1;
  bool isReseted = false;
  while (1)
  {
    bool erroLeituraSensor = false;
    float buff = dht.readTemperature();
    if (buff < 100 && buff > -10) // quando ocorre uma leitura errada no DHT, o valor de leitura é fora desses limites
    {
      sTemperatura.sensorValor = buff;
      if (sTemperatura.sensorValorMedio == 0)
      {
        sTemperatura.sensorValorMedio = sTemperatura.sensorValor;
      }
      else
      {
        sTemperatura.sensorValorMedio = ((sTemperatura.sensorValorMedio * 29) + sTemperatura.sensorValor) / 30;
      }
      if (sTemperatura.sensorValorMaximo < sTemperatura.sensorValor)
      {
        sTemperatura.sensorValorMaximo = sTemperatura.sensorValor;
      }
      if (sTemperatura.sensorValorMinimo > sTemperatura.sensorValor)
      {
        sTemperatura.sensorValorMinimo = sTemperatura.sensorValor;
      }
    }
    else
    {
      erroLeituraSensor = true;
    }
    buff = dht.readHumidity();
    if (buff < 110 && buff > -1) // quando ocorre uma leitura errada no DHT, o valor de leitura é fora desses limites
    {
      sUmidade.sensorValor = buff;
      if (sUmidade.sensorValorMedio == 0)
      {
        sUmidade.sensorValorMedio = buff;
      }
      else
      {
        sUmidade.sensorValorMedio = ((sUmidade.sensorValorMedio * 29) + buff) / 30;
      }
      if (sUmidade.sensorValorMaximo < sUmidade.sensorValor)
      {
        sUmidade.sensorValorMaximo = sUmidade.sensorValor;
      }
      if (sUmidade.sensorValorMinimo > sUmidade.sensorValor)
      {
        sUmidade.sensorValorMinimo = sUmidade.sensorValor;
      }
    }
    else
    {
      erroLeituraSensor = true;
    }

    if (!erroLeituraSensor) // se não der erro nas leituras de sensor, calcula a sensação termica
    {
      sSensacao.sensorValor = dht.computeHeatIndex(sTemperatura.sensorValor, sUmidade.sensorValor, false);
      if (sSensacao.sensorValorMedio == 0)
      {
        sSensacao.sensorValorMedio = sSensacao.sensorValor;
      }
      else
      {
        sSensacao.sensorValorMedio = ((sSensacao.sensorValorMedio * 29) + sSensacao.sensorValor) / 30;
      }
      if (sSensacao.sensorValorMaximo < sSensacao.sensorValor)
      {
        sSensacao.sensorValorMaximo = sSensacao.sensorValor;
      }
      if (sSensacao.sensorValorMinimo > sSensacao.sensorValor)
      {
        sSensacao.sensorValorMinimo = sSensacao.sensorValor;
      }
    }
    if (contador > 6)
    {
      xQueueSend(xFilaGravar, &sTemperatura, pdMS_TO_TICKS(1000));
      xQueueSend(xFilaGravar, &sUmidade, pdMS_TO_TICKS(1000));
      xQueueSend(xFilaGravar, &sSensacao, pdMS_TO_TICKS(1000));
      contador = 0;
      if (xSemaphoreTake(xSemafTime, pdMS_TO_TICKS(100)))
      {
        horaReset = vGlobalTempo.hora;
        xSemaphoreGive(xSemafTime);
      }
      if (horaReset == 0)
      {
        if (!isReseted)
        {
          sTemperatura.sensorValorMaximo = 0;
          sTemperatura.sensorValorMinimo = 100;
          sUmidade.sensorValorMaximo = 0;
          sUmidade.sensorValorMinimo = 100;
          sSensacao.sensorValorMaximo = 0;
          sSensacao.sensorValorMinimo = 100;
          isReseted = true;
        }
      }
      else
      {
        isReseted = false;
      }
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      Serial.print("Memoria DHT11: ");
      Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
    }
    contador++;
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
// Leitura do sensor de temperatura BMP180 - envia leitura pela queue a cada 1 minuto e le o sensor a cada 10s
void taskSensorBMP180(void *arg)
{
  Adafruit_BMP085 bmp;
  if (!bmp.begin()) // está conactado na porta padrão. para modificar usar bmp.begin(SCL, SDA)
    Serial.println("Could not find a valid BMP180 sensor, check wiring!");
  int contador = 0;
  Gravar sTemperatura;
  Gravar sPressao;
  sTemperatura.ID = "BMPtemperatura";
  sTemperatura.sensorUnidade = "°C";
  sTemperatura.sensorValorMinimo = 100;
  sTemperatura.isComando = false;
  sPressao.ID = "BMPpressao";
  sPressao.sensorUnidade = "hPa";
  sPressao.sensorValorMinimo = 200000;
  sPressao.isComando = false;
  int horaReset = 1;
  bool isReseted = false;
  while (1)
  {
    float buff = bmp.readTemperature();
    if (buff < 100 && buff > -100)
    {
      sTemperatura.sensorValor = buff;
      if (sTemperatura.sensorValorMedio == 0)
      {
        sTemperatura.sensorValorMedio = buff;
      }
      else
      {
        sTemperatura.sensorValorMedio = ((sTemperatura.sensorValorMedio * 29) + buff) / 30;
      }
      if (sTemperatura.sensorValorMaximo < sTemperatura.sensorValor)
      {
        sTemperatura.sensorValorMaximo = sTemperatura.sensorValor;
      }
      if (sTemperatura.sensorValorMinimo > sTemperatura.sensorValor)
      {
        sTemperatura.sensorValorMinimo = sTemperatura.sensorValor;
      }
    }
    buff = bmp.readPressure();
    buff = buff / 100;
    if (buff < 200000 && buff > 1)
    {
      sPressao.sensorValor = buff;
      if (sPressao.sensorValorMedio == 0)
      {
        sPressao.sensorValorMedio = buff;
      }
      else
      {
        sPressao.sensorValorMedio = ((sPressao.sensorValorMedio * 29) + buff) / 30;
      }
      if (sPressao.sensorValorMaximo < sPressao.sensorValor)
      {
        sPressao.sensorValorMaximo = sPressao.sensorValor;
      }
      if (sPressao.sensorValorMinimo > sPressao.sensorValor)
      {
        sPressao.sensorValorMinimo = sPressao.sensorValor;
      }
    }
    if (contador > 6)
    {
      xQueueSend(xFilaGravar, &sTemperatura, pdMS_TO_TICKS(1000));
      xQueueSend(xFilaGravar, &sPressao, pdMS_TO_TICKS(1000));
      contador = 0;
      if (xSemaphoreTake(xSemafTime, pdMS_TO_TICKS(100)))
      {
        horaReset = vGlobalTempo.hora;
        xSemaphoreGive(xSemafTime);
      }
      if (horaReset == 0)
      {
        if (!isReseted)
        {
          sTemperatura.sensorValorMaximo = 0;
          sTemperatura.sensorValorMinimo = 100;
          sPressao.sensorValorMaximo = 0;
          sPressao.sensorValorMinimo = 200000;
          isReseted = true;
        }
      }
      else
      {
        isReseted = false;
      }
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      Serial.print("Memoria BMP180: ");
      Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
    }
    contador++;
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
/* 
 * tarefas de controle via APP. Estas tarefas são executadas somente quando
 * taskRecebeComandoFirebase recebe um comando. Estas tarefas são disparadas
 * através da Queue (filas). Enquanto a xFilaComando está sem dados, essas
 * tarefas ficam bloqueadas
*/
// tarefa que executa o comando do portão eletronico
void taskComandoPortao(void *arg)
{
  Comando comando;
  Gravar gravar;
  gravar.comandoAcao = EXECUTADO;
  gravar.ID = "Portao";
  gravar.isComando = true;
  while (1)
  {
    if (xQueuePeek(xFilaComando, &comando, portMAX_DELAY))
    {
      if (comando.nome == gravar.ID)
      {
        xQueueReceive(xFilaComando, &comando, 0);
        if (comando.acao == PULSAR)
        {
          digitalWrite(5, HIGH);
          Serial.println("Chegou o comando do Portão");
          vTaskDelay(pdMS_TO_TICKS(500));
          digitalWrite(5, LOW);
          xQueueSend(xFilaGravar, &gravar, pdMS_TO_TICKS(100));
        }
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(100)); // tempo liberado para outras tarefas neste nucleo
      }
    }
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("comando do Portão: ");
    Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
  }
}
// tarefa que executa o acionamento do alarme
void taskComandoAlarme(void *arg)
{
  Comando comando;
  Gravar gravar;
  gravar.ID = "Alarme";
  gravar.isComando = true;
  while (1)
  {
    if (xQueuePeek(xFilaComando, &comando, portMAX_DELAY))
    {
      if (comando.nome == gravar.ID)
      {
        xQueueReceive(xFilaComando, &comando, 0);
        if (comando.acao == LIGAR)
        {
          digitalWrite(18, HIGH);
          gravar.comandoAcao = LIGAR;
          xQueueSend(xFilaGravar, &gravar, pdMS_TO_TICKS(100));
        }
        if (comando.acao == DESLIGAR)
        {
          digitalWrite(18, LOW);
          gravar.comandoAcao = DESLIGAR;
          xQueueSend(xFilaGravar, &gravar, pdMS_TO_TICKS(100));
        }
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(100)); // tempo liberado para outras tarefas neste nucleo
      }
    }
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("comando do Alarme: ");
    Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
  }
}
// Tarefa que executa o disparo do alarme
void taskComandoDispararAlarme(void *arg)
{
  Comando comando;
  Gravar gravar;
  gravar.ID = "DispararAlarme";
  gravar.isComando = true;
  while (1)
  {
    if (xQueuePeek(xFilaComando, &comando, portMAX_DELAY))
    {
      if (comando.nome == gravar.ID)
      {
        xQueueReceive(xFilaComando, &comando, 0);
        if (comando.acao == LIGAR)
        {
          digitalWrite(19, HIGH);
          gravar.comandoAcao = LIGAR;
          xQueueSend(xFilaGravar, &gravar, pdMS_TO_TICKS(100));
        }
        if (comando.acao == DESLIGAR)
        {
          digitalWrite(19, LOW);
          gravar.comandoAcao = DESLIGAR;
          xQueueSend(xFilaGravar, &gravar, pdMS_TO_TICKS(100));
        }
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(100)); // tempo liberado para outras tarefas neste nucleo
      }
    }
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("comando Disparar Alarme: ");
    Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
  }
}
// Se todos os comandos estiverem configurados, esta tarefa nunca será executada
void taskComandoRecebido(void *arg)
{
  Comando comando;
  while (1)
  {
    xQueueReceive(xFilaComando, &comando, portMAX_DELAY);
    Serial.println("Comando não identificado");
  }
}
// - colocar as tarefas de comando acima desta linha - Organização de código
void setup()
{
  Serial.begin(115200);
  pinMode(5, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(19, OUTPUT);
  while (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    Serial.print(".");
    delay(10000);
  }
  Serial.println(WiFi.localIP());
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // carrega as informações e configura o firabase
  // Area de configuração dos comandos
  // abrir o portão eletronico
  FirebaseJson jSon;
  FirebaseData firebaseData;
  jSon.set("/Portao/iComando", 0);
  jSon.set("/Portao/resposta", true);
  jSon.set("/Portao/sID", "Portao");
  jSon.set("/Portao/sTime", "00/00/00");
  jSon.set("/Portao/comRetorno", false);
  // acionar o alarme
  jSon.set("/Alarme/iComando", 0);
  jSon.set("/Alarme/resposta", true);
  jSon.set("/Alarme/sID", "Alarme");
  jSon.set("/Alarme/sTime", "00/00/00");
  jSon.set("/Alarme/comRetorno", true);
  // Disparar o alarme
  jSon.set("/DispararAlarme/iComando", 0);
  jSon.set("/DispararAlarme/resposta", true);
  jSon.set("/DispararAlarme/sID", "DispararAlarme");
  jSon.set("/DispararAlarme/sTime", "00/00/00");
  jSon.set("/DispararAlarme/comRetorno", true);
  Firebase.updateNode(firebaseData, "/RTS/COMANDOS", jSon); // push cria id única, set e setJson deleta tudo, update não exclui o do nivel do jSon
  jSon.clear();
  //----------------------------------------------
  xSemafTime = xSemaphoreCreateMutex();
  xSemafFirebase = xSemaphoreCreateMutex();
  xFilaComando = xQueueCreate(5, sizeof(Comando));
  xFilaGravar = xQueueCreate(10, sizeof(Gravar));
  if (xSemafFirebase == NULL && xSemafTime == NULL)
  {
    Serial.println("não foi posivel criar o semaforo");
  }
  else
  {
    xTaskCreatePinnedToCore(taskRecebeComandoFirebase, "RecebeFB", 8192, NULL, 5, NULL, APP_CPU_NUM); // max até o momento 3796 normal 1988
    xTaskCreatePinnedToCore(taskEnviaDadosFirebase, "EnviaFB", 16384, NULL, 4, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(taskComandoPortao, "Portão", 2048, NULL, 5, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(taskComandoAlarme, "Alarme", 2048, NULL, 5, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(taskComandoDispararAlarme, "Disparo", 2048, NULL, 5, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(taskComandoRecebido, "Generico", 2048, NULL, 1, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(taskSensorDHT11, "DHT11", 2048, NULL, 3, NULL, PRO_CPU_NUM);
    xTaskCreatePinnedToCore(taskSensorBMP180, "BMP180", 2048, NULL, 3, NULL, PRO_CPU_NUM);
  }
  // cria e inicia o timer do RTOS
  xTimer1m = xTimerCreate("tempo10s", pdMS_TO_TICKS(60000), pdTRUE, 0, Tempo1m);
  xTimerStart(xTimer1m, pdMS_TO_TICKS(100));
}

void loop()
{
  // put your main code here, to run repeatedly:
}
