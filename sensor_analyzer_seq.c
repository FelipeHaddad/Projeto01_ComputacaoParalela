#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_LINHAS 10000000

typedef struct {
    int sensor_id;
    char data[11];
    char hora[9];
    char tipo[15]; 
    float valor;
    char status[10];
} Sensor;

typedef struct {
    char sensor_id_str[12];
    double soma_total;
    double soma_quadrados;
    int contador; 
    float media;
    float desvio_padrao;
} EstatisticaSensores;

// Função renomeada para ser consistente
void leituraArquivo(const char *nome_arquivo, Sensor *logs, int *total_lido) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }

    char linha[150];
    int i = 0;

    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        int campos = sscanf(linha, "sensor_%d %s %s %s %f %*s %s", 
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

int main () {
    int total_lido = 0;
    int contadorStatus = 0;
    double consumoEnergia = 0;

    // Alocação dinâmica no Heap
    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if(logs == NULL){
        printf("Erro: Memória insuficiente.\n");
        return 1;
    }

    leituraArquivo("sensores.log", logs, &total_lido);

    EstatisticaSensores stats[1001]; 
    memset(stats, 0, sizeof(stats)); 

    for (int i = 0; i < total_lido; i++){
        int id = logs[i].sensor_id;

        // 1. Contagem global de alertas
        if (strcmp(logs[i].status, "ALERTA") == 0 || strcmp(logs[i].status, "CRITICO") == 0) {
            contadorStatus++;
        }

        // 2. Acumular energia total
        if (strcmp(logs[i].tipo, "energia") == 0) {
            consumoEnergia += logs[i].valor;
        }

        // 3. Estatísticas de Temperatura
        if (strcmp(logs[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id <= 1000) {
                stats[id].soma_total += logs[i].valor;
                stats[id].soma_quadrados += (double)logs[i].valor * logs[i].valor;
                stats[id].contador++;
            }
        }
    }

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;

    printf("\n--- RELATÓRIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
    int impressos = 0;

    for (int j = 0; j <= 1000; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = stats[j].soma_total / stats[j].contador;

            double variancia = (stats[j].soma_quadrados / stats[j].contador) - (stats[j].media * stats[j].media);
            // Evita erro de raiz negativa por imprecisão de float
            stats[j].desvio_padrao = (variancia > 0) ? sqrt(variancia) : 0;

            if (impressos < 10) {
                printf("Sensor %03d | Média: %.2f | Desvio: %.2f\n", j, stats[j].media, stats[j].desvio_padrao);
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
        printf("Sensor mais instável: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio);
    }

    free(logs);
    return 0;
}