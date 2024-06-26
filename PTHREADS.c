#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_PISTAS 10
#define MAX_CARROS 100
#define VEICULOS_FLUXO_BAIXO 30
#define VEICULOS_FLUXO_MEDIO 60
#define VEICULOS_FLUXO_ALTO 90
#define MAX_CONGESTIONAMENTO 10000  // Representa o congestionamento para até 10 km

typedef struct {
    int id;
    int conta_carro;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Pista;

typedef struct {
    int id;
    int pista_id;
} Carro;

Pista pistas[NUM_PISTAS];
int ponte_fechada = 1;  // 1 para fechada, 0 para aberta
int taxa_fluxo = VEICULOS_FLUXO_BAIXO;  // Pode ser ajustado para VEICULOS_FLUXO_BAIXO, VEICULOS_FLUXO_MEDIO, ou VEICULOS_FLUXO_ALTO
int contador_carros = 0;  // Contador global para IDs de carros
pthread_mutex_t ponte_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ponte_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t contador_mutex = PTHREAD_MUTEX_INITIALIZER;

void inicia_pistas() {
    for (int i = 0; i < NUM_PISTAS; i++) {
        pistas[i].id = i;
        pistas[i].conta_carro = 0;
        pthread_mutex_init(&pistas[i].mutex, NULL);
        pthread_cond_init(&pistas[i].cond, NULL);
    }
}

void* carro_thread(void* arg) {
    Carro* carro = (Carro*)arg;
    int pista_id = carro->pista_id;
    Pista* pista = &pistas[pista_id];
    
    pthread_mutex_lock(&pista->mutex);

    while (pista->conta_carro >= MAX_CONGESTIONAMENTO) {
        pthread_cond_wait(&pista->cond, &pista->mutex);
    }

    pista->conta_carro++;
    printf("Carro %d entrou na pista %d, total de carros: %d\n", carro->id, pista_id, pista->conta_carro);

    pthread_mutex_unlock(&pista->mutex);
    
    // Simula passar na ponte
    pthread_mutex_lock(&ponte_mutex);
    while (ponte_fechada) {
        pthread_cond_wait(&ponte_cond, &ponte_mutex);
    }
    pthread_mutex_unlock(&ponte_mutex);

    sleep(1);  // Simula o tempo de atravessar a ponte

    // Tranca a pista de novo para sair
    pthread_mutex_lock(&pista->mutex);

    pista->conta_carro--;
    printf("Carro %d saiu da pista %d, total de carros: %d\n", carro->id, pista_id, pista->conta_carro);

    pthread_cond_signal(&pista->cond);
    pthread_mutex_unlock(&pista->mutex);

    free(carro);  // Libera a memória alocada para o carro

    return NULL;
}

void* monitor_thread(void* arg) {
    while (1) {
        sleep(2);  // Intervalo de 5 segundos para monitorar
        int total_carros = 0;
        for (int i = 0; i < NUM_PISTAS; i++) {
            pthread_mutex_lock(&pistas[i].mutex);
            total_carros += pistas[i].conta_carro;
            pthread_mutex_unlock(&pistas[i].mutex);
        }
        printf("Total de carros acumulados: %d\n", total_carros);
    }
    return NULL;
}

void* controle_fluxo_thread(void* arg) {
    while (1) {
        ponte_fechada = 0;  // Abre a ponte
        pthread_cond_broadcast(&ponte_cond);  // Notifica todos os carros aguardando
        sleep(60 / taxa_fluxo);  // Tempo para permitir que carros passem
        ponte_fechada = 1;  // Fecha a ponte
        sleep(2);  // Mantém a ponte fechada por 1 minuto
    }
    return NULL;
}

int main() {
    // Inicializa pistas
    inicia_pistas();

    // Cria array pra guardar o ID das threads
    pthread_t carro_threads[MAX_CARROS];
    pthread_t monitor_thread_id, controle_fluxo_thread_id;

    // Cria a thread de monitoramento
    if (pthread_create(&monitor_thread_id, NULL, monitor_thread, NULL) != 0) {
        perror("Falha ao criar thread de monitoramento");
    }

    // Cria a thread de controle de fluxo
    if (pthread_create(&controle_fluxo_thread_id, NULL, controle_fluxo_thread, NULL) != 0) {
        perror("Falha ao criar thread de controle de fluxo");
    }

    // Cria as threads de carros
    for (int i = 0; i < MAX_CARROS; i++) {
        pthread_mutex_lock(&contador_mutex);
        int carro_id = contador_carros++;
        pthread_mutex_unlock(&contador_mutex);

        Carro* carro = (Carro*)malloc(sizeof(Carro));
        carro->id = carro_id;
        carro->pista_id = rand() % NUM_PISTAS;  // Aleatoriamente escolhe pista pro carro

        if (pthread_create(&carro_threads[i], NULL, carro_thread, carro) != 0) {
            perror("Falha ao criar thread");
        }
        // Simula taxa de chegada
        usleep(1000000 / taxa_fluxo);  // Ajustar para diferentes fluxos
    }

    // Espera todas as threads terminarem
    for (int i = 0; i < MAX_CARROS; i++) {
        pthread_join(carro_threads[i], NULL);
    }

    // Limpa
    for (int i = 0; i < NUM_PISTAS; i++) {
        pthread_mutex_destroy(&pistas[i].mutex);
        pthread_cond_destroy(&pistas[i].cond);
    }

    pthread_mutex_destroy(&ponte_mutex);
    pthread_cond_destroy(&ponte_cond);
    pthread_mutex_destroy(&contador_mutex);

    return 0;
}
