#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define NUM_PISTAS 10
#define MAX_CARROS 100
#define VEICULOS_FLUXO_BAIXO 30
#define VEICULOS_FLUXO_MEDIO 60
#define VEICULOS_FLUXO_ALTO 90
#define MAX_CONGESTIONAMENTO 10000  // Distancia de Congestionamento em metros

typedef struct {
    int id;
    int conta_carro;
} Pista;

typedef struct {
    int id;
    int pista_id;
} Carro;

Pista* pistas;
int* ponte_fechada;
int* contador_carros;
int* simulacao_ativa;
int taxa_fluxo = VEICULOS_FLUXO_BAIXO;  //Ajustar conforme o Define
int shm_id_pistas, shm_id_ponte, shm_id_contador, shm_id_simulacao;
int sem_id_pistas, sem_id_ponte, sem_id_contador;

void sem_lock(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = -1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

void sem_unlock(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = 1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

void inicia_pistas() {
    for (int i = 0; i < NUM_PISTAS; i++) {
        pistas[i].id = i;
        pistas[i].conta_carro = 0;
    }
}

void carro_process(int carro_id, int pista_id) {
    Pista* pista = &pistas[pista_id];

    sem_lock(sem_id_pistas, pista_id);
    while (pista->conta_carro >= MAX_CONGESTIONAMENTO) {
        sem_unlock(sem_id_pistas, pista_id);
        usleep(100000);  // Espera um pouco antes de verificar novamente
        sem_lock(sem_id_pistas, pista_id);
    }

    pista->conta_carro++;
    printf("Carro %d entrou na pista %d, total de carros: %d\n", carro_id, pista_id, pista->conta_carro);
    sem_unlock(sem_id_pistas, pista_id);
    
    // Simula passar na ponte
    sem_lock(sem_id_ponte, 0);
    while (*ponte_fechada) {
        sem_unlock(sem_id_ponte, 0);
        usleep(100000);  // Espera um pouco antes de verificar novamente
        sem_lock(sem_id_ponte, 0);
    }
    sem_unlock(sem_id_ponte, 0);

    sleep(1);  // Simula o tempo de atravessar a ponte

    sem_lock(sem_id_pistas, pista_id);
    pista->conta_carro--;
    printf("Carro %d saiu da pista %d, total de carros: %d\n", carro_id, pista_id, pista->conta_carro);
    sem_unlock(sem_id_pistas, pista_id);

    exit(0);
}

void monitor_process() {
    while (*simulacao_ativa) {
        sleep(2);  // Intervalo de 5 segundos para monitorar
        int total_carros = 0;
        for (int i = 0; i < NUM_PISTAS; i++) {
            sem_lock(sem_id_pistas, i);
            total_carros += pistas[i].conta_carro;
            sem_unlock(sem_id_pistas, i);
        }
        printf("Total de carros acumulados: %d\n", total_carros);
    }
    exit(0);
}

void controle_fluxo_process() {
    while (*simulacao_ativa) {
        sem_lock(sem_id_ponte, 0);
        *ponte_fechada = 0;  // Abre a ponte
        sem_unlock(sem_id_ponte, 0);
        sleep(60 / taxa_fluxo);  // Tempo para permitir que carros passem
        sem_lock(sem_id_ponte, 0);
        *ponte_fechada = 1;  // Fecha a ponte
        sem_unlock(sem_id_ponte, 0);
        sleep(2);  // Mantém a ponte fechada por 1 minuto
    }
    exit(0);
}

int main() {
    // Configura memória compartilhada
    shm_id_pistas = shmget(IPC_PRIVATE, NUM_PISTAS * sizeof(Pista), IPC_CREAT | 0666);
    shm_id_ponte = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_contador = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_simulacao = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    pistas = (Pista*)shmat(shm_id_pistas, NULL, 0);
    ponte_fechada = (int*)shmat(shm_id_ponte, NULL, 0);
    contador_carros = (int*)shmat(shm_id_contador, NULL, 0);
    simulacao_ativa = (int*)shmat(shm_id_simulacao, NULL, 0);

    *ponte_fechada = 1;  // Ponte começa fechada
    *contador_carros = 0;
    *simulacao_ativa = 1;  // Simulação ativa

    // Configura semáforos
    sem_id_pistas = semget(IPC_PRIVATE, NUM_PISTAS, IPC_CREAT | 0666);
    sem_id_ponte = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    sem_id_contador = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    for (int i = 0; i < NUM_PISTAS; i++) {
        semctl(sem_id_pistas, i, SETVAL, 1);
    }
    semctl(sem_id_ponte, 0, SETVAL, 1);
    semctl(sem_id_contador, 0, SETVAL, 1);

    inicia_pistas();

    // Cria processos de monitoramento e controle de fluxo
    if (fork() == 0) {
        monitor_process();
    }
    if (fork() == 0) {
        controle_fluxo_process();
    }

    // Cria processos de carros
    for (int i = 0; i < MAX_CARROS; i++) {
        sem_lock(sem_id_contador, 0);
        int carro_id = (*contador_carros)++;
        sem_unlock(sem_id_contador, 0);

        int pista_id = rand() % NUM_PISTAS;  // Aleatoriamente escolhe pista pro carro

        if (fork() == 0) {
            carro_process(carro_id, pista_id);
        }
        // Simula taxa de chegada
        usleep(1000000 / taxa_fluxo);  // Ajustar para diferentes fluxos
    }

    // Espera todos os processos de carros terminarem
    for (int i = 0; i < MAX_CARROS; i++) {
        wait(NULL);

    }

    // Finaliza simulação
    *simulacao_ativa = 0;

    // Espera processos de monitoramento e controle de fluxo terminarem
    wait(NULL);
    wait(NULL);

    // Limpa
    shmdt(pistas);
    shmdt(ponte_fechada);
    shmdt(contador_carros);
    shmdt(simulacao_ativa);
    shmctl(shm_id_pistas, IPC_RMID, NULL);
    shmctl(shm_id_ponte, IPC_RMID, NULL);
    shmctl(shm_id_contador, IPC_RMID, NULL);
    shmctl(shm_id_simulacao, IPC_RMID, NULL);
    semctl(sem_id_pistas, 0, IPC_RMID, 0);
    semctl(sem_id_ponte, 0, IPC_RMID, 0);
    semctl(sem_id_contador, 0, IPC_RMID, 0);

    return 0;
}
