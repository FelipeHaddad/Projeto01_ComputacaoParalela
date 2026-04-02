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

// ESTADO GLOBAL
EstatisticaSensores stats[MAX_SENSORES]; 
int contadorStatus = 0;   
double consumoEnergia = 0; 

// ESTRATÉGIA: Múltiplos Mutexes para evitar contenção na redução final
pthread_mutex_t stats_mutex[MAX_SENSORES]; 
pthread_mutex_t alert_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t energy_mutex = PTHREAD_MUTEX_INITIALIZER;

void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) { perror("Erro ao abrir arquivo"); exit(1); }
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

    // ESTRATÉGIA: Acumuladores Locais (Elimina mutex dentro do loop principal)
    EstatisticaSensores local_stats[MAX_SENSORES];
    memset(local_stats, 0, sizeof(local_stats));
    int local_alertas = 0;
    double local_energia = 0;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        if (strcmp(arr[i].status, "ALERTA") == 0 || strcmp(arr[i].status, "CRITICO") == 0) {
            local_alertas++;
        }

        if (strcmp(arr[i].tipo, "energia") == 0) {
            local_energia += arr[i].valor;
        }

        if (strcmp(arr[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id < MAX_SENSORES) {
                local_stats[id].soma_total += arr[i].valor;
                local_stats[id].soma_quadrados += (double)arr[i].valor * arr[i].valor;
                local_stats[id].contador++;
            }
        }
    }

    // ESTRATÉGIA: Redução com Múltiplos Mutexes (Particionamento por Sensor)
    // Atualiza alertas
    pthread_mutex_lock(&alert_mutex);
    contadorStatus += local_alertas;
    pthread_mutex_unlock(&alert_mutex);

    // Atualiza energia
    pthread_mutex_lock(&energy_mutex);
    consumoEnergia += local_energia;
    pthread_mutex_unlock(&energy_mutex);

    // Atualiza cada sensor de forma independente (Sem travar o array inteiro)
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (local_stats[j].contador > 0) {
            pthread_mutex_lock(&stats_mutex[j]);
            stats[j].soma_total += local_stats[j].soma_total;
            stats[j].soma_quadrados += local_stats[j].soma_quadrados;
            stats[j].contador += local_stats[j].contador;
            pthread_mutex_unlock(&stats_mutex[j]);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Uso: %s <num_threads>\n", argv[0]); return 1; }
    int num_threads = atoi(argv[1]);
    int total_lido = 0;
    clock_t start, end;

    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) { return 1; }

    printf("Lendo arquivo... Aguarde.\n");
    start = clock(); 
    leituraArquivo("sensores.log", logs, &total_lido);
    
    memset(stats, 0, sizeof(stats)); 
    for (int i = 0; i < MAX_SENSORES; i++) pthread_mutex_init(&stats_mutex[i], NULL);

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

    // Pós-processamento final
    float maior_desvio = -1.0;
    int id_mais_instavel = -1;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            double variancia = (stats[j].soma_quadrados / stats[j].contador) - (stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;
            if (stats[j].desvio_padrao > maior_desvio) {
                maior_desvio = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }

    end = clock(); 
    double tempo_cpu = ((double) (end - start)) / CLOCKS_PER_SEC;

    printf("\n--- RELATORIO OTIMIZADO FINAL ---\n");
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio);
    printf("Tempo de execucao: %.4f segundos\n", tempo_cpu);

    for (int i = 0; i < MAX_SENSORES; i++) pthread_mutex_destroy(&stats_mutex[i]);
    free(logs); free(threads); free(blocks);
    return 0;
}
