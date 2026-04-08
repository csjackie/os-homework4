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
	if (argc < 3) return 1;

	int msqid = atoi(argv[1]);
	int maxCPU = atoi(argv[2]);
	pid_t pid = getpid();

	// Seed random using PID
    	srand(time(nullptr) ^ pid);

	int totalCPU = 0;

	while (true) {
		msgbuffer msg;

		// wait for message from oss
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

		if (totalCPU >= maxCPU) {
			res.quantum = -1;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}

		if (action < 20) {
			used = 1 + rand() % quantum;
			res.quantum = used;
		}

		else if (action < 40) {
			used = 1 + rand() % quantum;
		     	res.quantum = -used;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}	

		else {
			used = quantum;
			res.quantum = used;
		}

		totalCPU += used;

		if (totalCPU >= maxCPU) {
			res.quantum = -used;
			msgsnd(msqid, &res, sizeof(int), 0);
			break;
		}
		
		msgsnd(msqid, &res, sizeof(res.quantum), 0);
	}

	return 0;
}

