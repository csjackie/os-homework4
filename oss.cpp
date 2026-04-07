#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <ctime>
#include <queue>
#include <cstring>
#include <fstream>

// Structs
struct SimulatedClock {
        unsigned int seconds;
        unsigned int nanoseconds;
};

struct PCB {
	int occupied;
	pid_t pid;
	int startSeconds;
	int startNano;
	int serviceTimeSeconds;
	int serviceTimeNano;
	int eventWaitSec;
	int eventWaitNano;
	int blocked;
};

struct msgbuffer {
	long mtype;
	int quantum;
};

// Globals
std::string filename = "log.txt";

void signal_handler(int sig) {
	std::cout << "Caught signal " << sig << ", terminating...\n";
	exit(1);
}

// helpers
void printQueue(std::queue<int> q, std::ofstream &logFile) {
	logFile << "Ready queue [ ";
	while (!q.empty()) {
		logFile << "P" << q.front() << " ";
		q.pop();
	}
	logFile << "]\n";
}

void printProcessTable(PCB table[], std::ofstream &logFile) {
	logFile << "Process Table:\n";
	for (int i = 0; i < 20; i++) {
		if (table[i].occupied) {
			logFile << "P" << i
				<< " PID:" << table[i].pid
				<< " Blocked:" << table[i].blocked
				<< "\n";
		}
	}
}

