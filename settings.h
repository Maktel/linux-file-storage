#ifndef _SERVER_CLIENT_HELPER_
#define _SERVER_CLIENT_HELPER_

    #include <stdio.h> // file, streams operations
    #include <stdlib.h> // exit()
    #include <unistd.h> // truncate(), unlink(), read(), write() and others
    #include <string.h> // strcpy()
    #include <sys/msg.h> // message queues
    #include <sys/types.h> // additional types
    #include <sys/stat.h> // mkfifo()
    #include <fcntl.h> // mode macros
    #include <errno.h> // errno
    #include <ctype.h> // isspace()
    #include <time.h> // date
    #include <utime.h> // utime()


    // max size of a message, enough to fit in header struct (20+4+4)
    #define MSGSIZE 28

    // size of a buffer for reading/writing to file/fifo
    #define BUFFER_SIZE 100

    // create string from unixtime date
    // possibly insecure because of int to time_t cast
    char* datify(int date);

// message queue creation and management
    // generate a key for a message queue
    // requires name of a file, which will be used to generate a key - should be unique to avoid conflicts with existing m. queues
    // returns key or exits on error
    key_t generateKey(const char* filename);

    // create or connect to an existing message queue
    // first argument: if zero is passed, merely connects to a queue, otherwise creates one
    // second argument is a key, obtained with generateKey()
    // returns id of a message queue
    int startQueue(int createQueue, key_t key);

    // send message to a message queue
    // requires id of a message queue, >0 type number and message
    // returns -1 on error
    int sendMessage(int msgid, long channel, const char* msgtext);

    // receives message from a queue
    // requires id of a message queue, >0 type number and pointer to a memory where message will be copied
    // returns -1 on error
    int receiveMessage(int msgid, long channel, char* msgcont);

    // deletes message queue
    // returns -1 on error
    int removeQueue(int msgid);

// FIFO management
    extern const char* fifoname;

// partition management
    const char* fsname;
    // defaul name of a partition file
    #define DEFAULT_FSNAME "fsfile.dat"

    // header of a file information structure -- should be 28 bytes long
    typedef struct header {
        char name[20];
        unsigned int size;
        int date; // can break at ~ 01/19/2038
    } header;

    // size of header equal to MSGSIZE
    extern const size_t hdsize;
#endif
