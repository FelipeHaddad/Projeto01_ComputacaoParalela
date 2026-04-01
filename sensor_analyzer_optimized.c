/*
gcc -O2 sensor_analyzer_optimized.c -o sensor_analyzer_optimized -lpthread -lm
./sensor_analyzer_optimized 8
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define MAX_LINHAS 11000000 
#define MAX_SENSORES 1001

typedef struct {
    int sensor_id;      
    char data[11];      
    char hora[9];       
    char tipo[20];      
    float valor;        
    char status[15];    
} Sensor;

typedef struct {
    double soma_total;      
    double soma_quadrados;  
    int contador;           
    float media;            
    float desvio_padrao;    
} EstatisticaSensores;

typedef struct {
    Sensor *logs;
    long start;
    long end;
} WorkBlock;

// ESTRUTURAS GLOBAIS (Estado Compartilhado)
EstatisticaSensores stats[MAX_SENSORES]; 
int contadorStatus = 0;   
double consumoEnergia = 0; 

// Mutexes globais - Usados apenas na REDUÇÃO FINAL
pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }
    char linha[256];
    int i = 0;
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        if (sscanf(linha, "sensor_%d %s %s %s %f", 
                   &logs[i].sensor_id, logs[i].data, logs[i].hora, logs[i].tipo, &logs[i].valor) == 5) {
            char *ptr_status = strstr(linha, "status ");
            if (ptr_status) sscanf(ptr_status, "status %s", logs[i].status);
            i++;
        }
    }
    *total_lido = i; 
    fclose(file);
}

static void *worker(void *arg) {
    WorkBlock *wb = (WorkBlock *)arg;
    Sensor *arr = wb->logs;

    // --- OTIMIZAÇÃO: ACUMULADORES LOCAIS ---
    // Cada thread tem sua própria cópia privada para evitar contenção
    EstatisticaSensores local_stats[MAX_SENSORES];
    memset(local_stats, 0, sizeof(local_stats));
    int local_alertas = 0;
    double local_energia = 0;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        // 1. Alertas Locais
        if (strcmp(arr[i].status, "ALERTA") == 0 || strcmp(arr[i].status, "CRITICO") == 0) {
            local_alertas++;
        }

        // 2. Energia Local
        if (strcmp(arr[i].tipo, "energia") == 0) {
            local_energia += arr[i].valor;
        }

        // 3. Temperatura Local
        if (strcmp(arr[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id < MAX_SENSORES) {
                local_stats[id].soma_total += arr[i].valor;
                local_stats[id].soma_quadrados += (double)arr[i].valor * arr[i].valor;
                local_stats[id].contador++;
            }
        }
    }

    // --- REDUÇÃO FINAL: Sincronização única por thread ---
    pthread_mutex_lock(&global_mutex);
    contadorStatus += local_alertas;
    consumoEnergia += local_energia;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (local_stats[j].contador > 0) {
            stats[j].soma_total += local_stats[j].soma_total;
            stats[j].soma_quadrados += local_stats[j].soma_quadrados;
            stats[j].contador += local_stats[j].contador;
        }
    }
    pthread_mutex_unlock(&global_mutex);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <num_threads>\n", argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    int total_lido = 0;
    clock_t start, end;

    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) { printf("Erro: Memoria insuficiente.\n"); return 1; }

    printf("Lendo arquivo... Aguarde.\n");
    start = clock(); 
    
    leituraArquivo("sensores.log", logs, &total_lido);
    
    memset(stats, 0, sizeof(stats)); 

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    WorkBlock *blocks = (WorkBlock *)malloc(num_threads * sizeof(WorkBlock));

    long chunk = total_lido / num_threads;
    long rem = total_lido % num_threads;

    long offset = 0;
    for (int t = 0; t < num_threads; t++) {
        blocks[t].logs = logs;
        blocks[t].start = offset;
        blocks[t].end = offset + chunk + (t < rem ? 1 : 0);
        offset = blocks[t].end;
        pthread_create(&threads[t], NULL, worker, &blocks[t]);
    }

    for (int t = 0; t < num_threads; t++) pthread_join(threads[t], NULL);

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;

    printf("\n--- RELATORIO DE TEMPERATURA OTIMIZADO (10 PRIMEIROS) ---\n");
    int impressos = 0;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            double variancia = (stats[j].soma_quadrados / stats[j].contador) - (stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;

            if (impressos < 10) {
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n", j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }
            if (stats[j].desvio_padrao > maior_desvio) {
                maior_desvio = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }

    end = clock(); 
    double tempo_cpu = ((double) (end - start)) / CLOCKS_PER_SEC;

    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    if (id_mais_instavel != -1) {
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio);
    }
    printf("Tempo de execucao: %.4f segundos\n", tempo_cpu);

    pthread_mutex_destroy(&global_mutex);
    free(logs); free(threads); free(blocks);
    return 0;
}