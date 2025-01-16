#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/wait.h>
#endif
#include <unistd.h>
#include <signal.h>
#include <time.h>

volatile int exit_flag = 0; // Para que el padre en FCFS espere, pero también maneje señales

typedef enum {
    NEW,
    RUNNING,
    STOPPED,
    EXITED
} ExecutionStatus;

// ------------------ Estructuras ------------------

// Representa un proceso
typedef struct Process {
    char executableName[256]; // Nombre del binario (p.ej. "work7")
    char route[256];          // Ruta completa (p.ej. "./work/work7")
    int pid;                  // PID
    ExecutionStatus status;   // Estado
    struct timeval entryTime; // Momento en que se encoló
    int remainingTime;
    
} Process;

// Para usar en FCFS/RR cuando un proceso termina
Process *terminatedProcess = NULL;


// Nodo de la cola
typedef struct Node {
    Process *process;
    struct Node *next;
} Node;

// Cola para gestionar procesos
typedef struct Queue {
    Node *front;
    Node *rear;
} Queue;

// ------------------ Funciones de cola ------------------

Queue* createQueue() {
    Queue *q = (Queue*)malloc(sizeof(Queue));
    if (q == NULL) {
        perror("Failed to allocate memory for queue");
        exit(EXIT_FAILURE);
    }
    q->front = NULL;
    q->rear = NULL;
    return q;
}
Queue* ioQueue;
Queue* processQueue;

int isQueueEmpty(Queue *q) {
    return (q->front == NULL);
}

void enqueue(Queue *q, Process* p) {
    Node *newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }
    newNode->process = p;
    newNode->next = NULL;

    if (isQueueEmpty(q)) {
        q->front = q->rear = newNode;
    } else {
        q->rear->next = newNode;
        q->rear = newNode;
    }
}

Process* dequeue(Queue *q) {
    if (isQueueEmpty(q)) {
        fprintf(stderr, "Error: Queue is empty\n");
        exit(EXIT_FAILURE);
    }
    Node *temp = q->front;
    Process *p = temp->process;
    q->front = q->front->next;
    if (q->front == NULL) {
        q->rear = NULL;
    }
    free(temp);
    return p;
}
// Function to find a process by PID in the queue
Process* findProcessByPid(Queue *q, int pid) {
    Node *current = q->front;
    while (current != NULL) {
        if (current->process->pid == pid) {
            return current->process;
        }
        current = current->next;
    }
    return NULL; // Return NULL if the process is not found
}

// ------------------ Funciones auxiliares ------------------

void extractExecutableName(const char *path, char *executableName) {
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash) {
        strcpy(executableName, lastSlash + 1);
    } else {
        strcpy(executableName, path);
    }
}

double timeval_diff(struct timeval *start, struct timeval *end) {
    double sec = (end->tv_sec - start->tv_sec);
    double usec = (end->tv_usec - start->tv_usec);

    if (usec < 0) {
        sec--;
        usec += 1000000;
    }
    return sec + usec / 1000000.0;
}

// Carga procesos desde un archivo
void loadProcessesFromFile(const char *filename, Queue *q) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Quitar el salto de línea

        Process *newProcess = (Process*)malloc(sizeof(Process));
        if (!newProcess) {
            perror("Failed to allocate memory for process");
            fclose(file);
            exit(EXIT_FAILURE);
        }

        // route contendrá algo como "./work/work7"
        strcpy(newProcess->route, line);
        // extraer solo "work7"
        extractExecutableName(line, newProcess->executableName);

        newProcess->pid = -1;
        newProcess->status = NEW;
        gettimeofday(&newProcess->entryTime, NULL);
        newProcess->remainingTime = 0; // Por defecto

        enqueue(q, newProcess);
        printf("Enqueued process: %s\n", newProcess->executableName);
    }

    fclose(file);
}

// ------------------ Handlers ------------------

