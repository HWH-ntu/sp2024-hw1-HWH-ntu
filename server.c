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
const char* read_shift_msg = "Please select the shift you want to check [902001-902005]: ";
#elif defined WRITE_SERVER
const char* write_shift_msg = "Please select the shift you want to book [902001-902005]: ";
const char* write_seat_msg = "Select the seat [1-40] or type \"pay\" to confirm: ";
const char* write_seat_or_exit_msg = "Type \"seat\" to continue or \"exit\" to quit [seat/exit]: ";
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

int train_number_to_fd (int train_number) {
    int train_index = train_number - TRAIN_ID_START;
    int train_fd_no = trains[train_index].file_fd;
    return train_fd_no;
}

#ifdef READ_SERVER
int print_train_info(int train_fd, char* seat_availability_msg, size_t msg_len) { //從struct印出來-->有沒有成功讀出來，並且格式化為string?
    // print_train_info 做的事是讀檔，並且將檔案的內容格式化地讀進buffer(seat_availability_msg)中
    char seat_buffer[MAX_MSG_LEN]= {0}; // Initialize with zero 相當於 memset(seat_buffer, 0, sizeof(seat_buffer));

    // Seek to the beginning of the file before reading
    lseek(train_fd, 0, SEEK_SET);

    // Read the seat data from the file
    int bytes_read = read(train_fd, seat_buffer, sizeof(seat_buffer));
    if (bytes_read <= 0) {
        snprintf(seat_availability_msg, msg_len, "Error reading seat data.\n");
        return -1;
    }

    snprintf(seat_availability_msg, msg_len, "%s", seat_buffer); 

    return 0;  
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

int if_seat_available (int train_number, int seat_number, enum SEAT* seat_availability) {
    int train_fd = train_number_to_fd(train_number);
    int lseek_n = lseek(train_fd, (seat_number-1)*2 , SEEK_SET);
    //printf("lseek_n:%d\n", lseek_n);
    //printf("train_fd:%d\n", train_fd);
    char seat_stat[2] = {0}; //因為atoi吃c string，所以還是要給2個位置
    int n = read(train_fd, seat_stat, 1);
    //printf("n: %d %d\n", n, errno);
    //printf("char_seat_stat: %s\n", seat_stat);
    int seat_stat_no = atoi(seat_stat);
    //這邊有一個因為拿不到loc，所以代表seat被reserve，要回傳CHOSEN
    if(n >=1){
        if( seat_stat_no == 0){//seat is now empty
            *seat_availability = EMPTY;
        } else if (seat_stat_no == 1){
            *seat_availability = PAID;
        }
        return 1;
    } else {
        printf("Cannot retrieve seat availability successfully.");
        return 0;
    }
}
#endif

int main(int argc, char** argv) {
    int Train_seat_left[5] = {40, 0, 21, 22, 26}; //How many seats left in each train. (e.g., 902001 -> 0, 902005 -> 4)
    int If_Train_Full[5] = {0, 1, 0, 0, 0};//Full:1, Not-Full:0

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
        requestP[conn_fd].booking_info.shift_id = atoi(requestP[conn_fd].buf);

        // Error handling: Check if the input is within the valid range
        if (requestP[conn_fd].booking_info.shift_id >= TRAIN_ID_START && requestP[conn_fd].booking_info.shift_id <= TRAIN_ID_END) {
            // Get the index for the train number (e.g., 902001 -> 0, 902005 -> 4)
            //int train_index = requestP[conn_fd].booking_info.shift_id - TRAIN_ID_START;

            // Buffer for seat availability message
            char seat_availability_msg[MAX_MSG_LEN];

            // Print the seat info from the file using the train's file descriptor
            if (print_train_info(train_number_to_fd(requestP[conn_fd].booking_info.shift_id), seat_availability_msg, sizeof(seat_availability_msg)) == 0) {
                // print_train_info 做的事是讀檔，並且將檔案的內容讀進buffer(seat_availability_msg)中，然後後續透過write，將這個msg output到那個connection的client output上 (requestP[conn_fd].conn_fd)
                // Send the seat availability to the client
                write(requestP[conn_fd].conn_fd, seat_availability_msg, strlen(seat_availability_msg));
            } else {
                // If there was an error reading the seat data, notify the client
                //const char* lock_msg = ">>> Locked.\n";
                write(requestP[conn_fd].conn_fd, "Error retrieving seat data.\n", 30);
            }

        } else {
            // If the input is out of range, prompt the user again without terminating the loop
            // const char* invalid_op_msg = ">>> Invalid operation.\n";
            write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg));
            continue;
        }

    }

