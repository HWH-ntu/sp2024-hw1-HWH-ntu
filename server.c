#include "server.h"

const unsigned char IAC_IP[3] = "\xff\xf4";
const char* file_prefix = "./csie_trains/train_";
const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char* welcome_banner = "======================================\n"
                             " Welcome to CSIE Train Booking System \n"
                             "======================================\n";

const char* lock_msg = ">>> Locked.\n";
const char* exit_msg = ">>> Client exit.\n";
const char* cancel_msg = ">>> You cancel the seat.\n";
const char* full_msg = ">>> The shift is fully booked.\n";
const char* seat_booked_msg = ">>> The seat is booked.\n";
const char* no_seat_msg = ">>> No seat to pay.\n";
const char* book_succ_msg = ">>> Your train booking is successful.\n";
const char* invalid_op_msg = ">>> Invalid operation.\n";

#ifdef READ_SERVER
char* read_shift_msg = "Please select the shift you want to check [902001-902005]: ";
#elif defined WRITE_SERVER
char* write_shift_msg = "Please select the shift you want to book [902001-902005]: ";
char* write_seat_msg = "Select the seat [1-40] or type \"pay\" to confirm: ";
char* write_seat_or_exit_msg = "Type \"seat\" to continue or \"exit\" to quit [seat/exit]: ";
#endif

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

int accept_conn(void);
// accept connection

static void getfilepath(char* filepath, int extension);
// get record filepath

int handle_read(request* reqP) {
    /*  Return value:
     *      1: read successfully
     *      0: read EOF (client down)
     *     -1: read failed
     *   TODO: handle incomplete input
     */
    int r;
    char buf[MAX_MSG_LEN];
    size_t len;

    memset(buf, 0, sizeof(buf));

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");   // \n
        if (p1 == NULL) {
            if (!strncmp(buf, IAC_IP, 2)) {
                // Client presses ctrl+C, regard as disconnection
                fprintf(stderr, "Client presses ctrl+C....\n");
                return 0;
            }
        }
    }

    len = p1 - buf + 1;
    memmove(reqP->buf, buf, len); //為何不能直接寫進req.buf?因為好的code style應該要有buf過度，可以避免將錯誤的資訊直接寫進req.buf之中。確定好了這個buf的資料是對的之後，才用memmove去copy data
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;
    return 1;
}

