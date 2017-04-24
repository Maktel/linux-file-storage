// If a file is inserted into an empty space (left by a previously deleled file), file's size is extended to match the size of the hole and during getting additional zeros are appended

#include "settings.h" // global variables and functions

FILE* openPartition(const char* fsname);

// check errors with feof() and ferror()
// return 1 on eof, 2 on error, 0 otherwise
int freadError(FILE* fs);

int main(int argc, char* argv[]) {
    if (argc == 2) {
        fsname = argv[1];
    } else {
        fsname = DEFAULT_FSNAME;
    }

    int msgid = startQueue(0, generateKey("queue.tmp")); // open existing message queue for transmission; send to 2, receive from 1
    FILE* fs = openPartition(fsname);

    // static writes
    // header hd;
    // strcpy(hd.name, "hello world");
    // // hd.name[0] = '\0';
    // hd.size=0;
    // hd.date=345;
    // fwrite(&hd, sizeof(header), 1, fs);

    // // strcpy(hd.name, "hello world");
    // hd.name[0] = '\0';
    // hd.size=123;
    // hd.date=345;
    // fwrite(&hd, sizeof(header), 1, fs);


    int contServ = 1;
    while (contServ) {
        printf("--- --- --- --- ---\n");
        // receive signal
        header sg;
        receiveMessage(msgid, 1, (char*) &sg);

    // #exit
        if (!strcmp(sg.name, "exit")) {
            contServ = 0;
            continue;

    // #list
        } else if (!strcmp(sg.name, "list")) {
            printf("Started listing\n");
            unsigned int n = 0; // how many elements have been listed
            rewind(fs); // reset position to beginning

            header hd;
            fread(&hd, hdsize, 1, fs); // read first element
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            while (1) {
                if (hd.name[0] != '\0') { // valid element
                    printf("Header: hd.name=%19s, hd.size=%10d, at position=%ld\n", hd.name, hd.size, ftell(fs)-hdsize);
                    sendMessage(msgid, 2, (char*) &hd); // send element header information
                    n++;
                } else // name is empty
                    if (hd.size == 0 || feof(fs)) { // empty space or end of partition -- terminate
                    // ensure "null" file
                    hd.date = 0;
                    sendMessage(msgid, 2, (char*) &hd); // send null file, meaning end of transmission
                    clearerr(fs);
                    break;
                } else { // file is just a deleted file placeholder, ignore, but update position
                    printf("--Empty space of size=%d, at position=%ld\n", hd.size, ftell(fs)-hdsize);
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }

                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    break;
                }
            }
            printf("Listing completed, listed %d elements\n", n);

    // #save
        } else if (!strcmp(sg.name, "save")) {
            // if a new file is saved in place of a deleted file, new file is stretched to match space of old one in order to keep continuity -- linked list model
            int fifo;

            header det; // details struct
            receiveMessage(msgid, 1, (char*) &det); // receive header

            header hd; // operational struct
            long pos = -1; // position in partition

            rewind(fs); // reset position to beginning
            fread(&hd, hdsize, 1, fs); // read first element
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            while (1) { // move pointer until free space is found
                printf("Looking for space at position: %ld\n", ftell(fs));
                if ( hd.name[0] == '\0' && hd.size == 0 ) { // end of partition
                    long c_pos = ftell(fs); // save current position
                    fseek(fs, 0, SEEK_END); // go to end of file
                    long e_pos = ftell(fs); // save partition end position
                    if ( (e_pos-c_pos) < det.size ) { // check if enough free space
                        sendMessage(msgid, 2, "notenoughspace");
                        break; // space not found
                    } else {
                        pos = c_pos - sizeof(det);
                        break; // found space
                    }
                } else if ( hd.name[0] == '\0' && hd.size >= det.size ) { // found empty space
                    pos = ftell(fs) - sizeof(det);
                    det.size = hd.size; // stretch received file to match hole
                    break; // found a hole
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }

                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    break;
                }
            }
            if (pos == -1) {
                printf("No free space found. Aborting\n");
                sendMessage(msgid, 2, "notenoughspace");
                continue;
            }
            sendMessage(msgid, 2, "noerr");
            printf("Sent noerr signal\n");


            if (fseek(fs, pos, SEEK_SET) == -1) { // set position
                perror("fseek");
                break;
            }

            // write header info
            int canWriteData = 1;
            fwrite(&det, 1, hdsize, fs);
            if (ferror(fs)) {
                printf("Error during writing header. Aborting\n");
                clearerr(fs);
                canWriteData = 0;
                continue;
            }

            // if size is zero, do not initialise transmission
            int ofZeroSize = 0;
            if (det.size == 0 && canWriteData) {
                ofZeroSize = 1;
            } else { // open fifo
                fifo = open(fifoname, O_RDONLY);
                if (fifo == -1) {
                    perror("open");
                    continue;
                }
            }

            // write file itself
            char buf[BUFFER_SIZE] = {0}; // initialise buffer with zeros
            int _bytesRead, bytesRead=0;
            while( canWriteData && !ofZeroSize && (_bytesRead = read(fifo, buf, BUFFER_SIZE)) > 0) {
                bytesRead += _bytesRead;

                fwrite(buf, _bytesRead, 1, fs);
                if (ferror(fs)) {
                    printf("Error during writing data\n");
                    clearerr(fs);
                    break; // data may be corrupt
                }
            }
            if (_bytesRead == -1) {
                perror("read");
            }

            printf("File \"%s\":%d:\"%s\" saved successfully\n", det.name, det.size, datify(det.date));

            close(fifo);

    // #get
        } else if (!strcmp(sg.name, "get")) {
            char fname[20];
            header hd; // operation struct
            receiveMessage(msgid, 1, (char*) &hd); // receive name
            strcpy(fname, hd.name);

            long pos = -1;
            rewind(fs); // reset position to beginning
            fread(&hd, hdsize, 1, fs); // read first element
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            while (1) { // find file to be sent
                printf("Reading header at position: %ld\n", ftell(fs)-hdsize); // debug
                printf("!strcmp: %d, fname: \"%s\", hd.name: \"%s\"\n", !strcmp(hd.name, fname), fname, hd.name); // debug
                if (!strcmp(hd.name, fname) && hd.name[0] != '\0') { // found match
                    sendMessage(msgid, 2, (char*) &hd); // send found file header
                    pos = ftell(fs); // position already set to transmit file
                    break;
                }
                if (hd.name[0] == '\0' && hd.size == 0) { // reached end of the partition
                    printf("Nothing appropriate found, terminating\n");
                    sendMessage(msgid, 2, "nosuchfile");
                    break;
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }

                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    break;
                }
            }
            if (pos == -1) { // nothing found
                continue;
            }
            printf("Found file at position: %ld\n", pos - hdsize);
            printf("Found: hd.name: %19s, hd.size: %10d, hd.date: %s\n", hd.name, hd.size, datify(hd.date));


            // if size is zero, do not initialise transmission
            if (hd.size == 0) {
                printf("File empty, no transmission\n");
                continue;
            }

            // open fifo
            int fifo = open(fifoname, O_WRONLY);
            if (fifo == -1) {
                perror("open");
                continue;
            }

            // send file body
            char buf[BUFFER_SIZE] = {0}; // initialise buffer with zeros
            int i, // file length, how many bytes are to be transmitted
                nBytes, // how many bytes should be read in a iteration
                _bytesWritten; // _ means temp
            unsigned int bytesWritten=0; // bytes written in general, control for write()
            for (i=hd.size; i>0; i -= nBytes) {
                nBytes = (i>=BUFFER_SIZE ? BUFFER_SIZE : i);

                fread(buf, nBytes, 1, fs); // read from file
                if (freadError(fs)) {
                    printf("Reading part of file failed\n");
                    break;
                }

                if ( (_bytesWritten = write(fifo, buf, nBytes)) == -1) { // write to fifo
                    perror("write");
                    break;
                }
                bytesWritten += _bytesWritten;
                printf("nBytes: %d, _bytesWritten: %d, bytesWritten: %d\n", nBytes, _bytesWritten, bytesWritten); // debug
            }
            if (bytesWritten != hd.size) {
                printf("Number of bytes sent (%d) do not match file size (%d), possible data corruption\n", bytesWritten, hd.size);
            } else {
                printf("File sent successfully\n");
            }

            close(fifo);

    // #rename
        } else if (!strcmp(sg.name, "rename")) {
            header det;
            receiveMessage(msgid, 1, (char*) &det); // receive old name

            long pos = -1; // position in partition
            header hd;

            rewind(fs); // reset position to beginning
            fread(&hd, hdsize, 1, fs); // read first header
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            while (1) { // find file to be renamed
                printf("Reading header at position: %ld\n", ftell(fs)-hdsize); // debug
                printf("!strcmp: %d, det.name: \"%s\", hd.name: \"%s\"\n", !strcmp(hd.name, det.name), det.name, hd.name); // debug
                if (!strcmp(hd.name, det.name) && hd.name[0] != '\0') { // found match
                    sendMessage(msgid, 2, "foundmatch"); // send found signal
                    pos = ftell(fs) - hdsize; // position set to changing header
                    break;
                }
                if (hd.name[0] == '\0' && hd.size == 0) { // reached end of the partition
                    printf("Nothing appropriate found, reporting error\n");
                    sendMessage(msgid, 2, "nosuchfile");
                    break;
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }

                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    continue;
                }
            }

            if (fseek(fs, pos, SEEK_SET) == -1) { // set position for header writing
                perror("fseek");
                break;
            }

            if (hd.name != '\0') {
                receiveMessage(msgid, 1, (char*) &det); // get new name
                strcpy(hd.name, det.name); // copy new name to old header, preserving details

                fwrite(&hd, hdsize, 1, fs); // update new header
                if (ferror(fs)) {
                    printf("Error during writing new header\n");
                    clearerr(fs);
                } else {
                    printf("File at header position %ld renamed successfully to \"%s\"\n", pos, hd.name);
                }
            } else {
                printf("Renaming unsuccessful, found wrong match\n");
            }

    // #delete
        } else if (!strcmp(sg.name, "delete")) {
            header det;
            receiveMessage(msgid, 1, (char*) &det); // receive file name

            long pos = -1; // position in partition
            long endpos = -1;
            header hd;

            rewind(fs); // reset position to beginning
            fread(&hd, hdsize, 1, fs); // read first header
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            int didNotFoundYet = 1; // protection against deleting last found file instead of first
            while (1) { // find file to be deleted
                printf("Reading header at position: %ld\n", ftell(fs)-hdsize); // debug
                printf("!strcmp: %d, det.name: \"%s\", hd.name: \"%s\"\n", !strcmp(hd.name, det.name), det.name, hd.name); // debug
                if (!strcmp(hd.name, det.name) && hd.name[0] != '\0') { // found match
                    if (didNotFoundYet) { // ignore more matches
                        sendMessage(msgid, 2, "foundmatch"); // send found signal
                        // save details of file for deletion
                        det.size = hd.size;
                        det.date = hd.date;
                        printf("--Start of found file's header position: %ld\n", ftell(fs) - hdsize);

                        pos = ftell(fs) + det.size; // position set to overwriting file (at the end of it)
                        // do not exist on match, look for end position
                        didNotFoundYet = 0;
                    }
                }
                if (hd.name[0] == '\0' && hd.size == 0) { // reached end of the partition
                    endpos = ftell(fs) - hdsize;
                    break;
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }
                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    break;
                }
            }
            if (pos == -1) {
                printf("Nothing appropriate found, reporting error\n");
                sendMessage(msgid, 2, "nosuchfile");
                continue;
            }

            if (fseek(fs, pos, SEEK_SET) == -1) { // set pointer at the end of removed file
                perror("fseek");
                continue;
            }
            int size = det.size + hdsize; // size of removed part, including header

            int move, noError=1;
            char buf[BUFFER_SIZE] = {0}; // initialise buffer with zeros
            while(noError) {
                // calculate buffer size = n
                move = size > BUFFER_SIZE ? BUFFER_SIZE : size % BUFFER_SIZE;
                if (size <= 0) {
                    break;
                }
                printf("Position of the end of removed part: %ld\n", ftell(fs));
                size -= move;
                do { // move whole partition by n bytes "left"
                    // read n bytes is after position to buffer
                    fread(buf, move, 1, fs);
                    if (freadError(fs)) { // possibly can cause problems during operations close to end of partition
                        printf("Reading part of partition failed\n");
                        noError = 0;
                        break;
                    }

                    // go back by 2 times n bytes
                    if (fseek(fs, (-1)*(move * 2), SEEK_CUR) == -1) {
                        perror("fseek");
                        noError = 0;
                        break;
                    }

                    // write n bytes from buffer
                    fwrite(buf, move, 1, fs);
                    if (ferror(fs)) {
                        printf("Error during overwriting part of partition\n");
                        clearerr(fs);
                        continue;
                    }

                    // move forward n bytes for another write
                    if (fseek(fs, move, SEEK_CUR) == -1) {
                        perror("fseek");
                        noError = 0;
                        break;
                    }
                } while (ftell(fs) < endpos + BUFFER_SIZE); // repeat until end of partition is met

                pos -= move;
                if (fseek(fs, pos, SEEK_SET)) { // position at the new end of removed file
                    perror("fseek");
                    noError = 0;
                    break;
                }
            }
            if (!noError) {
                printf("There was an error during partition writing, possible data corruption\n");
            } else {
                printf("File \"%s\" deleted successfully\n", det.name);
            }

    // #overwrite
        } else if (!strcmp(sg.name, "overwrite")) {
            header det;
            receiveMessage(msgid, 1, (char*) &det); // receive file name

            long pos = -1; // position in partition
            header hd;

            rewind(fs); // reset position to beginning
            fread(&hd, hdsize, 1, fs); // read first header
            if (freadError(fs)) {
                printf("Reading first header failed\n");
                continue;
            }
            while (1) { // find file to be overwritten
                printf("Reading header at position: %ld\n", ftell(fs)-hdsize); // debug
                printf("!strcmp: %d, det.name: \"%s\", hd.name: \"%s\"\n", !strcmp(hd.name, det.name), det.name, hd.name); // debug
                if (!strcmp(hd.name, det.name) && hd.name[0] != '\0') { // found match
                    sendMessage(msgid, 2, "foundmatch"); // send found signal
                    pos = ftell(fs) - hdsize; // position set to changing header

                    // save details of file for overwritting
                    det.size = hd.size;
                    det.date = hd.date;

                    break;
                }
                if (hd.name[0] == '\0' && hd.size == 0) { // reached end of the partition
                    printf("Nothing appropriate found, reporting error\n");
                    sendMessage(msgid, 2, "nosuchfile");
                    break;
                }

                if (fseek(fs, hd.size, SEEK_CUR) == -1) { // move position by file size
                    perror("fseek");
                    break;
                }

                fread(&hd, hdsize, 1, fs); // read next file into buffer
                if (freadError(fs)) {
                    printf("Reading next header failed\n");
                    break;
                }
            }
            if (pos == -1) {
                continue;
            }

            if (fseek(fs, pos, SEEK_SET) == -1) { // set position for header overwriting
                perror("fseek");
                continue;
            }

            // overwrite header info
            hd.name[0] = '\0';
            hd.size = det.size; // preserve linking
            hd.date = 0;
            fwrite(&hd, 1, hdsize, fs);
            if (ferror(fs)) {
                printf("Error during overwriting header\n");
                clearerr(fs);
                continue;
            }

            // overwrite file itself
            int i, nBytes; // iterator; number of bytes to be deleted
            unsigned int bytesOverwritten=0; // control
            char buf[BUFFER_SIZE] = {0}; // initialise buffer with zeros
            // already in proper position
            for (i=det.size; i>0; i -= nBytes) {
                nBytes = (i>=BUFFER_SIZE ? BUFFER_SIZE : i);
                printf("Number of bytes to be overwritten: %d\n", nBytes);

                fwrite(buf, nBytes, 1, fs);
                if (ferror(fs)) {
                    printf("Error during writing data\n");
                    clearerr(fs);
                    break; // data may be corrupt
                }
                bytesOverwritten += nBytes;
            }
            if (bytesOverwritten != det.size) {
                printf("Possible data corruption: overwritten %dB and file was of %dB size", bytesOverwritten, det.size);
                continue;
            }
            printf("Overwritting ended, affected %d bytes\n", bytesOverwritten);

            printf("File \"%s\" overwritten successfully\n", det.name);

        } else {
            printf("Unrecognised message, try restarting both programs\n");
        }
    }

    fclose(fs);
    return 0;
} // main end


FILE* openPartition(const char* fsname) {
    printf("Trying to open partition file: %s\n", fsname);
    // try to open file for reading and writing in binary mode
    FILE* fs = fopen(fsname, "r+b");
    if (fs == NULL) { // file does not exist (or other error occured)
        printf("Could not partition open file, creating one\n");

        // creates an empty file for reading and writing, in binary mode
        if ( (fs = fopen(fsname, "w+b")) == NULL ) { // could not create file, abort
            perror("fopen");
            exit(1);
        }
        // set size of a file in bytes, fill with nulls
        truncate(fsname, (off_t) 1024*1024); // 1 Mb
    }
    printf("Successfully opened partition file\n");

    return fs;
}

int freadError(FILE* fs) {
    if (feof(fs)) { // reached end of file
        printf("End of partition reached\n");
        clearerr(fs);
        return 1;
    }
    if (ferror(fs)) { // error encountered
        printf("Error encountered while reading partition\n");
        clearerr(fs);
        return 2;
    }
    return 0;
}
