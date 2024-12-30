#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef enum {
    NEW,
    RUNNING,
    STOPPED,
    FINISHED
} ExecutionStatus;

// Structure to represent a process
typedef struct Process {
    char executableName[256];// Executable file name
    char route[256] ; //Route of the executable
    int pid;                   // Unique process ID
    ExecutionStatus status;    // Process state
    struct timeval entryTime;             // Time of entry into the queue
} Process;

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
Process*  dequeue(Queue *q) {
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

void roundRobin(Queue*processes){

}


void firstComeFirstServe(Queue*processes){
    Process *currentProc;
    while (!isQueueEmpty(processes))
    {
      currentProc=dequeue(processes);
      pid_t pid = fork(); // Crea un nuevo proceso

    if (pid < 0) {
        perror("Fork failed");
        return 1;
    } else if (pid == 0) {
        currentProc->pid=getpid();
        currentProc->status=RUNNING;

        // Ejecutar el programa del atributo 'route'
        execl(currentProc->route, currentProc->executableName, NULL);
        // Si execl falla, imprimir un error y salir
        perror("Execution failed");
        exit(EXIT_FAILURE)
    } else {
        wait(NULL);
        //signaljandler;
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

    //Extract the arguments of th e terminal
    char *filename;
    int quantum = 0;
    Queue* processQueue = createQueue();
    if (strcmp(policy, "RR") == 0) {
        int quantum = 0;
        quantum = atoi(argv[2]);
        if (quantum <= 0) {
            printf("Invalid quantum value. It must be a positive integer.\n");
            return 1;
        }
        filename = argv[3];
        loadProcessesFromFile(filename,processQueue);
        roundRobin(processQueue);

    } else {
        filename = argv[2];
        loadProcessesFromFile(filename,processQueue);



    }



}