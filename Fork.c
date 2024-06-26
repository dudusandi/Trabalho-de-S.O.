#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>  // Para srand e rand

// Definições
#define NUM_PISTAS 10  // 5 pistas principais e 5 laterais
#define MAX_CARROS 100
#define VEICULOS_FLUXO_BAIXO 30
#define VEICULOS_FLUXO_MEDIO 60
#define VEICULOS_FLUXO_ALTO 90
#define MAX_CONGESTIONAMENTO 10000  // Distância de Congestionamento em metros

// Enumeração para direção das pistas
typedef enum {
    PRINCIPAL_DUPLO,
    PRINCIPAL_SIMPLES,
    LATERAL_DUPLO,
    LATERAL_SIMPLES
} TipoPista;

// Struct da Pista
typedef struct {
    int id;
    int conta_carro;
    TipoPista tipo;
} Pista;

// Struct do Carro
typedef struct {
    int id;
    int pista_id;
    int pista_origem;
} Carro;

// Variáveis e Ponteiros
Pista* pistas;
int* ponte_fechada;
int* contador_carros;
int* simulacao_ativa;
int taxa_fluxo = VEICULOS_FLUXO_BAIXO;  // Ajustar conforme o Define
int shm_id_pistas, shm_id_ponte, shm_id_contador, shm_id_simulacao;
int sem_id_pistas, sem_id_ponte, sem_id_contador;