void sigchld_handler(int signo) {
    (void)signo;
    if (terminatedProcess == NULL) {
        // A veces puede llegar SIGCHLD sin que hayamos asignado terminatedProcess
        // en FCFS, etc. Depende de la lógica. Lo ignoramos.
        return;
    }

    struct timeval finishTime;
    gettimeofday(&finishTime, NULL);

    double totalTime = timeval_diff(&terminatedProcess->entryTime, &finishTime);
    int status;
    // Esperar al proceso que terminó
    waitpid(terminatedProcess->pid, &status, 0);
    terminatedProcess->status = EXITED;

    // Mensajes de estilo FCFS
    printf("-----------------------------------------------------\n");
    printf("Process %d finished with code: %d\n", terminatedProcess->pid, WEXITSTATUS(status));
    printf("Executable: %s\n", terminatedProcess->executableName);
    printf("Route: %s\n", terminatedProcess->route);
    printf("Time to execute: %.6f\n", totalTime);
    printf("-----------------------------------------------------\n");
    free(terminatedProcess);
    terminatedProcess = NULL;
    exit_flag = 1;
}
void sigUsr1_handler(int sign) {
    printf("Starting I/O routine\n");
    enqueue(ioQueue,terminatedProcess);
    exit_flag = 1;
    
}
void sigUsr2_handler(int sign, siginfo_t* info, void* context) {
    pid_t sender_pid = info->si_pid; // Obtener el PID del proceso que envió SIGUSR2
    printf("Process with  PID %d finished the I/O routine \n", sender_pid);
    //Also you can just deque because it is a FIFO but i think its risky 
    Process* proc = dequeue(ioQueue);
    proc->status = STOPPED;
    printf("...\n");	 
    enqueue(processQueue,proc);
}


// ------------------ FCFS ------------------
void firstComeFirstServe(Queue* processes) {
    Process* currentProc;

    while (!isQueueEmpty(processes)) {
        exit_flag = 0;
        currentProc = dequeue(processes);
        terminatedProcess = currentProc;

        if (currentProc->status == STOPPED) {
            kill(currentProc->pid, SIGCONT);
        } else {
            
            pid_t pid = fork();
            if (pid < 0) {
                perror("Fork failed");
                free(currentProc);
                return;
            } else if (pid == 0) {
               
                currentProc->pid = getpid();
                currentProc->status = RUNNING;
                execlp(currentProc->route, currentProc->executableName, NULL);
                perror("Execution failed");
                exit(EXIT_FAILURE);
            } 
        }
        while (exit_flag == 0) {
            pause();
        }
    }
}
//-------------------RR---------------------
void roundRobin(Queue* q, int quantum) {
    while (!isQueueEmpty(q)) {
        Process* p = dequeue(q);
        if (!p) {
            continue;
        }

        // Si no se ha iniciado nunca, lo lanzamos
        if (p->pid == -1) {
            if (p->remainingTime <= 0) {
                p->remainingTime = 5000; // Ej. 5s
            }
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                free(p);
                continue;
            } else if (pid == 0) {
                // Hijo
                p->pid = getpid();
                p->status = RUNNING;
                execlp(p->route, p->executableName, NULL);
                perror("execlp failed");
                exit(EXIT_FAILURE);
            } else {
                // Padre
                p->pid = pid;
                p->status = RUNNING;
                printf("Started process: %s (PID: %d)\n", p->executableName, p->pid);
            }
        } else {
            // Ya existía
            printf("Resuming process: %s (PID: %d)\n", p->executableName, p->pid);
            kill(p->pid, SIGCONT);
            p->status = RUNNING;
        }

        // Esperamos quantum (simulado) revisando periódicamente
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int elapsed = 0;
        int finished = 0;

        while (elapsed < quantum) {
            // Dormimos 1ms
            struct timespec ts = {0, 1000000L};
            nanosleep(&ts, NULL);

            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (int)((now.tv_sec - start.tv_sec) * 1000 +
                            (now.tv_nsec - start.tv_nsec) / 1000000);

            // Comprobamos si ya terminó
            int status;
            pid_t res = waitpid(p->pid, &status, WNOHANG);
            if (res > 0) {
                // El proceso terminó o cambió de estado
                // Imitamos la misma salida que FCFS
                struct timeval finishTime;
                gettimeofday(&finishTime, NULL);
                double totalTime = timeval_diff(&p->entryTime, &finishTime);

                printf("-----------------------------------------------------\n");
                printf("Process %d finished with code: %d\n", p->pid, WEXITSTATUS(status));
                printf("Executable: %s\n", p->executableName);
                printf("Route: %s\n", p->route);
                printf("Time to execute: %.6f\n", totalTime);
                printf("-----------------------------------------------------\n");

                free(p);
                p = NULL;
                finished = 1;
                break;
            }
        }

        // Si no terminó, pausamos
        if (!finished && p != NULL) {
            // Verificamos una última vez
            int status;
            pid_t res = waitpid(p->pid, &status, WNOHANG);
            if (res > 0) {
                // Terminó justo al final
                struct timeval finishTime;
                gettimeofday(&finishTime, NULL);
                double totalTime = timeval_diff(&p->entryTime, &finishTime);

                printf("-----------------------------------------------------\n");
                printf("Process %d finished with code: %d\n", p->pid, WEXITSTATUS(status));
                printf("Executable: %s\n", p->executableName);
                printf("Route: %s\n", p->route);
                printf("Time to execute: %.6f\n", totalTime);
                printf("-----------------------------------------------------\n");

                free(p);
                p = NULL;
                continue;
            }

            // Aún sigue corriendo, lo pausamos
            printf("Pausing process: %s (PID: %d)\n", p->executableName, p->pid);
            kill(p->pid, SIGSTOP);
            p->status = STOPPED;

            // Descontamos su quantum
            p->remainingTime -= quantum;
            if (p->remainingTime > 0) {
                // Volvemos a encolarlo
                enqueue(q, p);
            } else {
                // Se agotó su tiempo total, lo matamos y mostramos info
                kill(p->pid, SIGKILL);
                waitpid(p->pid, NULL, 0);

                struct timeval finishTime;
                gettimeofday(&finishTime, NULL);
                double totalTime = timeval_diff(&p->entryTime, &finishTime);

                printf("-----------------------------------------------------\n");
                printf("Process %d finished with code: %d\n", p->pid, 0); // 0 = Killed?
                printf("Executable: %s\n", p->executableName);
                printf("Route: %s\n", p->route);
                printf("Time to execute: %.6f\n", totalTime);
                printf("-----------------------------------------------------\n");

                free(p);
                p = NULL;
            }
        }
    }
}

