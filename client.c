#include "settings.h"

// trim name macro -- use with appended strcpy(tar, buf);
#define TRIMNAME(itr, buf, src) \
for ((itr)=0; (itr)<19; (itr)++) { \
    if ((src)[(itr)] == '\0'){ \
        break; \
    } \
    (buf)[(itr)] = (src)[(itr)]; \
} \
(buf)[(itr)] = '\0'


// secure function to read user input
// returns pointer to dymanically allocated char array
char* userInput();
// break user input into tokens
char* stringSplit(char* str, unsigned int n);
// create header struct with file info and short name
header getFileInfo(char* fname);

int main() {
    int msgid = startQueue(1, generateKey("queue.tmp")); // create message queue for transmission; send to 1, receive from 2
    mkfifo(fifoname, 0666); // create fifo in client, since it uses it first

    printf(
        "List of options (separate arguments with spaces):\n"
        "  exit -- clear memory and abort client\n"
        "  list -- list all files in partition\n"
        "  save [filename] -- write local file to partition\n"
        "  get [filename] -- load file from partition and save it in current directory\n"
        "  rename [old_name] [new_name] -- rename file in partition\n"
        "  delete [filename] -- delete file from partition\n"
        "  overwrite [filename] -- overwrite file with zeros\n"
    );

// main client loop
    int contCli = 1;
    while(contCli) {
        printf("> "); // prompt

        // user input holders
        char* userinput = userInput(); // get input from user
        char* command = stringSplit(userinput, 0);

    // commands switch
    // #exit
        if (!strcmp(command, "exit")) {
            sendMessage(msgid, 1, "exit"); // send signal
            free(userinput);
            free(command);
            sleep(1);
            contCli = 0;
            continue;

    // #list
        } else if (!strcmp(command, "list")) {
            sendMessage(msgid, 1, "list"); // send signal

            // read messages as long as "null" file is received
            while (1) {
                header hd;
                receiveMessage(msgid, 2, (char*) &hd);

                // "null" file, stop reading
                if (hd.name[0] == '\0' && hd.size == 0 && hd.date == 0) {
                    break;
                }
                printf("Name: %19s, size: %10dB, date: \"%s\"\n", hd.name, hd.size, datify(hd.date));
            }
            printf("Listing ended\n");

    // #save
        } else if (!strcmp(command, "save")) {
            int fifo;
            char* arg1 = stringSplit(userinput, 1); // get name of a file

            int file = open(arg1, O_RDONLY); // open file for reading
            if (file == -1) {
                perror("open");
                continue;
            }

            sendMessage(msgid, 1, "save"); // send signal

            // get file details and send them to server
            header hd = getFileInfo(arg1);
            sendMessage(msgid, 1, (char*) &hd); // send file info

            // check for error response
            header ret;
            receiveMessage(msgid, 2, (char*) &ret);
            if (!strcmp(ret.name, "noerr")) {
                // if size is zero, do not initialise transmission
                int ofZeroSize = 0;
                if (hd.size == 0) {
                    ofZeroSize = 1;
                }

                if (!ofZeroSize) {
                    // possible error handling problem -- header can be written, but no file data will be transmitted
                    // in order to open fifo for write, read must be opened first
                    // O_WRONLY can cause program to stop, O_WRONLY|O_NONBLOCK would exit immediately, O_RDWR creates issues with breaking -- would have to use poll()
                    fifo = open(fifoname, O_WRONLY); // open fifo in write only mode
                    if (fifo == -1) {
                        perror("open");
                        close(file); // close file before aborting
                        continue;
                    }
                }

                // send file
                char buf[BUFFER_SIZE];
                int _bytesRead=0, bytesRead=0, _bytesWritten, bytesWritten=0; // _ means temporary variable
                // read file and send it to fifo part by part
                while ( !ofZeroSize && (_bytesRead = read(file, &buf, BUFFER_SIZE)) > 0 ) {
                    bytesRead += _bytesRead; // count characters read

                    if ( (_bytesWritten = write(fifo, buf, _bytesRead)) == -1 ) {
                        perror("write");
                        break; // data may be corrupted
                    }
                    bytesWritten += _bytesWritten; // count characters sent, currently unused
                }
                if (_bytesRead == -1) { // loop has been aborted, so data may be corrupted
                    perror("read");
                }

                printf("File \"%s\" of size %dB and date \"%s\" has been saved in partition\n", hd.name, hd.size, datify(hd.date));

                close(fifo);

            } else { // server returned an error
                printf("Not enough space to save file\n");
            }

            free(arg1);
            close(file);

    // #get
        } else if (!strcmp(command, "get")) {
            int fifo;
            char* arg1 = stringSplit(userinput, 1); // get name of a file

            int err=0; // variable to save errno
            int file = open(arg1, O_WRONLY | O_CREAT | O_EXCL, 0664); // create file for writing
            if (errno == EEXIST && file == -1) { // file with that name exists
                err = errno; // preserve errno because of printf() calls
                printf("File with that name exists. Remove it and try again\n");
            }
            if (file == -1) { // usual errors handling
                if (err) { // spohisticated perror()
                    char* errs = strerror(err);
                    printf("open: %s\n", errs);
                } else { // default handling
					perror("open");
				}
                continue;
            }

            sendMessage(msgid, 1, "get"); // send signal

            header hd;

            // trim file name to match name previously trimmed (during saving)
            char fname[20];
            int i;
            TRIMNAME(i, fname, arg1);
            strcpy(hd.name, fname);

            // send file name
            sendMessage(msgid, 1, (char*) &hd);


            receiveMessage(msgid, 2, (char*) &hd); // receive header info
            int canRead = 0;
            if (!strcmp(hd.name, "nosuchfile")) { // signal indicates no file found
                printf("File not found: %s\n", hd.name);
                close(file);
                continue;
            } else if (hd.size != 0) { // found and file is not empty
                canRead = 1;

                fifo = open(fifoname, O_RDONLY);
                if (fifo == -1) {
                    perror("open");
                    close(file);
                    continue;
                }
            }

            // read from fifo and write to file
        	char buf[BUFFER_SIZE];
            int _bytesRead, _bytesWritten; // immediate loop control
            unsigned int bytesRead=0, bytesWritten=0; // -1 will never be reached for these
            while( canRead && ( (_bytesRead = read(fifo, &buf, BUFFER_SIZE)) >= 0 ) ) {
				if (bytesRead >= hd.size || _bytesRead == 0) {
					break; // probably should break without this
				}
                bytesRead += _bytesRead; // sum read bytes

                if ( (_bytesWritten = write(file, buf, _bytesRead)) == -1 ) {
                    perror("write");
                    break;
                }
                bytesWritten += _bytesWritten; // sum written bytes
        	}
            if (_bytesRead == -1) {
                perror("read");
            } else if (bytesRead != bytesWritten) {
                printf("Bytes received and written differ, possible data corruption\n");
            }
            else {
                printf("File \"%s\" of size %dB has been received\n", hd.name, hd.size);
            }

            const struct utimbuf utimestr = { .actime = (time_t) hd.date, .modtime = (time_t) hd.date };
            if (utime(hd.name, &utimestr) == -1) {
                perror("utime");
            } else {
                printf("Access and modification time has been set to \"%s\"\n", datify(hd.date));
            }

            free(arg1);
            close(file);
            close(fifo);

    // #rename
        } else if (!strcmp(command, "rename")) {
            char* arg1 = stringSplit(userinput, 1); // get old name
            char* arg2 = stringSplit(userinput, 2); // get new name
            if (!strcmp(arg1, "") || !strcmp(userinput, arg1) || !strcmp(arg2, "") || !strcmp(userinput, arg2)) {
                printf("Invalid arguments, try again\n");
                continue;
            }

            sendMessage(msgid, 1, "rename"); // send signalrename

            header hd = { .name = "", .size=0, .date=0 };

            // trim old name
            int i;
            char oname[20];
            TRIMNAME(i, oname, arg1);
            strcpy(hd.name, oname);

            sendMessage(msgid, 1, (char*) &hd); // send old name


            receiveMessage(msgid, 2, (char*) &hd); // server should return string
            if (!strcmp(hd.name, "nosuchfile")) {
                printf("File does not exist\n");
                continue;
            }

            // trim new name
            int j;
            char nname[20];
            TRIMNAME(j, nname, arg2);
            strcpy(hd.name, nname);

            sendMessage(msgid, 1, (char*) &hd); // send new name

            printf("File \"%s\" renamed to \"%s\"\n", oname, nname);

            free(arg1);
            free(arg2);

    // #delete
        } else if (!strcmp(command, "delete")) {
            sendMessage(msgid, 1, "delete"); // send signal
            char* arg1 = stringSplit(userinput, 1); // get name

            header hd;
            strcpy(hd.name, arg1);
            sendMessage(msgid, 1, (char*) &hd); // send name

            receiveMessage(msgid, 2, (char*) &hd); // should return string
            if (!strcmp(hd.name, "nosuchfile")) {
                printf("File does not exist\n");
                continue;
            }

            printf("File deleted\n");
            free(arg1);

    // #overwrite
        } else if (!strcmp(command, "overwrite")) {
            sendMessage(msgid, 1, "overwrite"); // send signal
            char* arg1 = stringSplit(userinput, 1); // get name

            header hd;
            strcpy(hd.name, arg1);
            sendMessage(msgid, 1, (char*) &hd); // send name

            receiveMessage(msgid, 2, (char*) &hd); // should return string
            if (!strcmp(hd.name, "nosuchfile")) {
                printf("File does not exist\n");
                continue;
            }

            printf("File overwritten\n");
            free(arg1);

    // #unknown
        } else
            printf("Unrecognised input, try again\n");
    }
    printf("Terminating\n");


    unlink(fifoname);
    removeQueue(msgid);
    return 0;
}


