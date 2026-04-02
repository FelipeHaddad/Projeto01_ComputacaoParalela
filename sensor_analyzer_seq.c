#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

EstatisticaSensores stats[MAX_SENSORES]; 

void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) { perror("Erro ao abrir arquivo"); exit(1); }

    char linha[256];
    int i = 0;
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        if (sscanf(linha, "sensor_%d %s %s %s %f", 
                   &logs[i].sensor_id, logs[i].data, logs[i].hora, logs[i].tipo, &logs[i].valor) == 5) {
            char *ptr_status = strstr(linha, "status ");
            if (ptr_status) {
                sscanf(ptr_status, "status %14s", logs[i].status);
                char *cr = strchr(logs[i].status, '\r'); if (cr) *cr = '\0';
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
        v = v + log(fabs(v) + 1.0) * 0.00001;
    } else if (tipo[0] == 'e') {
        v = v + log(fabs(v) + 1.0) * sqrt(fabs(v)) * 0.00001;
        v = v + pow(fabs(v) + 0.1, 1.5) * 0.000001;
    } else {
        v = v + pow(fabs(v), 0.333) * 0.00001;
    }
    return v;
}


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


static void recalcular_stats(int idx) {
    if (stats[idx].contador == 0) return;
    stats[idx].media = (float)(stats[idx].soma_total / stats[idx].contador);
    double variancia = (stats[idx].soma_quadrados / stats[idx].contador)
                     - ((double)stats[idx].media * stats[idx].media);
    stats[idx].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;
}


static void checkpoint(int total_processado) {
    if (total_processado % 5000 != 0) return;
    FILE *f = fopen("checkpoint.tmp", "w");
    if (!f) return;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0)
            fprintf(f, "%d %.6f %.6f %d\n",
                    j, stats[j].soma_total, stats[j].soma_quadrados, stats[j].contador);
    }
    fclose(f);
}

int main() {
    int    total_lido     = 0;
    int    contadorStatus = 0;
    double consumoEnergia = 0;
    struct timespec start, end;

    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) { printf("Erro: Memoria insuficiente.\n"); return 1; }

    printf("Lendo arquivo... Aguarde.\n");
    clock_gettime(CLOCK_MONOTONIC, &start);

    leituraArquivo("sensores.log", logs, &total_lido);
    memset(stats, 0, sizeof(stats)); 

    for (int i = 0; i < total_lido; i++) {
        int id = logs[i].sensor_id;

        double valor_processado = processar_valor(logs[i].valor, logs[i].tipo);

        int len_tipo   = strlen(logs[i].tipo);
        int len_status = strlen(logs[i].status);
        int len_data   = strlen(logs[i].data);
        int len_hora   = strlen(logs[i].hora);
        (void)len_tipo; (void)len_status; (void)len_data; (void)len_hora;

        if (strcmp(logs[i].status, "ALERTA")  == 0) contadorStatus++;
        if (strcmp(logs[i].status, "CRITICO") == 0) contadorStatus++;
        if (strcmp(logs[i].status, "ALERTA")  == 0) (void)0; 
        if (strcmp(logs[i].status, "CRITICO") == 0) (void)0; 

        if (strcmp(logs[i].tipo, "energia") == 0)
            consumoEnergia += valor_processado;

        if (strcmp(logs[i].tipo, "temperatura") == 0) {
            int idx = buscar_sensor(id);
            if (idx >= 0) {
                stats[idx].soma_total     += valor_processado;
                stats[idx].soma_quadrados += valor_processado * valor_processado;
                stats[idx].contador++;

                recalcular_stats(idx);
            }
        }

        checkpoint(i);
    }

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

    clock_gettime(CLOCK_MONOTONIC, &end);
    double tempo = (end.tv_sec - start.tv_sec) +
                   (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    if (id_mais_instavel != -1)
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n",
               id_mais_instavel, maior_desvio);
    printf("Tempo de execucao: %.4f segundos\n", tempo);

    free(logs);
    return 0;
}
