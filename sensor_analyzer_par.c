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
#include <math.h>
#include <time.h>
#include <pthread.h>

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


Sensor *logs = NULL;
int total_lido = 0;

EstatisticaSensores stats[MAX_SENSORES];
int contadorStatus = 0;
double  consumoEnergia = 0.0;

pthread_mutex_t mutex; 
// define o pedaço do trabalho que cada thread vai fazer 
typedef struct {
    int inicio;
    int fim;
} ThreadArgs;
// lê o arquivo e preenche o array de logs 
void leituraArquivo(const char *nome_arquivo) {
    FILE *file = fopen(nome_arquivo, "r");
    if (!file) { perror("Erro ao abrir arquivo"); exit(1); }

    char linha[256];
    int  i = 0;
    while (fgets(linha, sizeof(linha), file) && i < MAX_LINHAS) {
        if (sscanf(linha, "sensor_%d %10s %8s %19s %f",
                   &logs[i].sensor_id, logs[i].data, logs[i].hora,
                    logs[i].tipo, &logs[i].valor) == 5) {
            char *ptr = strstr(linha, "status "); //pega o status de ALERTA, CRITICO
            if (ptr) sscanf(ptr, "status %14s", logs[i].status);
            else     logs[i].status[0] = '\0';
            i++;
        }
    }
    total_lido = i;
    fclose(file);
}


void* thread_func(void* arg) {
    ThreadArgs *ta = (ThreadArgs *)arg;

    for (int i = ta->inicio; i < ta->fim; i++) {//cada thread processa só um pedaço do vetor 
        int   id    = logs[i].sensor_id;
        float valor = logs[i].valor;
//já deixa as condições prontas pra facilitar 
        int alerta      = (strcmp(logs[i].status, "ALERTA")      == 0 || strcmp(logs[i].status, "CRITICO")     == 0);
        int energia     = (strcmp(logs[i].tipo,   "energia")     == 0);
        int temperatura = (strcmp(logs[i].tipo,   "temperatura") == 0 && id >= 0 && id < MAX_SENSORES);

        pthread_mutex_lock(&mutex);//trava o acesso as variáveis globais, pra evitar conflito entre threads

        if (alerta)
            contadorStatus++;

        if (energia)
            consumoEnergia += valor;

        if (temperatura) {
            stats[id].soma_total     += valor;
            stats[id].soma_quadrados += (double)valor * valor;
            stats[id].contador++;
        }

        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) { //verifica se passou os argumentos corretos 
        fprintf(stderr, "Uso: %s <num_threads> <arquivo.log>\n", argv[0]);
        return 1;
    }

    int         num_threads  = atoi(argv[1]); //qtdade de threads
    const char *nome_arquivo = argv[2];
//aloca memória pra os logs 
    logs = (Sensor *)malloc((size_t)MAX_LINHAS * sizeof(Sensor));
    if (!logs) { fprintf(stderr, "Memoria insuficiente.\n"); return 1; }

    memset(stats, 0, sizeof(stats));

    printf("Lendo arquivo... Aguarde.\n");

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    leituraArquivo(nome_arquivo);
//inicializa o mutex 
    pthread_mutex_init(&mutex, NULL);

    pthread_t  *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    ThreadArgs *args    = malloc((size_t)num_threads * sizeof(ThreadArgs));

    int bloco  = total_lido / num_threads; //divide o trabalho entre as threads 
    int inicio = 0;

    for (int t = 0; t < num_threads; t++) {
        args[t].inicio = inicio;
        args[t].fim    = (t == num_threads - 1) ? total_lido : inicio + bloco;
        inicio         = args[t].fim;
        pthread_create(&threads[t], NULL, thread_func, &args[t]);//cria as threads
    }
// espera todas terminarem 
    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    pthread_mutex_destroy(&mutex);

    
    float maior_desvio     = -1.0f;
    int   id_mais_instavel = -1;
    
    printf("\n--- RELATORIO DE TEMPERATURA (10 PRIMEIROS) ---\n");
    int impressos = 0;
    for (int j = 0; j < MAX_SENSORES; j++) {
        if (stats[j].contador > 0) {
            stats[j].media = (float)(stats[j].soma_total / stats[j].contador);
            
            double variancia = (stats[j].soma_quadrados / stats[j].contador)
            - ((double)stats[j].media * stats[j].media);
            stats[j].desvio_padrao = (variancia > 0.0001) ? (float)sqrt(variancia) : 0.0f;
            
            if (impressos < 10) {
                printf("Sensor %03d | Media: %.2f | Desvio: %.2f\n",
                       j, stats[j].media, stats[j].desvio_padrao);
                impressos++;
            }

            if (stats[j].desvio_padrao > maior_desvio) { //identifica o mais instável 
                maior_desvio     = stats[j].desvio_padrao;
                id_mais_instavel = j;
            }
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double tempo = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("\n----------------------------------------------\n");
    printf("Threads utilizadas: %d\n", num_threads);
    printf("Total de Alertas: %d\n", contadorStatus);
    printf("Consumo Total de Energia: %.2f Wh\n", consumoEnergia);
    if (id_mais_instavel != -1)
        printf("Sensor mais instavel: sensor_%03d (Desvio: %.2f)\n",
               id_mais_instavel, maior_desvio);
    printf("Tempo de execucao: %.4f segundos\n", tempo);

    free(logs);
    free(threads);
    free(args);
    return 0;
}
