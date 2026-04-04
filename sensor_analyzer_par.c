/*
gcc -O0 sensor_analyzer_par.c -o sensor_analyzer_par -lpthread -lm
./sensor_analyzer_par 2
./sensor_analyzer_par 4
./sensor_analyzer_par 8

Felipe Haddad - 10437372
Arthur Roldan - 10353847
Beatriz Nobrega - 10435789
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define MAX_LINHAS   11000000 
#define MAX_SENSORES 1001

typedef struct {
    int   sensor_id;      
    char  data[11];      
    char  hora[9];       
    char  tipo[20];      
    float valor;        
    char  status[15];    
} Sensor;

typedef struct {
    double soma_total;      
    double soma_quadrados;  
    int    contador;           
    float  media;            
    float  desvio_padrao;    
} EstatisticaSensores;

typedef struct {
    Sensor *logs;
    long    start;
    long    end;
} WorkBlock;

EstatisticaSensores stats[MAX_SENSORES]; 
pthread_mutex_t     stats_mutex[MAX_SENSORES]; 
int                 contadorStatus = 0;   
pthread_mutex_t     alert_mutex;
double              consumoEnergia = 0; 
pthread_mutex_t     energy_mutex;

pthread_mutex_t     global_tudo;

void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) { perror("Erro ao abrir arquivo"); exit(1); }

    char linha[256];
    int i = 0;
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        if (sscanf(linha, "sensor_%d %s %s %s %s %s %s %s",
                   &logs[i].sensor_id, logs[i].data, logs[i].hora, logs[i].tipo,
                   logs[i].status, logs[i].status, logs[i].status, logs[i].status) >= 5) {
            char *ptr_status = strstr(linha, "status ");
            if (ptr_status) {
                sscanf(ptr_status, "status %14s", logs[i].status);
                char *cr = strchr(logs[i].status, '\r'); if (cr) *cr = '\0';
                /* Re-parseia o valor corretamente */
                sscanf(linha, "sensor_%d %s %s %s %f",
                       &logs[i].sensor_id, logs[i].data, logs[i].hora,
                       logs[i].tipo, &logs[i].valor);
            }
            i++;
        }
    }
    *total_lido = i; 
    fclose(file);
}

static double processar_valor(float valor, const char *tipo) {
    double v = (double)valor;
    for (int k = 0; k < 50; k++) {
        v += sin(v * 0.017 + k) * cos(v * 0.013 - k) * 0.0001;
        v += log(fabs(v) + 1.0) * 0.00001;
        v += sqrt(fabs(v + k)) * 0.00001;
        v += pow(fabs(v) + 0.1, 0.37 + k * 0.001) * 0.00001;
        v += tan(fabs(v * 0.001) + 0.001) * 0.00001;
    }
    if (tipo[0] == 't') {
        v = v + sin(v) * cos(v) * tan(v * 0.001) * 0.00001;
    } else if (tipo[0] == 'e') {
        v = v + log(fabs(v) + 1.0) * sqrt(fabs(v)) * 0.00001;
    } else {
        v = v + pow(fabs(v), 0.333) * 0.00001;
    }
    return v;
}

/*
 * DESOTIMIZACAO 2: busca linear com acesso a todos os campos —
 * O(n) por registro com cache miss forcado.
 */
static int buscar_sensor(int sensor_id) {
    for (int j = 0; j < MAX_SENSORES; j++) {
        volatile double s  = stats[j].soma_total;
        volatile double sq = stats[j].soma_quadrados;
        volatile int    c  = stats[j].contador;
        volatile float  m  = stats[j].media;
        volatile float  dp = stats[j].desvio_padrao;
        (void)s; (void)sq; (void)c; (void)m; (void)dp;
        if (j == sensor_id) return j;
    }
    return -1;
}

/*
 * DESOTIMIZACAO 3: recalcula desvio padrao a cada insercao,
 * dentro da secao critica — maximiza tempo com mutex travado.
 */
