#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <ctime>
#include <queue>

// initialize system clock - seconds = 0, nanoseconds = 0
struct SimulatedClock {
        unsigned int seconds;
        unsigned int nanoseconds;

// create process control block
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

// setup message struct
struct msgbuffer {
	long mtype;
	int quantum;
};

// setup message queue
typedef struct msgbuffer {
        long mtype;
        char strData[100];
        int intData;
} msgbuffer;

// add variables
int activeChildren = 0;
int totalLaunched = 0;
int totalFinished = 0;

std::string filename = "log.txt";

// Main function
int main(int argc, char **argv) {

        // Default values for command line parameters
        int n = 1;
        int s = 1;
        float t = 1;
        float i = 1;

        int opt;
        // Currently running children
        int activeChildren = 0;
        // Total children ever launched
        int totalLaunched = 0;
        // Set up real time safety timeout (60 seconds)
        signal(SIGALRM, signal_handler);
        alarm(3);

        // parse command line arguments -n, -s, -t, -i, -f
	while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
                switch (opt) {
                        case 'h':
                                std::cout << "To run program:\n\t ./oss -n # -s # -t # -i # -f file name\n";
                                std::cout << "Replace # with an integer for n and s, and a float of t and i.\n";
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

	// initialize signal handling - avoid zombie processes
	void signal_handler(int sig) {
    		std::cout << "Caught signal " << sig << ", terminating...\n";
    	// cleanup here later
    	exit(1);
	}

	// create shared memory - system clock (seconds, nanoseconds)
	int shmid = shmget(IPC_PRIVATE, sizeof(SimulatedClock), IPC_CREAT | 0666);
	if (shmid == -1) {
    		perror("shmget failed");
    	exit(1);
	}

	SimulatedClock* clock = (SimulatedClock*) shmat(shmid, nullptr, 0);
	if (clock == (void*) -1) {
    		perror("shmat failed");
    	exit(1);
	}
	clock->seconds = 0;
	clock->nanoseconds = 0;

	// initialize queues = ready queue (round robin), blocked queue (logic to scan PCBs)
	std::queue<int> readyQueue;

	// create message queue
	int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
	if  (msqid == -1) {
		perror("msgget failed");
		exit(1);
	}

	// create process control table - array of PCBs
	PCB processTable[20];
	for (int j = 0; j < 20; j++) {
    		processTable[j].occupied = 0;
	}

	// main while loop
	while (totalLaunched < n || activeChildren < s) {
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
				processsTable[index].occupied = 1;
				processTable[index].pid = pid;
				processTable[index].blocked = 0;
				
				readyQueue.push(index);

				totalLaunched++;
				activeChildren++;
			}
		}

		// If ready queue has processes -> schedule
		if (!readyQueue.empty()) {

			int index = readyQueue.front();
			readyQueue.pop();

			pid_t pid = processTable[index].pid;

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
			if (msgrcv(msqid, &response, sizeof(int), 1, 0) == -1) {
				perror("msgrcv failed");
				exit(1);
			}

			int timeUsed = response.quantum;

			// update clock
			clock->nanoseconds += abs(timeUsed);
			while (clock->nanoseconds >= 1000000000) {
				clock->seconds++;
				clock->nanoseconds -= 1000000000;
			}

			// process behavior
			if (timeUsed < 0) {
				// terminated
				processTable[index].occupied = 0;
				activeChildren--;
				totalFinished++;
			}
			else if (timeUsed < QUANTUM) {
				// blocked
				processTable[index].blocked = 1;

				// set unblock time
				processTable[index].eventWaitSec = clock->seconds;
				processTable[index].eventWaitNano = clock->nanoseconds;
			}
			else {
				// used full quantum -> back to ready queue
				readyQueue.push(index);
			}
		}

		// check blocked processes and unblock
		for (int j = 0; j < 20; j++) {
			if (processTable[j].occupied && processTable[j].blocked) {
				if (clock->seconds > processTable[j].eventWaitSec ||
						(clock->seconds == processTable[j].eventWaitSec &&
						 clock->nanoseconds >= processTable[j].eventWaitNano)) {
						
						processTable[j].blocked = 0;
						readyQueue.push(j);
				}
			}
		}

		// if nothing ready then advance clock
		if (readyQueue.empty()) {
			clock->nanoseconds += 1000;
			if (clock->nanoseconds >= 1000000000) {
				clock->seconds++;
				clock->nanoseconds -= 1000000000;
			}
		}
	}

	// Cleanup
	shmdt(clock);
	shmctl(shmid, IPC_RMID, nullptr);
	msgctl(msqid, IPC_RMID, nullptr);
};
