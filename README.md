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
 
A separação das atividades em tarefas permite trabalhar em uma atividade específica do programa facilitando a manutenção e aprimoramentos. A criação de uma tarefa no FreeRTOs pode ser observada abaixo. Ela se comporta de uma forma parecida com o conjunto "Setup" e "Loop" que usamos na IDE do arduino. Caso tenha curiosidade em entender como funciona uma task, tem esse excelente tutorial em portugues do Felipe Neves disponivel no site [Embarcados](https://www.embarcados.com.br/esp32-lidando-com-multiprocessamento-parte-ii/) ou o manual do FreeRTOS disponivel [aqui](https://www.freertos.org/Documentation/RTOS_book.html).



```C++

// criação da tarefa (codigo escrito no setup)
xTaskCreatePinnedToCore(taskRecebeComandoFirebase, "RecebeFB", 8192, NULL, 5, NULL, APP_CPU_NUM);
// xTaskCreatePinnedToCore( A, B, C, D, E, F, G );
// A - Nome da tarefa para a função em "C". é a função que será chamada após a criação da tarefa
// B - Nome descritivo da tarefa. Isso não é usado pelo FreeRTOS. É um nome amigavel para identificação da tarefa durante o debug
// C - Memoria alocada para a tarefa. É a maxima memoria que osistema vai precisar.
// D - 


```