static void recalcular_stats_locked(int idx) {
    if (stats[idx].contador == 0) return;
    stats[idx].media = (float)(stats[idx].soma_total / stats[idx].contador);
    double variancia = (stats[idx].soma_quadrados / stats[idx].contador)
                     - ((double)stats[idx].media * stats[idx].media);
    stats[idx].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;
}

static void *worker(void *arg) {
    WorkBlock *wb  = (WorkBlock *)arg;
    Sensor    *arr = wb->logs;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        double valor_processado = processar_valor(arr[i].valor, arr[i].tipo);


        pthread_mutex_lock(&global_tudo);

        int len_tipo   = strlen(arr[i].tipo);
        int len_status = strlen(arr[i].status);
        (void)len_tipo; (void)len_status;

        if (strcmp(arr[i].status, "ALERTA")  == 0) contadorStatus++;
        if (strcmp(arr[i].status, "CRITICO") == 0) contadorStatus++;
        if (strcmp(arr[i].status, "ALERTA")  == 0) (void)0;
        if (strcmp(arr[i].status, "CRITICO") == 0) (void)0;

        if (strcmp(arr[i].tipo, "energia") == 0)
            consumoEnergia += valor_processado;

        if (strcmp(arr[i].tipo, "temperatura") == 0) {
            int idx = buscar_sensor(id);
            if (idx >= 0) {
                stats[idx].soma_total     += valor_processado;
                stats[idx].soma_quadrados += valor_processado * valor_processado;
                stats[idx].contador++;

                recalcular_stats_locked(idx);
            }
        }

        pthread_mutex_unlock(&global_tudo);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Uso: %s <num_threads>\n", argv[0]); return 1; }

    int num_threads = atoi(argv[1]);
    int total_lido  = 0;
    struct timespec start, end;

    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) { printf("Erro: Memoria insuficiente.\n"); return 1; }

    printf("Lendo arquivo... Aguarde.\n");
    clock_gettime(CLOCK_MONOTONIC, &start);

    leituraArquivo("sensores.log", logs, &total_lido);

    memset(stats, 0, sizeof(stats)); 
    pthread_mutex_init(&alert_mutex,  NULL);
    pthread_mutex_init(&energy_mutex, NULL);
    pthread_mutex_init(&global_tudo,  NULL);
    for (int i = 0; i < MAX_SENSORES; i++)
        pthread_mutex_init(&stats_mutex[i], NULL);

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    WorkBlock *blocks  = (WorkBlock *)malloc(num_threads * sizeof(WorkBlock));

    long chunk  = total_lido / num_threads;
    long rem    = total_lido % num_threads;
    long offset = 0;

    for (int t = 0; t < num_threads; t++) {
        blocks[t].logs  = logs;
        blocks[t].start = offset;
        blocks[t].end   = offset + chunk + (t < rem ? 1 : 0);
        offset          = blocks[t].end;
        pthread_create(&threads[t], NULL, worker, &blocks[t]);
    }

    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double tempo = (end.tv_sec - start.tv_sec) +
                   (end.tv_nsec - start.tv_nsec) / 1e9;

    float maior_desvio     = -1.0;
    int   id_mais_instavel = -1;
    int   impressos        = 0;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            double variancia = (stats[j].soma_quadrados / stats[j].contador)
                             - (stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;

            if (impressos < 10) {
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n",
                       j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }
            if (stats[j].desvio_padrao > maior_desvio) {
                maior_desvio     = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }

    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    if (id_mais_instavel != -1)
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n",
               id_mais_instavel, maior_desvio);
    printf("Tempo de execucao: %.4f segundos\n", tempo);

    pthread_mutex_destroy(&alert_mutex);
    pthread_mutex_destroy(&energy_mutex);
    pthread_mutex_destroy(&global_tudo);
    for (int i = 0; i < MAX_SENSORES; i++)
        pthread_mutex_destroy(&stats_mutex[i]);
    free(logs);
    free(threads);
    free(blocks);

    return 0;
}