// Operações de Lock e Unlock
void sem_fecha(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = -1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

void sem_abre(int sem_id, int sem_num) {
    struct sembuf sops;
    sops.sem_num = sem_num;
    sops.sem_op = 1;
    sops.sem_flg = 0;
    semop(sem_id, &sops, 1);
}

// Inicializa as pistas com características diferentes
void inicia_pistas() {
    int pista_id = 0;
    for (int i = 0; i < NUM_PISTAS; i++) {
        pistas[i].id = i;
        pistas[i].conta_carro = 0;
        if (i < NUM_PISTAS / 4) {
            pistas[i].tipo = PRINCIPAL_DUPLO;  // Pistas principais duplas
        } else if (i < NUM_PISTAS / 2) {
            pistas[i].tipo = PRINCIPAL_SIMPLES; // Pistas principais simples
        } else if (i < 3 * NUM_PISTAS / 4) {
            pistas[i].tipo = LATERAL_DUPLO;  // Pistas laterais duplas
        } else {
            pistas[i].tipo = LATERAL_SIMPLES; // Pistas laterais simples
        }
    }
}

// Função para verificar e mudar de pista se houver congestionamento
int verifica_muda_pista(int carro_id, int pista_id) {
    Pista* pista_atual = &pistas[pista_id];

    // Verifica se há congestionamento na pista atual
    if (pista_atual->conta_carro >= MAX_CONGESTIONAMENTO) {
        // Tentar mudar para outra pista disponível
        for (int i = 0; i < NUM_PISTAS; i++) {
            if (i != pista_id) {  // Evita verificar a mesma pista
                sem_fecha(sem_id_pistas, i);
                if (pistas[i].conta_carro < MAX_CONGESTIONAMENTO) {
                    // Realiza a mudança de faixa
                    printf("Carro %d mudou da pista %d para a pista %d devido a congestionamento\n", carro_id, pista_id, i);
                    pista_atual->conta_carro--;
                    sem_abre(sem_id_pistas, pista_id);
                    sem_abre(sem_id_pistas, i);
                    return i;  // Retorna a nova pista para onde o carro mudou
                }
                sem_abre(sem_id_pistas, i);
            }
        }
    }

    return pista_id;  // Mantém na mesma pista se não houver mudança
}

// Processo simulando o comportamento de um carro
void carro_process(int carro_id, int pista_id) {
    Pista* pista = &pistas[pista_id];
    sem_fecha(sem_id_pistas, pista_id);

    while (pista->conta_carro >= MAX_CONGESTIONAMENTO) {
        sem_abre(sem_id_pistas, pista_id);
        usleep(100000);
        sem_fecha(sem_id_pistas, pista_id);
    }

    pista->conta_carro++;
    printf("Carro %d entrou na pista %d (Carros na Pista: %d, Tipo: %s)\n", carro_id, pista_id, pista->conta_carro,
           (pista->tipo == PRINCIPAL_DUPLO ? "IDA Duplo" :
            pista->tipo == PRINCIPAL_SIMPLES ? "IDA Simples" :
            pista->tipo == LATERAL_DUPLO ? "VOLTA Duplo" : "VOLTA Simples"));
    sem_abre(sem_id_pistas, pista_id);

    // Simula a passagem pela ponte
    sem_fecha(sem_id_ponte, 0);
    while (*ponte_fechada) {
        sem_abre(sem_id_ponte, 0);
        usleep(100000);
        sem_fecha(sem_id_ponte, 0);
    }
    sem_abre(sem_id_ponte, 0);

    sleep(1);  // Simula o tempo de atravessar a ponte

    // Verifica e muda de pista se necessário
    int nova_pista_id = verifica_muda_pista(carro_id, pista_id);

    // Se mudou de pista, reinicia o processo na nova pista
    if (nova_pista_id != pista_id) {
        carro_process(carro_id, nova_pista_id);
    } else {
        // Se não mudou de pista, verifica se deve voltar para a pista de origem
        if (pista_id != ((Carro*) shmat(shm_id_pistas, NULL, 0))[carro_id].pista_origem) {
            int pista_origem = ((Carro*) shmat(shm_id_pistas, NULL, 0))[carro_id].pista_origem;
            printf("Carro %d está retornando à pista de origem %d\n", carro_id, pista_origem);
            carro_process(carro_id, pista_origem);
        }
    }

    sem_fecha(sem_id_pistas, pista_id);
    pista->conta_carro--;
    printf("Carro %d saiu da pista %d (Carros nessa Pista %d, Tipo: %s)\n", carro_id, pista_id, pista->conta_carro,
           (pista->tipo == PRINCIPAL_DUPLO ? "IDA Duplo" :
            pista->tipo == PRINCIPAL_SIMPLES ? "IDA Simples" :
            pista->tipo == LATERAL_DUPLO ? "VOLTA Duplo" : "VOLTA Simples"));
    sem_abre(sem_id_pistas, pista_id);

    exit(0);
}

// Processo monitorando o total de carros nas pistas
void monitor_process() {
    while (*simulacao_ativa) {
        sleep(2);
        int total_carros = 0;
        for (int i = 0; i < NUM_PISTAS; i++) {
            sem_fecha(sem_id_pistas, i);
            total_carros += pistas[i].conta_carro;
            sem_abre(sem_id_pistas, i);
        }
        printf("Total de carros acumulados: %d\n", total_carros);
    }
    exit(0);
}

// Processo controlando o fluxo na ponte
void controle_fluxo_process() {
    while (*simulacao_ativa) {
        sem_fecha(sem_id_ponte, 0);
        *ponte_fechada = 0;
        sem_abre(sem_id_ponte, 0);
        sleep(60 / taxa_fluxo);
        sem_fecha(sem_id_ponte, 0);
        *ponte_fechada = 1;
        sem_abre(sem_id_ponte, 0);
        sleep(2);
    }
    exit(0);
}

int main() {
    // Criação de segmentos de memória
    shm_id_pistas = shmget(IPC_PRIVATE, NUM_PISTAS * sizeof(Pista), IPC_CREAT | 0666);
    shm_id_ponte = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_contador = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    shm_id_simulacao = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    // Atribuição dos segmentos à memória compartilhada
    pistas = (Pista*) shmat(shm_id_pistas, NULL, 0);
    ponte_fechada = (int*) shmat(shm_id_ponte, NULL, 0);
    contador_carros = (int*) shmat(shm_id_contador, NULL, 0);
    simulacao_ativa = (int*) shmat(shm_id_simulacao, NULL, 0);

    *ponte_fechada = 1;    // Ponte começa fechada
    *contador_carros = 0;  // Contador de carros zerado
    *simulacao_ativa = 1;  // Iniciar simulação

    // Criação dos semáforos
    sem_id_pistas = semget(IPC_PRIVATE, NUM_PISTAS, IPC_CREAT | 0666);
    sem_id_ponte = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    sem_id_contador = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);

    // Inicialização dos semáforos
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
    srand(time(NULL));  // Inicializa a semente para geração de números aleatórios

    for (int i = 0; i < MAX_CARROS; i++) {
        sem_fecha(sem_id_contador, 0);
        int carro_id = (*contador_carros)++;
        sem_abre(sem_id_contador, 0);

        int pista_id = rand() % NUM_PISTAS;  // Aleatoriamente escolhe pista para o carro
        int pista_origem = pista_id;  // Pista de origem é a escolhida inicialmente

        // Armazena a pista de origem na estrutura do carro
        ((Carro*) shmat(shm_id_pistas, NULL, 0))[carro_id].pista_origem = pista_origem;

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

    // Limpar Memória Compartilhada e Semáforos
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