// Main function
int main(int argc, char **argv) {

        // Default values for command line parameters
        int n = 1;
        int s = 1;
        float t = 1;
        float i = 1;

        signal(SIGALRM, signal_handler);
        alarm(3);

	int opt;
        // parse command line arguments
	while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
                switch (opt) {
                        case 'h':
                                std::cout << "To run program:\n\t ./oss -n # -s # -t # -i # -f file name\n";
                                return 0;

                        // Total number of children to launch
                        case 'n': n = atoi(optarg); break;
                        // Maximum simultaneous children 
                        case 's': s = atoi(optarg); break;
                        // Maximum time children ran before termination
                        case 't': t = atof(optarg); break;
                        // Allowed time between children launched
                        case 'i': i = atof(optarg); break;
			// logfile
			case 'f': filename = optarg; break;
                }
        }

        // prints error message and exits program if the value of n, s, or t are out of range
        if (n <= 0 || n > 20 || s <= 0 || s > n || t <= 0 || i < 0) {
                std::cout << "Invalid argument values\n";
                exit(1);
        }

	std::ofstream logFile(filename);
	if(!logFile) {
		perror("log file failed");
		exit(1);
	}

	// shared memory
	int shmid = shmget(IPC_PRIVATE, sizeof(SimulatedClock), IPC_CREAT | 0666);
	if (shmid == -1) {
    		perror("shmget failed");
    		exit(1);
	}

	SimulatedClock *simClock = (SimulatedClock *)shmat(shmid, nullptr, 0);
	if (simClock == (void *) -1) {
    		perror("shmat failed");
    		exit(1);
	}

	simClock->seconds = 0;
	simClock->nanoseconds = 0;

	// message queue
	key_t key = ftok(".", 'A');
	int msqid = msgget(key, IPC_CREAT | 0666);
	if (msqid == -1) {
		perror("msgget failed");
		exit(1);
	}

	// create process control table and queue
	PCB processTable[20];
	for (int j = 0; j < 20; j++) {
    		processTable[j].occupied = 0;
	}

	std::queue<int> readyQueue;

	// variables
	int activeChildren = 0;
	int totalLaunched = 0;
	int totalFinished = 0;
	const int QUANTUM = 25000000;

	unsigned int lastPrintSec = 0;
	unsigned int lastPrintNano = 0;
	
	// main while loop
	while (totalLaunched < n || activeChildren < 0) {
		// Launch new children if allowed
		if (totalLaunched < n && activeChildren < s) {
			int index = -1;
			for (int j = 0; j < 20; j++) {
				if (processTable[j].occupied == 0) {
					index = j;
					break;
				}
			}

			if (index != -1) {
				pid_t pid = fork();
			
				if (pid == -1) {
					perror("fork failed");
					exit(1);
				}

				if (pid == 0) {
					execl("./worker", "./worker", nullptr);
					perror("execl failed");
					exit(1);
				}

				// PARENT
				processTable[index].occupied = 1;
				processTable[index].pid = pid;
				processTable[index].blocked = 0;
				processTable[index].startSeconds = simClock->seconds;
				processTable[index].startNano = simClock->nanoseconds;

				readyQueue.push(index);
			
				logFile << "OSS: Generated P: " << index
					<< " PID:" << pid
					<< " at time " << simClock->seconds
					<< ":" << simClock->nanoseconds << "\n";	

				totalLaunched++;
				activeChildren++;
			}
		}

		// schedule
		if (!readyQueue.empty()) {

			printQueue(readyQueue, logFile);

			int index = readyQueue.front();
			readyQueue.pop();
			pid_t pid = processTable[index].pid;

			logFile << "OSS: Dispatching P:"  << index
				<< " PID:" << pid << "\n";

			// send message (dispatch)
			msgbuffer msg;
			msg.mtype = pid;
			msg.quantum = QUANTUM;

			if (msgsnd(msqid, &msg, sizeof(int), 0) == -1) {
				perror("msgsnd failed");
				exit(1);
			}

			// receive response
			msgbuffer response;
			if (msgrcv(msqid, &response, sizeof(int), pid, 0) == -1) {
				perror("msgrcv failed");
				exit(1);
			}

			int timeUsed = response.quantum;
			
			logFile << "OSS: P" << index << " ran for "
				<< abs(timeUsed) << " ns\n";
			
			// update clock
			simClock->nanoseconds += abs(timeUsed);
			while (simClock->nanoseconds >= 1000000000) {
				simClock->seconds++;
				simClock->nanoseconds -= 1000000000;
			}

			// process behavior
			if (timeUsed < 0) {
				// terminated
				processTable[index].occupied = 0;
				activeChildren--;
				totalFinished++;
			}
			else if (timeUsed < QUANTUM) {
				processTable[index].blocked = 1;
				processTable[index].eventWaitSec = simClock->seconds;
				processTable[index].eventWaitNano = simClock->nanoseconds + 100000000;
				if (processTable[index].eventWaitNano >= 1000000000) {
					processTable[index].eventWaitSec++;
					processTable[index].eventWaitNano -= 1000000000;
				}
			}

			else {
				readyQueue.push(index);
			}
		}

		// check blocked processes and unblock
		for (int j = 0; j < 20; j++) {
			if (processTable[j].occupied && processTable[j].blocked) {
				if (simClock->seconds > processTable[j].eventWaitSec ||
					(simClock->seconds == processTable[j].eventWaitSec &&
					simClock->nanoseconds >= processTable[j].eventWaitNano)) {
					processTable[j].blocked = 0;
					readyQueue.push(j);
				}
			}
		}

		// print every 0.5 seconds
		if ((simClock->seconds > lastPrintSec) ||
				(simClock->seconds == lastPrintSec &&
				 simClock->nanoseconds - lastPrintNano >= 5000000000)) {

			printProcessTable(processTable, logFile);
			lastPrintSec = simClock->seconds;
			lastPrintNano = simClock->nanoseconds;
		}

		// if nothing ready then advance clock
		if (readyQueue.empty()) {
			simClock->nanoseconds += 1000;
			if (simClock->nanoseconds >= 1000000000) {
				simClock->seconds++;
				simClock->nanoseconds -= 1000000000;
			}
		}
	}

	// Cleanup
	shmdt(simClock);
	shmctl(shmid, IPC_RMID, nullptr);
	msgctl(msqid, IPC_RMID, nullptr);

	logFile.close();

	return 0;
};
