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
	// attach to same message queue
	key_t key = ftok(".", 'A');
	int msqid = msgget(key, 0666);
	if (msqid == -1) {
		perror("msgget failed");
		exit(1);
	}

	pid_t mypid = getpid();

    	// Seed random using PID
    	srand(mypid);

	int totalUsed = 0;
	while (true) {
		msgbuffer msg;

		// wait for message from oss
		if (msgrcv(msqid, &msg, sizeof(int), mypid, 0) == -1) {
	            perror("msgrcv failed");
        	    exit(1);
        	}	

        	int quantum = msg.quantum;
        	msgbuffer response;
        	response.mtype = 1;
	
		int action = rand() % 100;

		if (action < 20) {
			int timeUsed = 1 + rand() % quantum;
			totalUsed += timeUsed;
			response.quantum = timeUsed;
		}

		else if (action < 40) {
         	   	int timeUsed = 1 + rand() % quantum;
		   	totalUsed += timeUsed;
			response.quantum = -timeUsed;
		   	if (msgsnd(msqid, &response, sizeof(int), 0) == -1) {
			   	perror("msgsnd failed");
			   	exit(1);
		   	}
		   	break;
        	}

		else {
			int remaining = quantum;
			response.quantum = quantum;
			totalUsed += remaining;
		}

		if (msgsnd(msqid, &response, sizeof(int), 0) == -1) {
			perror("msgsnd failed");
			exit(1);
		}
	}
	return 0;
}