// courtesy of SO.com
char* userInput() {
    int max = 21;
    char* name = (char*) calloc(max, sizeof(*name)); /* allocate buffer */
    if (name == 0) {
        perror("malloc");
        exit(1);
    }

    while (1) { /* skip leading whitespace */
        int c = getchar();
        if (c == EOF) break; /* end of file */
        if (!isspace(c)) {
             ungetc(c, stdin);
             break;
        }
    }

    int i = 0;
    while (1) {
        int c = getchar();
        if (c == '\n' || c == EOF) { /* end string with null */
            name[i] = '\0';
            break;
        }
        name[i] = c;
        if (i == max - 1) { /* buffer full */
            max = max + max;
            name = (char*) realloc(name, max); /* get a new and larger buffer */
            if (name == 0) {
                perror("malloc");
                exit(1);
            }
        }
        i++;
    }

    return name;
}

// split string to get individual tokens from user input
char* stringSplit(char* str, unsigned int n) {
    char* token = malloc((strlen(str)+1) * sizeof(*token));
    if (token == NULL) {
        perror("malloc");
        return NULL;
    }
    unsigned int i, a=0, j=0;

    for (i=0; i<=strlen(str); i++) {
        // added optional space escape character
        if ( ( str[i] == ' ' && (i>0 && str[i-1] != '\\') ) || str[i] == '\0' ) {
            if (a < n) {
                a++;
                j=0;
            }
            else {
                token = realloc(token, (j+1) * sizeof(*token));
                if (token == NULL) {
                    perror("realloc");
                    return NULL;
                }
                int k;
                for (k=j; k>=0; k--) {
                    token[j-k] = str[i-k];
                }
                token[j] = '\0';

                // escape characters -- remove backslashes
                // courtesy of SO.com
                char *src, *dst;
                for (src = dst = token; *src != '\0'; src++) {
                    *dst = *src;
                    if (*dst != '\\') dst++;
                }
                *dst = '\0';

                return token;
            }
        } else {
            j++;
        }
    }

    strcpy(token, str);
    return token;
}

header getFileInfo(char* fname) {
    header hd;

    // trim name
    char c[20];
    int i;
    TRIMNAME(i, c, fname);
    strcpy(hd.name, c);

    // size
    struct stat details;
    lstat(fname, &details);
    hd.size = details.st_size;

    // date
    struct stat statbuf;
    if (stat(fname, &statbuf) == -1) {
        perror(fname);
        hd.date = 0; // set to 0 on error
    } else {
        hd.date = (statbuf.st_mtime + 2*60); // fix date to match timezone
    }

    return hd;
}
