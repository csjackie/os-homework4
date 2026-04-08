#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctime>
#include <cerrno>

struct msgbuffer {
	long mtype;
	int quantum;
};

int main(int argc, char** argv) {
	// Require msqid and maxCPU
	if (argc < 3) return 1;

	int msqid = atoi(argv[1]);
	int maxCPU = atoi(argv[2]);
	pid_t pid = getpid();

	// Seed RNG with PID for variability
    	srand(time(nullptr) ^ pid);

	int totalCPU = 0;

	while (true) {
		msgbuffer msg;

		// Wait for dispatch from oss
		if (msgrcv(msqid, &msg, sizeof(msg.quantum), pid, 0) == -1) {
	            	if (errno == EIDRM) exit(0);
			perror("msgrcv");
        	    	exit(1);
        	}	

        	int quantum = msg.quantum;
		int action = rand() % 100;
		int used = 0;

        	msgbuffer res;
        	res.mtype = pid;

		// If exceeded max CPU, terminate
		if (totalCPU >= maxCPU) {
			res.quantum = -1;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}

		// 20%, use partial quantum
		if (action < 20) {
			used = 1 + rand() % quantum;
			res.quantum = used;
		}

		// 20%, terminate early
		else if (action < 40) {
			used = 1 + rand() % quantum;
		     	res.quantum = -used;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}	

		// 60%, use full quantum
		else {
			used = quantum;
			res.quantum = used;
		}

		totalCPU += used;

		// If exceeded max CPU after run, terminate
		if (totalCPU >= maxCPU) {
			res.quantum = -used;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}
		
		// Send result back to OSS
		msgsnd(msqid, &res, sizeof(res.quantum), 0);
	}

	return 0;
}

