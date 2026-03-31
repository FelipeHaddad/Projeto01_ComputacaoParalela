/*
 * sensor_analyzer_par.c
 * Analisador Paralelo de Logs de Sensores - Abordagem de Estado Compartilhado
 *
 * Compile: gcc -O2 -o sensor_analyzer_par sensor_analyzer_par.c -lpthread -lm
 * Uso    : ./sensor_analyzer_par <num_threads> <arquivo.log>
 */

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

// Estado global compartilhado

static EstatisticaSensores stats[1001];
static pthread_mutex_t     stats_mutex[1001]; /* mutex por sensor = menos contenção */

static int             contadorStatus = 0;
static pthread_mutex_t alert_mutex;

static double          consumoEnergia = 0.0;
static pthread_mutex_t energy_mutex;

// Bloco de trabalho
typedef struct {
    Sensor *logs;
    long    start;
    long    end;
} WorkBlock;

/* ─────────────────────────────────────────────
   LEITURA DO ARQUIVO
   Formato: sensor_ID data hora tipo valor campo2 val2 status STATUS
   O sscanf pula 3 tokens (%*s %*s %*s) para chegar no valor do status.
   ───────────────────────────────────────────── */
void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido)
{
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }

    char linha[256];
    int i = 0;

    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        /* Remove \r e \n */
        char *cr = strchr(linha, '\r'); if (cr) *cr = '\0';
        char *nl = strchr(linha, '\n'); if (nl) *nl = '\0';

        int campos = sscanf(linha,
                            "sensor_%d %10s %8s %14s %f %*s %9s",
                            &logs[i].sensor_id,
                            logs[i].data,
                            logs[i].hora,
                            logs[i].tipo,
                            &logs[i].valor,
                            logs[i].status);
        if (campos == 6) {
            i++;
        }
    }

    *total_lido = i;
    fclose(file);
}

//Função thread
static void *worker(void *arg)
{
    WorkBlock *wb  = (WorkBlock *)arg;
    Sensor    *arr = wb->logs;

    for (long i = wb->start; i < wb->end; i++) {
        int id = arr[i].sensor_id;

        /* 1. Alertas */
        if (strcmp(arr[i].status, "ALERTA")  == 0 ||
            strcmp(arr[i].status, "CRITICO") == 0) {
            pthread_mutex_lock(&alert_mutex);
            contadorStatus++;
            pthread_mutex_unlock(&alert_mutex);
        }

        /* 2. Energia */
        if (strcmp(arr[i].tipo, "energia") == 0) {
            pthread_mutex_lock(&energy_mutex);
            consumoEnergia += arr[i].valor;
            pthread_mutex_unlock(&energy_mutex);
        }

        /* 3. Temperatura */
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

//Main
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <num_threads>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int         num_threads = atoi(argv[1]);
    const char *filename = "sensores.log";

    if (num_threads <= 0) {
        fprintf(stderr, "ERRO: numero de threads invalido: %d\n", num_threads);
        return EXIT_FAILURE;
    }

    /* ── 1. Aloca e lê ── */
    Sensor *logs = (Sensor *)malloc(MAX_LINHAS * sizeof(Sensor));
    if (!logs) {
        printf("Erro: Memoria insuficiente.\n");
        return 1;
    }

    int total_lido = 0;
    leituraArquivo(filename, logs, &total_lido);

    /* ── 2. Inicializa estado compartilhado ── */
    memset(stats, 0, sizeof(stats));
    pthread_mutex_init(&alert_mutex,  NULL);
    pthread_mutex_init(&energy_mutex, NULL);
    for (int i = 0; i <= 1000; i++)
        pthread_mutex_init(&stats_mutex[i], NULL);

    /* ── 3. Cria threads ── */
    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    WorkBlock *blocks  = (WorkBlock *)malloc(num_threads * sizeof(WorkBlock));
    if (!threads || !blocks) { perror("malloc"); return EXIT_FAILURE; }

    long chunk = total_lido / num_threads;
    long rem   = total_lido % num_threads;

    clock_t t_start = clock();

    long offset = 0;
    for (int t = 0; t < num_threads; t++) {
        blocks[t].logs  = logs;
        blocks[t].start = offset;
        blocks[t].end   = offset + chunk + (t < rem ? 1 : 0);
        offset          = blocks[t].end;
        pthread_create(&threads[t], NULL, worker, &blocks[t]);
    }

    /* ── 4. Aguarda threads ── */
    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    clock_t t_end = clock();
    double elapsed = (double)(t_end - t_start) / CLOCKS_PER_SEC;

    /* ── 5. Pos-processamento e saida (igual ao sequencial) ── */
    float maior_desvio     = -1.0;
    int   id_mais_instavel = -1;
    int   impressos        = 0;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");

    for (int j = 0; j <= 1000; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = stats[j].soma_total / stats[j].contador;

            double variancia = (stats[j].soma_quadrados / stats[j].contador)
                             - ((double)stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0) ? sqrt(variancia) : 0;

            if (impressos < MAX_TOP) {
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
    if (id_mais_instavel != -1) {
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n",
               id_mais_instavel, maior_desvio);
    }
    printf("Tempo de execucao: %.6f segundos (%d thread%s, %d registros)\n",
           elapsed, num_threads, num_threads > 1 ? "s" : "", total_lido);

    /* Cleanup */
    pthread_mutex_destroy(&alert_mutex);
    pthread_mutex_destroy(&energy_mutex);
    for (int i = 0; i <= 1000; i++)
        pthread_mutex_destroy(&stats_mutex[i]);
    free(logs);
    free(threads);
    free(blocks);

    return EXIT_SUCCESS;
}