// ------------------ main ------------------

int main(int argc, char **argv) {
    // Validaciones mínimas
    if (argc < 2) {
        printf("Usage: %s <policy> [quantum] <filename>\n", argv[0]);
        return 1;
    }

    char *policy = argv[1];
    if (strcmp(policy, "FCFS") != 0 && strcmp(policy, "RR") != 0) {
        printf("Invalid policy name. Use 'FCFS' or 'RR'.\n");
        return 1;
    }

    if (strcmp(policy, "RR") == 0 && argc != 4) {
        printf("Usage for RR: %s RR <quantum> <filename>\n", argv[0]);
        return 1;
    }
    if (strcmp(policy, "FCFS") == 0 && argc != 3) {
        printf("Usage for FCFS: %s FCFS <filename>\n", argv[0]);
        return 1;
    }

    // Creamos la cola y asignamos handler
    processQueue = createQueue();
    ioQueue = createQueue();

    signal(SIGCHLD, sigchld_handler);
    signal(SIGUSR1, sigUsr1_handler);
    struct sigaction  sa_usr2;
    // Configuración para SIGUSR2 (usando sigaction para obtener más control)
    sa_usr2.sa_sigaction = sigUsr2_handler;  // Usamos la versión con siginfo_t para obtener información adicional
    sigemptyset(&sa_usr2.sa_mask);           // No bloquear señales durante la ejecución del handler
    sa_usr2.sa_flags = SA_SIGINFO;           // Especificamos que usamos siginfo_t
    if (sigaction(SIGUSR2, &sa_usr2, NULL) == -1) {
        perror("Error al configurar SIGUSR2");
        exit(EXIT_FAILURE);
    }

 
    if (strcmp(policy, "RR") == 0) {
        int quantum = atoi(argv[2]);
        if (quantum <= 0) {
            printf("Invalid quantum value. Must be positive.\n");
            return 1;
        }
        char *filename = argv[3];
        loadProcessesFromFile(filename, processQueue);
        roundRobin(processQueue, quantum);
    } else {
        // FCFS
       
        char *filename = argv[2];
        loadProcessesFromFile(filename, processQueue);
        firstComeFirstServe(processQueue);
    }

    // Liberamos la cola
    while (!isQueueEmpty(processQueue)) {
        Process* p = dequeue(processQueue);
        free(p);
    }
    free(processQueue);
    free(ioQueue);

    return 0;
}
