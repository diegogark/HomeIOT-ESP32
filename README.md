# Implementação de uma central IOT com o ESP32


 Neste repósitório você encontrará um exemplo de código de programação escrito em C para implementação de uma central de monitoramento e controle residencial.
O código utiliza o Firebase para armazenamento de informações e recepção de comandos. Ele foi escrito em "C" e faz uso do sistema operacional FreeRTOS disponivel no ESP32.

Uma versão do aplicativo para monitoramento e controle via celular pode ser encontrada no link abaixo (apenas android).
* [APP de monitoramento para android]( https://github.com/diegogark/HomeIOT-APP)

## Dispositivos Testados

* ESP32 DEVKIT V1

## Funcionamento do código

 O FreeRTOS é um sistema operacional em tempo real para microcontroladores. Com ele podemos executar varias tarefas em paralelo deixando o FreeRTOS responsável pelo seu gerenciamento. A partir desta idéia, o programa do ESP32 foi separado nas tarefas abaixo:

 * Tarefa para receber os comandos do Firebase
 * Tarefa para enviar comandos para o Firebase
 * Tarefas de leituras de sensores
 * Tarefas de execução de comandos
 
 A separação das atividades em tarefas permite trabalhar em uma atividade específica do programa facilitando a manutenção e aprimoramentos. Uma tarefa se comporta de forma parecida com o conjunto "Setup" e "Loop" que usamos na IDE do arduino. Caso tenha curiosidade em entender como funciona uma task (tarefa), tem esse excelente tutorial em portugues do Felipe Neves disponivel no site [Embarcados](https://www.embarcados.com.br/esp32-lidando-com-multiprocessamento-parte-ii/) ou o manual do FreeRTOS disponivel [aqui](https://www.freertos.org/Documentation/RTOS_book.html).

 A comunicação entre as tarefas é realizada através de um buffer chamado que Queue (fila). As Queues possuem algumas funções que vão além do simples armazenamento de dados. Para saber mais sobre as Queues, tem esse [tutorial em português](https://www.embarcados.com.br/rtos-queue-sincronizacao-e-comunicacao/) ou o [manual do FreeRTOS](https://www.freertos.org/wp-content/uploads/2018/07/FreeRTOS_Reference_Manual_V10.0.0.pdf).
 
 Este projeto utiliza 2 semáforos, um para acesso ao firebase e outro para acesso a variavel de tempo. Caso tenha interesse em aprender como funciona o semáforo no FreeRTOS, tem esse [tutorial em portugues sobre semáforos](https://www.embarcados.com.br/rtos-semaforos-sincronizacao-de-tarefas/) ou o [manual do FreeRTOS](https://www.freertos.org/wp-content/uploads/2018/07/FreeRTOS_Reference_Manual_V10.0.0.pdf). Por se tratar de um sistema com multiplas tarefas, um recurso pode ser solicitado por diferentes rotinas ocasionando um erro. Para que isso não ocorra é utilizado um semáforo. Somente o processo que estiver com a liberação do semáforo utilizará o recurso e os demais ficarão aguardando sua liberação.
 
 Para disponibilizar a data e hora para o programa é utilizado a função Timer do próprio FreeRTOS. Esta função é disparada periodicamente e tem a função de atualizar a hora do sistema. Qualquer tarefa pode obter a hora atual lendo a variavel global ``` vGlobalTempo ``` com a utilização do semáforo ``` SemafTime ```. Para mais informações sobre Timers no FreeRTOS tem esse [tutorial em português](https://www.embarcados.com.br/rtos-software-timer-no-freertos/) ou o [manual do FreeRTOS](https://www.freertos.org/wp-content/uploads/2018/07/FreeRTOS_Reference_Manual_V10.0.0.pdf).
 
## Adicionando Sensores

Para adicionar um sensor ao código basta criar uma nova tarefa no Setup utilizano o código abaixo:
```C++
xTaskCreatePinnedToCore(taskSensorBMP180, "BMP180", 2048, NULL, 3, NULL, PRO_CPU_NUM);
```
* ```taskSensorBMP180``` - Nome da tarefa que será chamada após sua criação.
* ```"BMP180"``` - Não tem relevancia no programa. serve apenas para identificação da tarefa durante o debugg.
* ```2048``` - quantidade de memória resevada para execução da tarefa.
* ```NULL``` - Não é necessário alterar.
* ```3``` - prioridade da tarefa. quanto mais alto o numero, maior sua prioridade.
* ```PRO_CPU_NUM``` - nucleo onde será executado a tarefa.

A quantidade de memoria utilizada pela tarefa pode ser acompanhada através dos comandos do quadro abaixo. Esses comandos exibe na serial a quantidade de memoria livre na tarefa (task). Através dele é possivel determinar a quatidade necessária de memoria que a tarefa irá utilizar. Deixe uma sobrade memória por segurança.
```C++
uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
Serial.print("Memoria BMP180: ");
Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
```
Recomendo utilizar o nucleo ```PRO_CPU_NUM``` do ESP32 para trabalhar com sensores e comandos deixando o ```APP_CPU_NUM``` dedicado a tarefas que utilizam o firebase.

Com a tarefa criada no Setup, é hora de escrever o código que será executado por ela. Abaixo está um exemplo de tarefa de sensor.

```C++
void taskSensorBMP180(void *arg)
{
//--- esta parte será executada apenas 1 vez. Funciona parecido com o void setup da IDE do arduino ---
  Adafruit_BMP085 bmp;
  if (!bmp.begin()) // está conactado na porta padrão. para modificar usar bmp.begin(SCL, SDA)
    Serial.println("Could not find a valid BMP180 sensor, check wiring!");
  int contador = 0;
  Gravar sPressao;
  sPressao.ID = "BMPpressao";
  sPressao.sensorUnidade = "hPa";
  sPressao.sensorValorMinimo = 200000;
  sPressao.isComando = false;
  int horaReset = 1;
  bool isReseted = false;
//---------------------------------------------------------------------------------------------------

// Inicio da função de ciclo infinito. Funciona parecido com o void loop da IDE do arduino

  while (1)
  {
    float buff = bmp.readPressure();
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
    // IMPORTANTE
    // esta parte do programa grava as informações do sensor na QUEUE para que seja gravado no firebase
      xQueueSend(xFilaGravar, &sPressao, pdMS_TO_TICKS(1000));// espera até 1 segunto para gravar na QUEUE
    //--------------------------------------------------------------
      contador = 0;
    // verifica se mudou de dia para resetar o valor medio diário
      if (xSemaphoreTake(xSemafTime, pdMS_TO_TICKS(100)))
      {
        horaReset = vGlobalTempo.hora;
        xSemaphoreGive(xSemafTime);
      }
      if (horaReset == 0)
      {
        if (!isReseted)
        {
          sPressao.sensorValorMaximo = 0;
          sPressao.sensorValorMinimo = 200000;
          isReseted = true;
        }
      }
      else
      {
        isReseted = false;
      }
      //---------------------------------------------------------------
      // informa a memoria livre da task
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      Serial.print("Memoria BMP180: ");
      Serial.println(uxHighWaterMark); // quantidade de memoria sobrando para a tarefa
      //---------------------------------------------------------------------
    }
    contador++;
    vTaskDelay(pdMS_TO_TICKS(10000)); // este comando libera o processador para executar outras tarefas. O tempo está definido em 10 segundos neste exemplo
  }
}
```

## Adicionando Comandos

segue a lista

- [ ] lista 1
- [ ] lista 2



```C++

// criação da tarefa (codigo escrito no setup)
xTaskCreatePinnedToCore(taskRecebeComandoFirebase, "RecebeFB", 8192, NULL, 5, NULL, APP_CPU_NUM);
// xTaskCreatePinnedToCore( A, B, C, D, E, F, G );
// A - Nome da tarefa para a função em "C". é a função que será chamada após a criação da tarefa
// B - Nome descritivo da tarefa. Isso não é usado pelo FreeRTOS. É um nome amigavel para identificação da tarefa durante o debug
// C - Memoria alocada para a tarefa. É a maxima memoria que osistema vai precisar.
// D - 


```


