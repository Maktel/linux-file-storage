#include "settings.h"

const char* fifoname = "bhrcyzehrncjfrg.fifo"; // random name

const size_t hdsize = MSGSIZE;


typedef struct msgbuf {
    long mtype; // used by reader to decide on message selection, must be positive!
    char mtext[MSGSIZE]; // message
} Msgbuf_struct;


char* datify(int date) {
	struct tm* t;
    time_t a = (time_t) date; // proper cast
	t = localtime(&a); // create local date
	char* r = asctime(t); // stringify date
	if (r != NULL) {
		r[strlen(r)-1] = '\0'; // trim newline char
	}

	return r;
}


key_t generateKey(const char* filename) {
    key_t key;

    // tries to open file required by ftok; if it does not exists, create one
    FILE* fptr;
    if ( ( fptr = fopen(filename, "rb+") ) == NULL ) { // file does not exist
        fclose(fopen(filename, "wb"));
    }

    // create a key - this key must be equal in both client and server program
    if ( ( key = ftok(filename, 1) ) == -1 ) {
        perror("ftok");
        exit(1);
    }

    return key;
}


int startQueue(int createQueue, key_t key) {
	// create a message queue, return its id
	int msgid;
	if (createQueue) { // called from client, create new queue, for error if queue exists use IPC_EXCL flag
        if ( ( msgid = msgget(key, IPC_CREAT | 0664) ) == -1 ) {
    		perror("msgget");
    		exit(1);
    	}
    } else { // called from server, just connect to the existing queue
        if ( ( msgid = msgget(key, 0664) ) == -1) {
            perror("msgget");
            exit(1);
        }
    }

	return msgid;
}


int sendMessage(int msgid, long channel, const char* msgdata) {
	size_t msgsize = MSGSIZE;

	// create message struct
	Msgbuf_struct msgbuf;
    msgbuf.mtype = channel; // determine message type to differentiate between server and client messages
    memcpy(msgbuf.mtext, msgdata, msgsize); // copy elements of msgdata to message struct -- using strncpy would break message except for first field as a string scenario

	// push message to queue
	if ( msgsnd(msgid, &msgbuf, msgsize, 0) == -1 ) {
		perror("msgsnd");
		return -1;
	}

    return 0;
}


int receiveMessage(int msgid, long channel, char* msgdata) {
	size_t msgsize = MSGSIZE;

    // create message struct
	Msgbuf_struct msgbuf;
    // pop message from queue
	if ( msgrcv(msgid, &msgbuf, msgsize, channel, MSG_NOERROR ) == -1 ) { // truncates if necessary
		perror("msgrcv");
		return -1;
	}
	else {
        memcpy(msgdata, msgbuf.mtext, msgsize); // copy elemnt of message struct to msgdata -- using strncpy would break message except for first field as a string scenario
	}

    return 0;
}


int removeQueue(int msgid) {
    if ( msgctl(msgid, IPC_RMID, 0) == -1 ) {
		perror("msgctl");
		return -1;
	}

    return 0;
}