#elif defined WRITE_SERVER
        write(requestP[conn_fd].conn_fd, write_shift_msg, strlen(write_shift_msg));

        int ret = handle_read(&requestP[conn_fd]); 
        //handle_read：讀client input,讀到它的internal buffer
	    if (ret < 0) { // -1: read failed user還沒寫就斷線，就沒寫到
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        } else if (ret == 0) { // 0: read EOF (client down)
            continue;
        }

        // Client input is in requestP[conn_fd].buf, convert it to an integer
        requestP[conn_fd].booking_info.shift_id = atoi(requestP[conn_fd].buf);
        //requestP[conn_fd].buf = 0;//clean up

        // Error handling: Check if the input is within the valid range
        if (requestP[conn_fd].booking_info.shift_id >= TRAIN_ID_START && requestP[conn_fd].booking_info.shift_id <= TRAIN_ID_END) {
            // Get the index for the train number (e.g., 902001 -> 0, 902005 -> 4)
            int train_index = requestP[conn_fd].booking_info.shift_id - TRAIN_ID_START;

            if(If_Train_Full[train_index] == 1){
                write(requestP[conn_fd].conn_fd, full_msg, strlen(full_msg));// full_msg = ">>> The shift is fully booked.\n";
            } else {// the train is not full
                // Send the formatted booking info to the client
                char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                snprintf(info_buf, sizeof(info_buf),"\nBooking info\n|- Shift ID: %d\n|- Chose seat(s):\n|- Paid:\n", requestP[conn_fd].booking_info.shift_id);
                write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));

                while(1){
                    write(requestP[conn_fd].conn_fd, write_seat_msg, strlen(write_seat_msg));
                    //write_seat_msg = "Select the seat [1-40] or type \"pay\" to confirm: ";

                    int seat_ret = handle_read(&requestP[conn_fd]); 
                    printf("seat_ret = %d\n", seat_ret);
                    if (seat_ret < 0) { 
                        fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                        continue;
                    } else if (seat_ret == 0) { 
                        continue;
                    }

                    // Convert the seat input to an integer (seat number)
                    int selected_seat = atoi(requestP[conn_fd].buf);
                    printf("selected seat = %d\n", selected_seat);
                    // Function to input train number and seat, then connect with enum SEAT 
                    enum SEAT seat_availability = INITIAL;
                    int seat_part = if_seat_available (requestP[conn_fd].booking_info.shift_id, selected_seat, &seat_availability);

                    // Check if the seat number is valid (1-40)
                    if (selected_seat >= 1 && selected_seat <= SEAT_NUM && seat_part ==1) {
                        // Check the seat status
                        if (seat_availability == EMPTY) {
                            //*********要加write loc ********/
                            //reserving the seat now
                            char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                            snprintf(info_buf, sizeof(info_buf),"\nBooking info\n|- Shift ID: %d\n|- Chose seat(s):%d\n|- Paid:\n", requestP[conn_fd].booking_info.shift_id, selected_seat);
                            write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));

                        } else if (seat_availability == PAID) {
                            // Seat is already booked
                            write(requestP[conn_fd].conn_fd, seat_booked_msg, strlen(seat_booked_msg));
                        } else if (seat_availability == CHOSEN) {
                            // Seat is reserved by someone else
                            write(requestP[conn_fd].conn_fd, lock_msg, strlen(lock_msg));
                        }
                    } else {
                        // Invalid seat number
                        write(requestP[conn_fd].conn_fd, "Invalid seat number.\n", strlen("Invalid seat number.\n"));
                    }
                }
            }

        } else {
            write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg)); 
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
        reqP->booking_info.seat_stat[i] = EMPTY;
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