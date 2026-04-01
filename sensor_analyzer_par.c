/*
gcc -O2 sensor_analyzer_par.c -o sensor_analyzer_par -lpthread -lm
Escolhe o numero de threads:
./sensor_analyzer_par 2
./sensor_analyzer_par 4
./sensor_analyzer_par 8
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

// Estrutura para passar o intervalo de trabalho para cada thread
typedef struct {
    Sensor *logs;
    long start;
    long end;
} WorkBlock;

// Estado global compartilhado
EstatisticaSensores stats[MAX_SENSORES]; 
pthread_mutex_t stats_mutex[MAX_SENSORES]; 

int contadorStatus = 0;   
pthread_mutex_t alert_mutex;

double consumoEnergia = 0; 
pthread_mutex_t energy_mutex;

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
            if (ptr_status) {
                sscanf(ptr_status, "status %s", logs[i].status);
            }
            i++;
        }
    }
    *total_lido = i; 
    fclose(file);
}

// Função executada pelas threads
static void *worker(void *arg) {
    WorkBlock *wb = (WorkBlock *)arg;
    Sensor *arr = wb->logs;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        // 1. Totalização de alertas (Protegido por Mutex)
        if (strcmp(arr[i].status, "ALERTA") == 0 || strcmp(arr[i].status, "CRITICO") == 0) {
            pthread_mutex_lock(&alert_mutex);
            contadorStatus++;
            pthread_mutex_unlock(&alert_mutex);
        }

        // 2. Acumulação do consumo total de energia (Protegido por Mutex)
        if (strcmp(arr[i].tipo, "energia") == 0) {
            pthread_mutex_lock(&energy_mutex);
            consumoEnergia += arr[i].valor;
            pthread_mutex_unlock(&energy_mutex);
        }

        // 3. Processamento de Temperatura (Protegido por Mutex Granular por ID)
        if (strcmp(arr[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id < MAX_SENSORES) {
                pthread_mutex_lock(&stats_mutex[id]);
                stats[id].soma_total += arr[i].valor;
                stats[id].soma_quadrados += (double)arr[i].valor * arr[i].valor;
                stats[id].contador++;
                pthread_mutex_unlock(&stats_mutex[id]);
            }
        }
    }
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
    if (logs == NULL) {
        printf("Erro: Memoria insuficiente.\n");
        return 1;
    }

    printf("Lendo arquivo... Aguarde.\n");
    start = clock(); 
    
    leituraArquivo("sensores.log", logs, &total_lido);
    
    // Inicialização das estruturas e Mutexes
    memset(stats, 0, sizeof(stats)); 
    pthread_mutex_init(&alert_mutex, NULL);
    pthread_mutex_init(&energy_mutex, NULL);
    for (int i = 0; i < MAX_SENSORES; i++) {
        pthread_mutex_init(&stats_mutex[i], NULL);
    }

    // Criação das threads e divisão de carga
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

    // Sincronização: Aguarda todas as threads
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
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

    // Limpeza de recursos
    pthread_mutex_destroy(&alert_mutex);
    pthread_mutex_destroy(&energy_mutex);
    for (int i = 0; i < MAX_SENSORES; i++) {
        pthread_mutex_destroy(&stats_mutex[i]);
    }
    
    free(logs);
    free(threads);
    free(blocks);

    return 0;
}