#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#define MAX_LINHAS 10000000
#define MAX_TOP    10

typedef struct {
    int   sensor_id;
    char  data[11];
    char  hora[9];
    char  tipo[15];
    float valor;
    char  status[10];
} Sensor;

typedef struct {
    double soma_total;
    double soma_quadrados;
    int    contador;
    float  media;
    float  desvio_padrao;
} EstatisticaSensores;

static EstatisticaSensores stats[1001];
static pthread_mutex_t     stats_mutex[1001]; 

static int             contadorStatus = 0;
static pthread_mutex_t alert_mutex; 

static double          consumoEnergia = 0.0;
static pthread_mutex_t energy_mutex; 

typedef struct {
    Sensor *logs;
    long    start;
    long    end;
} WorkBlock;

void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido)
{
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) { perror("Erro ao abrir arquivo"); exit(1); }

    char linha[256];
    int i = 0;
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        int campos = sscanf(linha, "sensor_%d %10s %8s %14s %f %*s %9s", 
                            &logs[i].sensor_id, logs[i].data, logs[i].hora, 
                            logs[i].tipo, &logs[i].valor, logs[i].status);
        if (campos >= 5) i++; 
    }
    *total_lido = i;
    fclose(file);
}

static void *worker(void *arg)
{
    WorkBlock *wb  = (WorkBlock *)arg;
    Sensor    *arr = wb->logs;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        if (strcmp(arr[i].status, "ALERTA") == 0 || strcmp(arr[i].status, "CRITICO") == 0) {
            pthread_mutex_lock(&alert_mutex);
            contadorStatus++;
            pthread_mutex_unlock(&alert_mutex);
        }

        if (strcmp(arr[i].tipo, "energia") == 0) {
            pthread_mutex_lock(&energy_mutex);
            consumoEnergia += arr[i].valor;
            pthread_mutex_unlock(&energy_mutex);
        }

        if (strcmp(arr[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id <= 1000) {
                pthread_mutex_lock(&stats_mutex[id]);
                stats[id].soma_total     += arr[i].valor;
                stats[id].soma_quadrados += (double)arr[i].valor * arr[i].valor;
                stats[id].contador++;
                pthread_mutex_unlock(&stats_mutex[id]);
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { printf("Uso: %s <num_threads>\n", argv[0]); return 1; }
    int num_threads = atoi(argv[1]);

    Sensor *logs = (Sensor *)malloc(MAX_LINHAS * sizeof(Sensor));
    int total_lido = 0;

    leituraArquivo("sensores.log", logs, &total_lido);

    memset(stats, 0, sizeof(stats));
    pthread_mutex_init(&alert_mutex, NULL);
    pthread_mutex_init(&energy_mutex, NULL);
    for (int i = 0; i <= 1000; i++) pthread_mutex_init(&stats_mutex[i], NULL);

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    WorkBlock *blocks  = (WorkBlock *)malloc(num_threads * sizeof(WorkBlock));

    long chunk = total_lido / num_threads;
    long rem   = total_lido % num_threads;

    long offset = 0;
    for (int t = 0; t < num_threads; t++) {
        blocks[t].logs  = logs;
        blocks[t].start = offset;
        blocks[t].end   = offset + chunk + (t < rem ? 1 : 0);
        offset          = blocks[t].end;
        pthread_create(&threads[t], NULL, worker, &blocks[t]);
    }

    for (int t = 0; t < num_threads; t++) pthread_join(threads[t], NULL);

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;
    int impressos = 0;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");

    for (int j = 0; j <= 1000; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            double variancia = (stats[j].soma_quadrados / stats[j].contador) - ((double)stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0) ? (float)sqrt(variancia) : 0.0f;

            if (impressos < MAX_TOP) {
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n", j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }

            if (stats[j].desvio_padrao > maior_desvio) {
                maior_desvio = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }

    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    if (id_mais_instavel != -1) {
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio);
    }

    pthread_mutex_destroy(&alert_mutex);
    pthread_mutex_destroy(&energy_mutex);
    for (int i = 0; i <= 1000; i++) pthread_mutex_destroy(&stats_mutex[i]);
    
    free(logs);
    free(threads);
    free(blocks);

    return 0;
}