#ifdef READ_SERVER
int print_train_info(int train_fd, char* seat_availability_msg, size_t msg_len) { //從struct印出來
    // print_train_info 做的事是讀檔，並且將檔案的內容格式化地讀進buffer(seat_availability_msg)中
    // Function to print seat availability from the file associated with train_fd
    char seat_buffer[SEAT_NUM * 2]; // Buffer for seat data (40 seats + newlines)
    memset(seat_buffer, 0, sizeof(seat_buffer));

    // Seek to the beginning of the file before reading
    lseek(train_fd, 0, SEEK_SET);

    // Read the seat data from the file
    int bytes_read = read(train_fd, seat_buffer, sizeof(seat_buffer));
    if (bytes_read <= 0) {
        snprintf(seat_availability_msg, msg_len, "Error reading seat data.\n");
        return -1;
    }

    // Debugging: Print the raw content of the seat_buffer
    printf("Raw seat data read from the file: \n");
    for (int i = 0; i < bytes_read; i++) {
        printf("%c", seat_buffer[i]);  // Print each character as-is
    }
    printf("\n");  // Newline after printing the entire buffer

    // Now, format the seat availability message (as before)
    memset(seat_availability_msg, 0, msg_len);  // Clear the buffer
    // Directly copy the raw content from seat_buffer to seat_availability_msg
    snprintf(seat_availability_msg, msg_len-1, "%s", seat_buffer); 
    // 這邊msg_len-1因為發現最後有多一個問號，不確定是原本的string讀到什麼，所以只讓他讀到-1的位置

    return 0;  // Success
}
#else //WRITE_SERVER
int print_train_info(request *reqP) {
    /*
     * Booking info
     * |- Shift ID: 902001
     * |- Chose seat(s): 1,2
     * |- Paid: 3,4
     */
    char buf[MAX_MSG_LEN*3];
    char chosen_seat[MAX_MSG_LEN] = "1,2";
    char paid[MAX_MSG_LEN] = "3,4";

    memset(buf, 0, sizeof(buf));
    sprintf(buf, "\nBooking info\n"
                 "|- Shift ID: %d\n"
                 "|- Chose seat(s): %s\n"
                 "|- Paid: %s\n\n"
                 ,902001, chosen_seat, paid);
    return 0;
}
#endif

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    int conn_fd;  // fd for file that we open for reading
    char buf[MAX_MSG_LEN*2], filename[FILE_LEN];

    int i,j;

    for (i = TRAIN_ID_START, j = 0; i <= TRAIN_ID_END; i++, j++) {
        getfilepath(filename, i);
#ifdef READ_SERVER
        trains[j].file_fd = open(filename, O_RDONLY); //j是0的時候，filename是902001;j是1的時候，filename是902002
#elif defined WRITE_SERVER
        trains[j].file_fd = open(filename, O_RDWR);
#else
        trains[j].file_fd = -1;
#endif
        if (trains[j].file_fd < 0) {
            ERR_EXIT("open");
        }
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    while (1) {
        // TODO: Add IO multiplexing

        // Check new connection
        conn_fd = accept_conn(); //server 等connection
        if (conn_fd < 0) // 如果沒有connection，就離開到while loop
            continue;

        // Send the welcome banner to the client upon connection
        write(requestP[conn_fd].conn_fd, welcome_banner, strlen(welcome_banner));

#ifdef READ_SERVER
    while (1) {
        // Now send the shift selection message if it's a READ_SERVER
        write(requestP[conn_fd].conn_fd, read_shift_msg, strlen(read_shift_msg));

        int ret = handle_read(&requestP[conn_fd]); //將User input 吃進&requestP[conn_fd]中
        //handle_read：讀client input,讀到它的internal buffer
	    if (ret < 0) { // -1: read failed user還沒寫就斷線，就沒寫到
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        } else if (ret == 0) { // 0: read EOF (client down)
            continue;
        }

        // Client input is in requestP[conn_fd].buf, convert it to an integer
        int read_server_train_number = atoi(requestP[conn_fd].buf);

        // Error handling: Check if the input is within the valid range
        if (read_server_train_number >= TRAIN_ID_START && read_server_train_number <= TRAIN_ID_END) {
            // Get the index for the train number (e.g., 902001 -> 0, 902005 -> 4)
            int train_index = read_server_train_number - TRAIN_ID_START;

            // Buffer for seat availability message
            char seat_availability_msg[MAX_MSG_LEN];

            // Print the seat info from the file using the train's file descriptor
            if (print_train_info(trains[train_index].file_fd, seat_availability_msg, sizeof(seat_availability_msg)) == 0) {
                // print_train_info 做的事是讀檔，並且將檔案的內容讀進buffer(seat_availability_msg)中，然後後續透過write，將這個msg output到那個connection的client output上 (requestP[conn_fd].conn_fd)
                // Send the seat availability to the client
                write(requestP[conn_fd].conn_fd, seat_availability_msg, strlen(seat_availability_msg));
            } else {
                // If there was an error reading the seat data, notify the client
                write(requestP[conn_fd].conn_fd, "Error retrieving seat data.\n", 30);
            }

        } else {
            // If the input is out of range, prompt the user again without terminating the loop
            write(requestP[conn_fd].conn_fd, "Invalid train number. Please try again.\n", 40);
            continue;
        }

        //以下是原本的code，因為不符合題目需求所以改掉
        // TODO: handle requests from clients
        //sprintf(buf,"%s : %s",accept_read_header,requestP[conn_fd].buf);
        //printf是printf到STDOUT，sprintf是print到string，到string裡的是concat完的兩條string
        //write(requestP[conn_fd].conn_fd, buf, strlen(buf));
        //把concat好的string寫到connection fd
    }

#elif defined WRITE_SERVER
        int ret = handle_read(&requestP[conn_fd]); 
        //handle_read：讀client input,讀到它的internal buffer
	    if (ret < 0) { // -1: read failed user還沒寫就斷線，就沒寫到
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        } else if (ret == 0) { // 0: read EOF (client down)
            continue;
        }

        // TODO: handle requests from clients
        sprintf(buf,"%s : %s",accept_write_header,requestP[conn_fd].buf);
        write(requestP[conn_fd].conn_fd, buf, strlen(buf)); 
 
#endif
        

        close(requestP[conn_fd].conn_fd); //關起這個client的connection
        free_request(&requestP[conn_fd]); //將剛剛的輸入清空
    }      

    free(requestP);
    close(svr.listen_fd);
    for (i = 0;i < TRAIN_NUM; i++)
        close(trains[i].file_fd);

    return 0;
}

int accept_conn(void) {

    struct sockaddr_in cliaddr;
    size_t clilen;
    int conn_fd;  // fd for a new connection with client

    clilen = sizeof(cliaddr);
    conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
    if (conn_fd < 0) {
        if (errno == EINTR || errno == EAGAIN) return -1;  // try again
        if (errno == ENFILE) {
            (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                return -1;
        }
        ERR_EXIT("accept");
    }
    
    requestP[conn_fd].conn_fd = conn_fd;
    strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
    fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
    requestP[conn_fd].client_id = (svr.port * 1000) + num_conn;    // This should be unique for the same machine.
    num_conn++;
    
    return conn_fd;
}

static void getfilepath(char* filepath, int extension) {
    char fp[FILE_LEN*2];
    
    memset(filepath, 0, FILE_LEN);
    sprintf(fp, "%s%d", file_prefix, extension);
    strcpy(filepath, fp);
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->client_id = -1;
    reqP->buf_len = 0;
    reqP->status = INVALID;
    reqP->remaining_time.tv_sec = 5;
    reqP->remaining_time.tv_usec = 0;

    reqP->booking_info.num_of_chosen_seats = 0;
    reqP->booking_info.train_fd = -1;
    for (int i = 0; i < SEAT_NUM; i++)
        reqP->booking_info.seat_stat[i] = UNKNOWN;
}

static void free_request(request* reqP) {
    memset(reqP, 0, sizeof(request));
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}