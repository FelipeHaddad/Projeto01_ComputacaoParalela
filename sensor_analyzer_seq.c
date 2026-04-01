#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// Definição do limite de linhas para suportar os 10 milhões exigidos
#define MAX_LINHAS 11000000 
// Limite de sensores baseado no identificador sensor_001 até sensor_999
#define MAX_SENSORES 1001

// Estrutura para armazenar dados do sensor
typedef struct {
    int sensor_id;      // ID numérico extraído de "sensor_XXX"
    char data[11];      // AAAA-MM-DD 
    char hora[9];       // HH:MM:SS 
    char tipo[20];      // temperatura, umidade, energia, consumo
    float valor;        // Valor numérico da leitura 
    char status[15];    // OK, ALERTA ou CRITICO
} Sensor;

// Estrutura para armazenar dados estatísticos
typedef struct {
    double soma_total;      // Para cálculo da média 
    double soma_quadrados;  // Para cálculo do desvio padrão 
    int contador;           // Quantidade de leituras válidas
    float media;            // Resultado final da média 
    float desvio_padrao;    // Resultado final da variação térmica 
} EstatisticaSensores;

// Alocação do array de estatísticas para evitar estouro de pilha
EstatisticaSensores stats[MAX_SENSORES]; 

// Função responsável por abrir o arquivo, ler a linha atual e criar a estrutura de dados logs para o sensor
void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }

    char linha[256];
    int i = 0;

    // Lê o arquivo linha por linha até o fim ou atingir o limite de memória
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        // sscanf tenta extrair os 5 primeiros campos fixos da linha 
        if (sscanf(linha, "sensor_%d %s %s %s %f", 
                   &logs[i].sensor_id, logs[i].data, logs[i].hora, logs[i].tipo, &logs[i].valor) == 5) {
            
            // strstr busca a palavra "status" para lidar com colunas variáveis no meio da linha 
            char *ptr_status = strstr(linha, "status ");
            if (ptr_status) {
                sscanf(ptr_status, "status %s", logs[i].status);
            }
            i++;
        }
    }

    *total_lido = i; // Atualiza a contagem de registros processados
    fclose(file);
}

int main() {
    int total_lido = 0;       // Quantidade de registros lidos
    int contadorStatus = 0;   // Contador para ALERTA e CRITICO
    double consumoEnergia = 0; // Acumulador para soma de energia 
    clock_t start, end;       // Contador de Tempo

    // Alocação dinâmica no Heap: essencial para grandes volumes de dados
    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) {
        printf("Erro: Memoria insuficiente para alocar o array de logs.\n");
        return 1;
    }

    printf("Lendo arquivo... Aguarde.\n");
    start = clock(); // Inicia a medição do tempo de execução
    
    leituraArquivo("sensores.log", logs, &total_lido);
    
    // Inicializa o array de estatísticas com zeros
    memset(stats, 0, sizeof(stats)); 

    // Loop único de processamento (Complexidade O(n)) 
    for (int i = 0; i < total_lido; i++) {
        int id = logs[i].sensor_id;

        // 1. Totalização de alertas (ALERTA ou CRITICO)
        if (strcmp(logs[i].status, "ALERTA") == 0 || strcmp(logs[i].status, "CRITICO") == 0) {
            contadorStatus++;
        }

        // 2. Acumulação do consumo total de energia 
        if (strcmp(logs[i].tipo, "energia") == 0) {
            consumoEnergia += logs[i].valor;
        }

        // 3. Processamento de Temperatura para média e desvio padrão 
        if (strcmp(logs[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id < MAX_SENSORES) {
                stats[id].soma_total += logs[i].valor;
                // Armazena a soma dos quadrados para cálculo do desvio padrão sem novo loop 
                stats[id].soma_quadrados += (double)logs[i].valor * logs[i].valor;
                stats[id].contador++;
            }
        }
    }

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
    int impressos = 0;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0) {
            // Cálculo da média aritmética 
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            
            // Cálculo incremental da variância: E[X^2] - (E[X])^2 
            double variancia = (stats[j].soma_quadrados / stats[j].contador) - (stats[j].media * stats[j].media);
            // sqrt() calcula o desvio padrão final 
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;

            if (impressos < 10) {
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n", j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }

            // Identifica o sensor com maior variação térmica 
            if (stats[j].desvio_padrao > maior_desvio) {
                maior_desvio = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }

    end = clock(); // Finaliza a medição do tempo
    double tempo_cpu = ((double) (end - start)) / CLOCKS_PER_SEC;

    // Exibição dos resultados finais conforme exigido no roteiro
    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus); [cite: 158]
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia); [cite: 159]
    if (id_mais_instavel != -1) {
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio); [cite: 157, 162]
    }
    printf("Tempo de execucao: %.4f segundos\n", tempo_cpu); [cite: 161]

    // Liberação da memória alocada no Heap para evitar vazamentos (Memory Leaks)
    free(logs);
    return 0;
}