#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctime>

struct msgbuffer {
	long mtype;
	int quantum;
};

int main(int argc, char** argv) {
	if (argc < 2) {
		perror("missing msqid");
		return 1;
	}

	int msqid = atoi(argv[1]);
	pid_t pid = getpid();

	// Seed random using PID
    	srand(time(nullptr) ^ pid);

	int totalCPU = 0;

	while (true) {
		msgbuffer msg;

		// wait for message from oss
		if (msgrcv(msqid, &msg, sizeof(msg.quantum), getpid(), 0) == -1) {
	            perror("msgrcv failed");
        	    exit(1);
        	}	

        	int quantum = msg.quantum;

		int action = rand() % 100;
		int usedTime = 0;

        	msgbuffer response;
        	response.mtype = 1;

		if (action < 20) {
			usedTime = 1 + rand() % quantum;
			response.quantum = usedTime;
		}

		else if (action < 40) {
			usedTime = 1 +rand() % quantum;
		     	response.quantum = -usedTime;	

			if (msgsnd(msqid, &response, sizeof(response.quantum), 0) == -1) {
			   	perror("msgsnd failed");
			   	exit(1);
		   	}
		   	break;
        	}

		else {
			usedTime = quantum;
			response.quantum = usedTime;
		}

		totalCPU += usedTime;

		if (msgsnd(msqid, &response, sizeof(response.quantum), 0) == -1) {
			perror("msgsnd failed");
			exit(1);
		}
	}
	return 0;
}

