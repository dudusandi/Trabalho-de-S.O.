#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>   //Unix
#include <sys/wait.h> //Unix
#include <sys/shm.h>  //Memoria Compartilhada
#include <sys/sem.h>  //Semaforos


//Definições 
#define NUM_PISTAS 10
#define MAX_CARROS 100
#define VEICULOS_FLUXO_BAIXO 30
#define VEICULOS_FLUXO_MEDIO 60
#define VEICULOS_FLUXO_ALTO 90
#define MAX_CONGESTIONAMENTO 10000  // Distancia de Congestionamento em metros


//Struct
typedef struct {
    int id;
    int conta_carro;
} Pista;

//Struct
typedef struct {
    int id;
    int pista_id;
} Carro;

//Variaveis e Ponteiros
Pista* pistas;
int* ponte_fechada;
int* contador_carros;
int* simulacao_ativa;
int taxa_fluxo = VEICULOS_FLUXO_BAIXO;  //Ajustar conforme o Define
int shm_id_pistas, shm_id_ponte, shm_id_contador, shm_id_simulacao;
int sem_id_pistas, sem_id_ponte, sem_id_contador;

//Operaçoes de Lock e Unlock
void sem_lock(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = -1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

//Operaçoes de Lock e Unlock
void sem_unlock(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = 1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

//Criação das pistas usando Define
void inicia_pistas() {
    for (int i = 0; i < NUM_PISTAS; i++) {
        pistas[i].id = i;          //Variavel da Pista
        pistas[i].conta_carro = 0; //Contador de Carros
    }
}


//Simula a entrada e saida de um carro na pista. Se a pista estiver congestionada, o carro espera.
//Também verifica se a ponte está fechada e espera até que esteja aberta para atravessar.

void carro_process(int carro_id, int pista_id) {
    Pista* pista = &pistas[pista_id];         //Ponteiros para Pistas
    sem_lock(sem_id_pistas, pista_id);        //Controle de Semaforo (Fechado)
    while (pista->conta_carro >= MAX_CONGESTIONAMENTO) { // Condição para o Maximo de Transito
        sem_unlock(sem_id_pistas, pista_id);  //Controle de Semaforo (Aberto)
        usleep(100000);                       // Espera um pouco antes de verificar novamente
        sem_lock(sem_id_pistas, pista_id);    //Controle de Semaforo (Fechado)
    }

    
    pista->conta_carro++; //Contador de Carros
    printf("Carro %d entrou na pista %d (Carros na Pista: %d)\n", carro_id, pista_id, pista->conta_carro); //Sauda de Dados
    sem_unlock(sem_id_pistas, pista_id);  //Controle de Semaforo (Aberto)
    
    
    // Simula passar na ponte
    sem_lock(sem_id_ponte, 0);          //Controle de Semaforo (Fechado)
    while (*ponte_fechada) {
        sem_unlock(sem_id_ponte, 0);    //Controle de Semaforo (Aberto)
        usleep(100000);                 // Espera um pouco antes de verificar novamente
        sem_lock(sem_id_ponte, 0);      //Controle de Semaforo (Fechado)
    }
    sem_unlock(sem_id_ponte, 0); //Controle de Semaforo (Aberto)

    sleep(1);  // Simula o tempo de atravessar a ponte

    sem_lock(sem_id_pistas, pista_id);   //Controle de Semaforo (Fechado)
    pista->conta_carro--;                // Contador de Carros que sairam da pista
    printf("Carro %d saiu da pista %d (Carros nessa Pista %d)\n", carro_id, pista_id, pista->conta_carro);
    sem_unlock(sem_id_pistas, pista_id); //Controle de Semaforo (Aberto)

    exit(0);
}

// Monitor de carros TOTAIS na ponte
void monitor_process() {
    while (*simulacao_ativa) {
        sleep(2);                                   // Intervalo de 2 segundos para monitorar
        int total_carros = 0;                       //Contador de Carros
        for (int i = 0; i < NUM_PISTAS; i++) {
            sem_lock(sem_id_pistas, i);             //Controle de Semaforo (Fechado)
            total_carros += pistas[i].conta_carro;  //Contagem de carros por pista
            sem_unlock(sem_id_pistas, i);           //Controle de Semaforo (Aberto)
        }
        //Saida de Dados
        printf("Total de carros acumulados: %d\n", total_carros);
    }
    exit(0);
}

//Controle de Fluxo
void controle_fluxo_process() {
    while (*simulacao_ativa) {
        sem_lock(sem_id_ponte, 0);      //Controle de Semaforo (Fechado)
        *ponte_fechada = 0;             // Abre a ponte
        sem_unlock(sem_id_ponte, 0);    //Controle de Semaforo (Aberto)
        sleep(60 / taxa_fluxo);         // Tempo para permitir que carros passem
        sem_lock(sem_id_ponte, 0);      //Controle de Semaforo (Fechado)
        *ponte_fechada = 1;             // Fecha a ponte
        sem_unlock(sem_id_ponte, 0);    //Controle de Semaforo (Aberto)
        sleep(2);                       // Mantém a ponte fechada por 2 segundos
    }
    exit(0);
}

int main() {
    // Criaçao de segmentos de memoria shmget
    shm_id_pistas = shmget(IPC_PRIVATE, NUM_PISTAS * sizeof(Pista), IPC_CREAT | 0666);
    shm_id_ponte = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_contador = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_simulacao = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    
    // Inserção dos segmentos na memoria shmat
    pistas = (Pista*)shmat(shm_id_pistas, NULL, 0);
    ponte_fechada = (int*)shmat(shm_id_ponte, NULL, 0);
    contador_carros = (int*)shmat(shm_id_contador, NULL, 0);
    simulacao_ativa = (int*)shmat(shm_id_simulacao, NULL, 0);

    *ponte_fechada = 1;     // Ponte começa fechada
    *contador_carros = 0;   // Contador de carros zerado
    *simulacao_ativa = 1;   // Iniciar Simulação

    //Criação dos semaforos usando semget
    sem_id_pistas = semget(IPC_PRIVATE, NUM_PISTAS, IPC_CREAT | 0666);
    sem_id_ponte = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    sem_id_contador = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    
    //Controlador de Operaçoes de semaforos semctl
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


    //Necessario dependente o compilador e a memoria disponivel 
    //wait(NULL);
    //wait(NULL);
    
    // Limpar Memoria
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
