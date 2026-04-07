/*
gcc -O2 sensor_analyzer_seq.c -o sensor_analyzer_seq -lpthread -lm
./sensor_analyzer_seq

Felipe Haddad - 10437372
Arthur Roldan - 10353847
Beatriz Nobrega - 10435789
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_LINHAS 11000000 //limite maximo de linhas do arquivo 
#define MAX_SENSORES 1001 //qtd máxima de sensores 

typedef struct {
    int sensor_id;
    char data[11];
    char hora[9];
    char tipo[20];
    float valor;
    char status[15];
} Sensor;
// guarda os cálculos que a gente vai fazer pra cada sensor 
typedef struct {
    double soma_total;
    double soma_quadrados;
    int contador;
    float media;
    float desvio_padrao;
} EstatisticaSensores;

EstatisticaSensores stats[MAX_SENSORES];//array global pra guardar tudo  
//lê o arquivo e joga os dados no array 
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
            // pega o status 
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

int main() {
    int total_lido = 0;
    int contadorStatus = 0;
    double consumoEnergia = 0;
    struct timespec t0, t1;  // <- CLOCK_MONOTONIC
// aloca memória pra guardar todos os logs 
    Sensor *logs = (Sensor *) malloc(MAX_LINHAS * sizeof(Sensor));
    if (logs == NULL) {
        printf("Erro: Memoria insuficiente para alocar o array de logs.\n");
        return 1;
    }

    printf("Lendo arquivo... Aguarde.\n");
    clock_gettime(CLOCK_MONOTONIC, &t0);  // <- inicio, começa a contar o tempo 
    
    leituraArquivo("sensores.log", logs, &total_lido);
    
    memset(stats, 0, sizeof(stats)); //zera tudo antes de começar os calculos 

    for (int i = 0; i < total_lido; i++) {// percorre todos os logs 
        int id = logs[i].sensor_id;

        if (strcmp(logs[i].status, "ALERTA") == 0 || strcmp(logs[i].status, "CRITICO") == 0) { //conta quantos ALERTA ou CRITICO apareceram
            contadorStatus++;
        }

        if (strcmp(logs[i].tipo, "energia") == 0) {
            consumoEnergia += logs[i].valor;
        }

        if (strcmp(logs[i].tipo, "temperatura") == 0) {
            if (id >= 0 && id < MAX_SENSORES) {
                stats[id].soma_total += logs[i].valor;
                stats[id].soma_quadrados += (double)logs[i].valor * logs[i].valor;
                stats[id].contador++;
            }
        }
    }

    float maior_desvio = -1.0;
    int id_mais_instavel = -1;

    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
    int impressos = 0;
    for (int j = 0; j < MAX_SENSORES; j++) { //calcula media e desvio padrão
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador); //média basica 
            
            double variancia = (stats[j].soma_quadrados / stats[j].contador) - (stats[j].media * stats[j].media);//desvio padrão 
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;

            if (impressos < 10) { //imprime só os primeiros 10 
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n", j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }

            if (stats[j].desvio_padrao > maior_desvio) { // guarda qual sensot é mais instável
                maior_desvio = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }
// termina de contar o tempo 
    clock_gettime(CLOCK_MONOTONIC, &t1);  // <- fim
    double tempo = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;  // <- wall time

    printf("\n----------------------------------------------\n");
    printf("Total de Alertas: %d\n", contadorStatus); 
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia); 
    if (id_mais_instavel != -1) {
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n", id_mais_instavel, maior_desvio);
    }
    printf("Tempo de execucao: %.4f segundos\n", tempo);  // <- mesmo formato do paralelo

    free(logs); // libera memória 
    return 0;
}