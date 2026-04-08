#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <queue>
#include <fstream>
#include <cstring>

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
	int eventWaitSec;
	int eventWaitNano;
	int blocked;
	int totalCPU;
};

struct msgbuffer {
	long mtype;
	int quantum;
};

// Globals
std::string filename = "log.txt";
int shmid, msqid;
SimulatedClock* simClock = nullptr;

void cleanup() {
	if (simClock) shmdt(simClock);
	shmctl(shmid, IPC_RMID, nullptr);
	msgctl(msqid, IPC_RMID, nullptr);
}

void signal_handler(int sig) {
	std::cout << "Caught signal, terminating...\n";
	kill(0, SIGTERM);
	usleep(100000);
	cleanup();
	exit(1);
}

void incrementClock(int ns) {
	simClock->nanoseconds += ns;
	while (simClock->nanoseconds >= 1000000000) {
		simClock->seconds++;
		simClock->nanoseconds -= 1000000000;
	}
}

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
            		logFile << "P" << i << " PID:" << table[i].pid
                    		<< " Blocked:" << table[i].blocked << "\n";
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
        if (n <= 0 || n > 20 || s <= 0 || s > n || t <= 0) {
                std::cout << "Invalid argument values\n";
                exit(1);
        }

	signal(SIGALRM, signal_handler);
	alarm(3);

	std::ofstream logFile(filename);
	
	// shared memory
	shmid = shmget(IPC_PRIVATE, sizeof(SimulatedClock), IPC_CREAT | 0666);
	simClock = (SimulatedClock*)shmat(shmid, nullptr, 0);
	simClock->seconds = 0;
	simClock->nanoseconds = 0;

	// message queue
	msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

	// create process control table and queue
	PCB table[20] = {};
	std::queue<int> readyQueue;

	// variables
	int activeChildren = 0;
	int totalLaunched = 0;
	const int QUANTUM = 25000000;

	unsigned int lastPrintSec = 0;
	unsigned int lastPrintNano = 0;

	unsigned int nextLaunchSec = 0;
	unsigned int nextLaunchNano = 0;	
	int logLines = 0;
	
	// main while loop
	while (totalLaunched < n || activeChildren > 0) {
		// Launch new children if allowed
		if (totalLaunched < n && activeChildren < s) {
			if (simClock->seconds > nextLaunchSec ||
				(simClock->seconds == nextLaunchSec &&
			 	simClock->nanoseconds >= nextLaunchNano)) {
		       		
				int idx = -1;
				
				for (int j = 0; j < 20; j++) {
					if (!table[j].occupied) {
						idx = j;
						break;
					}
				}
				
				if (idx != -1) {
					pid_t pid = fork();

					if (pid == 0) {
						char msqidStr[16], maxStr[16];
						sprintf(msqidStr, "%d", msqid);
						int maxCPU = 1 + rand() % (int)(t * 1e9);
						sprintf(maxStr, "%d", maxCPU);

						execl("./worker", "./worker", msqidStr, maxStr, nullptr);
						exit(1);
					}

					table[idx].occupied = 1;
					table[idx].pid = pid;
					table[idx].blocked = 0;
					table[idx].totalCPU = 0;

					readyQueue.push(idx);
					
					logFile << "OSS: Created P" << idx << " PID " << pid << "\n";
					std::cout << "Created P" << idx << " PID " << pid << "\n";

					totalLaunched++;
					activeChildren++;

					nextLaunchNano = simClock->nanoseconds + (i * 1e9);
					nextLaunchSec = simClock->seconds;
					if (nextLaunchNano >= 1000000000) {
						nextLaunchSec++;
						nextLaunchNano -= 1000000000;
					}
				}

			}
		}

		// check blocked processes and unblock
		for (int j = 0; j < 20; j++) {
			if (table[j].occupied && table[j].blocked) {
				if (simClock->seconds > table[j].eventWaitSec ||
					(simClock->seconds == table[j].eventWaitSec &&
					simClock->nanoseconds >= table[j].eventWaitNano)) {
					table[j].blocked = 0;
					readyQueue.push(j);
				}
			}
		}

		if (!readyQueue.empty()) {

			printQueue(readyQueue, logFile);

			int idx = readyQueue.front();

			if (!table[idx].occupied) {
				readyQueue.pop();
				continue;
			}

			if (table[idx].blocked) {
				readyQueue.pop();
				readyQueue.push(idx);
				continue;
			}

			readyQueue.pop();

			pid_t pid = table[idx].pid;

			logFile << "Dispatching P" << idx << "\n";
			std::cout << "Dispatching P" << idx << "\n";

			msgbuffer msg;
			msg.mtype = pid;
			msg.quantum = QUANTUM;

			msgsnd(msqid, &msg, sizeof(msg.quantum), 0);

			incrementClock(10000);

			msgbuffer res;
			if (msgrcv(msqid, &res, sizeof(res.quantum), pid, 0) == -1) {
				perror("msgrcv failed");
				exit(1);
			}

			int used = abs(res.quantum);

			logFile << "P" << idx << " ran for " << used << " ns\n";

			incrementClock(used);

			table[idx].totalCPU += used;

			if (res.quantum < 0) {
				waitpid(pid, nullptr, 0);
				table[idx].occupied = 0;
				activeChildren--;
			}
			
			else if (used < QUANTUM) {
				table[idx].blocked = 1;

				table[idx].eventWaitSec = simClock->seconds;
				table[idx].eventWaitNano = simClock->nanoseconds + 100000000;

				if (table[idx].eventWaitNano >= 1000000000) {
					table[idx].eventWaitSec++;
					table[idx].eventWaitNano -= 1000000000;
				}
			}
			else {
				readyQueue.push(idx);
			}
		}

		else {
			incrementClock(1000000);
		}

		if (++logLines > 10000) break;
	
		if ((simClock->seconds > lastPrintSec) ||
				(simClock->seconds == lastPrintSec &&
				 simClock->nanoseconds >= lastPrintNano + 500000000)) {
			
			printProcessTable(table, logFile);
	
			lastPrintSec = simClock->seconds;
			lastPrintNano = simClock->nanoseconds;
		}
	}

	while (waitpid(-1, nullptr, WNOHANG) > 0);

	// Cleanup
	cleanup();

	logFile.close();

	return 0;
}
