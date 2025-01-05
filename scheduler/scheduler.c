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


volatile int exit_flag = 0; //This is for the father in fcfs to wait but also handle signals

typedef enum {
    NEW,
    RUNNING,
    STOPPED,
    EXITED
} ExecutionStatus;

// Structure to represent a process
typedef struct Process {
    char executableName[256];// Executable file name
    char route[256] ; //Route of the executable
    int pid;                   // Unique process ID
    ExecutionStatus status;    // Process state
    struct timeval entryTime;             // Time of entry into the queue
} Process;

Process *terminatedProcess = NULL;
// Node structure for the queue
typedef struct Node {
    Process *process;           // Process stored in the node
    struct Node *next;         // Pointer to the next node
} Node;

// Queue structure to manage processes
typedef struct Queue {
    Node *front;               // Front node of the queue
    Node *rear;                // Rear node of the queue
} Queue;

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

// Function to check if the queue is empty
int isQueueEmpty(Queue *q) {
    return q->front == NULL;
}

// Function to add a process to the queue
void enqueue(Queue *q, Process* p) {
    Node *newNode = (Node *)malloc(sizeof(Node));
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

// Function to remove a process from the queue
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

void extractExecutableName(const char *path, char *executableName) {
    // Find the position of the last '/'
    const char *lastSlash = strrchr(path, '/');

    // If a '/' was found, the executable name starts after it
    if (lastSlash) {
        strcpy(executableName, lastSlash + 1);
    } else {
        // If no '/' is found, the entire path is the executable name
        strcpy(executableName, path);
    }
}

double timeval_diff(struct timeval *start, struct timeval *end) {
    // Restamos los segundos y los microsegundos
    double sec = (end->tv_sec - start->tv_sec);
    double usec = (end->tv_usec - start->tv_usec);

    // Si los microsegundos son negativos, ajustamos
    if (usec < 0) {
        sec--;
        usec += 1000000;
    }

    // Retornamos el tiempo total en segundos (segundos + microsegundos convertidos a segundos)
    return sec + usec / 1000000.0;
}

void loadProcessesFromFile(const char *filename, Queue *q) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove trailing newline

        Process *newProcess = (Process *)malloc(sizeof(Process));
        if (newProcess == NULL) {
            perror("Failed to allocate memory for process");
            fclose(file);
            exit(EXIT_FAILURE);
        }

        char executableName[256];
        extractExecutableName(line, executableName);

        // Prepare the attributes of the process struct
        strcpy(newProcess->executableName, executableName);
        strcpy(newProcess->route, line);
        newProcess->pid = -1;
        newProcess->status = NEW;

        struct timeval time;
        gettimeofday(&time, NULL);
        newProcess->entryTime = time;

        enqueue(q, newProcess);
        printf("Enqueued process: %s\n", newProcess->executableName);
    }

    fclose(file);
}

void roundRobin(Queue* processes, struct timespec quantum) {
    while (!isQueueEmpty(processes)) {
        Process* currentProcess = dequeue(processes);
        terminatedProcess = currentProcess;
        if (currentProcess->status == NEW) {
            int pid = fork();
            if (pid == 0) {
                printf("Executing process %s for the first time \n", currentProcess->executableName);
                currentProcess->pid = getpid();
                currentProcess->status = RUNNING;
                execl(currentProcess->route, currentProcess->executableName, NULL);
                perror("Execution failed");
                exit(EXIT_FAILURE);
            } else if (pid < 0) {
                perror("Fork failed");
                free(currentProcess);
                return;
            } else {
                nanosleep(&quantum, NULL);
                kill(currentProcess->pid, SIGSTOP);
                if (currentProcess->status == RUNNING) {
                    currentProcess->status = STOPPED;
                }
            }
            enqueue(processes, currentProcess);
        } else if (currentProcess->status == STOPPED || currentProcess->status == RUNNING) {
            kill(currentProcess->pid, SIGCONT);
            currentProcess->status = RUNNING;
            nanosleep(&quantum, NULL);
            kill(currentProcess->pid, SIGSTOP);
            currentProcess->status = STOPPED;
            enqueue(processes, currentProcess);
        } else {
            free(currentProcess);
        }
    }
}

// Manejador de la señal SIGCHLD
void sigchld_handler(int signo) {
    struct timeval finishTime;
    gettimeofday(&finishTime, NULL);
    double totalTime = timeval_diff(&terminatedProcess->entryTime, &finishTime);
    int status;
    waitpid(terminatedProcess->pid, &status, 0);
    terminatedProcess->status = EXITED;
    printf("-----------------------------------------------------\n");
    printf("Process %d finished with code: %d\n", terminatedProcess->pid, status);
    printf("Executable: %s\n", terminatedProcess->executableName);
    printf("Route: %s\n", terminatedProcess->route);
    printf("Time to execute: %.6f\n", totalTime);
    printf("-----------------------------------------------------\n");
    terminatedProcess = NULL;
    exit_flag = 1;
}

void firstComeFirstServe(Queue* processes) {
    Process* currentProc;
    while (!isQueueEmpty(processes)) {
        exit_flag = 0;
        currentProc = dequeue(processes);
        terminatedProcess = currentProc;
        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            free(currentProc);
            return;
        } else if (pid == 0) {
            currentProc->pid = getpid();
            currentProc->status = RUNNING;
            execl(currentProc->route, currentProc->executableName, NULL);
            perror("Execution failed");
            exit_flag = 1;
            exit(EXIT_FAILURE);
        } else {
            while (exit_flag == 0) {
                pause();
            }
        }
    }
}

int main(int argc, char **argv) {
    // Validate the minimum number of arguments
    if (argc < 2) {
        printf("Usage: %s <policy> [quantum] <filename>\n", argv[0]);
        return 1;
    }

    // Validate the scheduling policy
    char *policy = argv[1];
    if (strcmp(policy, "FCFS") != 0 && strcmp(policy, "RR") != 0) {
        printf("Invalid policy name. Use 'FCFS' or 'RR'.\n");
        return 1;
    }

    // Validate the number of arguments based on the policy
    if (strcmp(policy, "RR") == 0 && argc != 4) {
        printf("Usage for RR: %s RR <quantum> <filename>\n", argv[0]);
        return 1;
    }

    if (strcmp(policy, "FCFS") == 0 && argc != 3) {
        printf("Usage for FCFS: %s FCFS <filename>\n", argv[0]);
        return 1;
    }

    // Extract the arguments of the terminal
    char *filename;
    Queue* processQueue = createQueue();
    signal(SIGCHLD, sigchld_handler);

    if (strcmp(policy, "RR") == 0) {
        int quantum = 0;
        quantum = atoi(argv[2]);
        struct timespec quantumTime;

        if (quantum <= 0) {
            printf("Invalid quantum value. It must be a positive integer.\n");
            return 1;
        }
        quantumTime.tv_nsec = quantum;
        quantumTime.tv_sec = 0;
        filename = argv[3];
        loadProcessesFromFile(filename, processQueue);
        roundRobin(processQueue, quantumTime);
    } else if (strcmp(policy, "FCFS") == 0) {
        filename = argv[2];
        loadProcessesFromFile(filename, processQueue);
        firstComeFirstServe(processQueue);
    }

    // Free the queue
    while (!isQueueEmpty(processQueue)) {
        Process* p = dequeue(processQueue);
        free(p);
    }
    free(processQueue);

    return 0;
}


