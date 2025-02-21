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
    if (r == 0) return 0; // Soft End-->have to release loc if there was on before
    char* p1 = strstr(buf, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");   // \n
        if (p1 == NULL) {
            if (!strncmp(buf, IAC_IP, 2)) {
                // Client presses ctrl+C, regard as disconnection
                fprintf(stderr, "Client presses ctrl+C....\n");
                return 0; // Hard End-->have to release loc if there was on before
            }
        }
    }

    len = p1 - buf + 1;
    memmove(reqP->buf, buf, len); //為何不能直接寫進req.buf?因為好的code style應該要有buf過度，可以避免將錯誤的資訊直接寫進req.buf之中。確定好了這個buf的資料是對的之後，才用memmove去copy data
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;


    if (strcmp(reqP->buf, "exit") == 0){
        write(reqP->conn_fd, exit_msg, strlen(exit_msg));
        return 0; //if handle_read return 0 表示client要exit -->have to release loc if there was on before
    } else {
        return 1;
    }
}

int train_number_to_fd (int train_number) {
    int train_index = train_number - TRAIN_ID_START;
    int train_fd_no = trains[train_index].file_fd;
    return train_fd_no;
}

int unloc(int train_number, int selected_seat){
    /* 
       return -1: 沒有順利unlock
       return 1: 有順利unlock
    */
    int train_fd = train_number_to_fd (train_number);
    struct flock fl = {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = (selected_seat-1)*2,
        .l_len = 1
    };
    if (fcntl(train_fd, F_SETLK, &fl) == -1){//return -1 表示 unlock失敗
        printf("The file didn't have lock before. Fail to unlock.\n");
        return -1;
    }
    return 1; // 有順利unlock
}

#ifdef READ_SERVER

int read_loc(int train_number, int selected_seat){//針對單一位置
    /* return 0: 沒有拿到loc，"因為有其他人已經拿到loc"
       return -1: 沒有拿到loc，"其他的error"
       return 1: 有順利拿到loc
    */
    int train_fd = train_number_to_fd (train_number);
    struct flock fl = {
        .l_type = F_RDLCK,
        .l_whence = SEEK_SET,
        .l_start = (selected_seat-1)*2,
        .l_len = 1
    };
    if (fcntl(train_fd, F_SETLK, &fl) == -1){
        //if (errno == EAACES || errno == EAGAIN){
        if (errno == EAGAIN){
            return 0; // 這邊已經有人拿到loc
        } else {
            return -1; // random fail
        }
    }
    return 1;
}

int read_loc_read(int train_number, char* seat_availability_msg, size_t msg_len) {//針對多個位置上read_loc並且將該位置讀出來
    /* XXreturn 0: 沒有拿到loc，"因為有其他人已經拿到loc"
       return -1: 沒有拿到loc/沒有順利讀取，"其他的error"
       return 1: 有順利拿到loc
    */
    //memset(seat_availability_msg, 0, MAX_MSG_LEN);//?有需要歸零嗎？下一次就直接蓋過去
    int train_fd = train_number_to_fd (train_number);
    int selected_seat = 0; //i
    int if_read_loc_success;//讀取該位置的read loc這件事有沒有成功，return 0: 沒有拿到loc，"因為有其他人已經拿到loc“;return -1: 沒有拿到loc，"其他的error";return 1: 有順利拿到loc
    int if_read_unloc_success;

    for(int i=1;i<=SEAT_NUM;i++){//這個i是client端的座位號，由1開始
        if_read_loc_success = read_loc(train_number, i);
        
        if(if_read_loc_success == 1){ // 有順利拿到read loc，要unloc
            lseek(train_fd, (i-1)*2, SEEK_SET);
            char seat_buffer[2] = {0};//seat_buffer最少要給到2，因為最後會補\0
            int bytes_read = read(train_fd, seat_buffer, 1); //因為最後有\0，所以只吃一個byte
            if (bytes_read <= 0) {
                printf("Error reading seat data. Seat number: %d\n", i);
                if_read_unloc_success = unloc(train_number, i);
                if(if_read_unloc_success == -1){
                    printf("read_unloc failed at %d.\n", i);
                    return -1;
                }
                return -1;
            }
            if((i%4)==0){ // snprintf第一個para是寫入的位置，第二個para不能是固定的(如果在迴圈中)，不然會寫到其他記憶體
//printf("here1(%d,%d,%d,%d):[%d,%d]\n",train_number, if_read_loc_success, i, bytes_read, seat_buffer[0], seat_buffer[1]);
                snprintf(seat_availability_msg + (i-1) * 2, msg_len - ((i-1) * 2), "%s\n", seat_buffer);
            } else {
//printf("here2(%d,%d,%d,%d):[%d,%d]\n",train_number, if_read_loc_success, i, bytes_read, seat_buffer[0], seat_buffer[1]);
                snprintf(seat_availability_msg + (i-1) * 2, msg_len - ((i-1) * 2), "%s ", seat_buffer);
            }
            if_read_unloc_success = unloc(train_number, i);
            if(if_read_unloc_success == -1){
                printf("read_unloc failed at %d.\n", i);
                return -1;
            }

        } else if(if_read_loc_success == 0){//沒有順利拿到loc，因為該位置有人正在拿著write_loc
            if((i%4)==0){ // snprintf第一個para是寫入的位置，第二個para不能是固定的(如果在迴圈中)，不然會寫到其他記憶體
//printf("here3(%d,%d,%d)\n",train_number, if_read_loc_success, i);
                snprintf(seat_availability_msg + (i-1) * 2, msg_len - ((i-1) * 2), "2\n");
            } else {
//printf("here4(%d,%d,%d)\n",train_number, if_read_loc_success, i);
                snprintf(seat_availability_msg + (i-1) * 2, msg_len - ((i-1) * 2), "2 ");
            }

        } else if (if_read_loc_success == -1){
            printf("read_loc_success return -1:有其他error在第%d位置\n",i);
            return -1;
        }
    }
//printf("here:[%s]\n[%s]\n",seat_availability_msg, seat_availability_msg+2);
    return 1;//有順利跑完read_loc_read
}
#else //WRITE_SERVER
int if_train_full(int train_number){ //會去read給訂車號的檔案，全部都是1才算full
//return -1:error; return 0: not full; return 1:full
    int train_fd = train_number_to_fd(train_number);
    char seat_buffer[2] = {0};//seat_buffer最少要給到2，因為最後會補\0
    int num =0;//這是用來計數吃到了幾次1，吃到了40

    for(int i=1;i<=SEAT_NUM;i++){//這個i是client端的座位號，由1開始
        lseek(train_fd, (i-1)*2, SEEK_SET);
        int bytes_read = read(train_fd, seat_buffer, 1); //因為最後有\0，所以只吃一個byte
        if (bytes_read <= 0) {
            printf("Error reading seat data. Seat number: %d\n", i);
            return -1;
        }
        if(seat_buffer[0] == '1') {
            num++;
        } else { 
            return 0;
        }
    }
    if(num == 40){
        return 1;
    }
}

int if_seat_available (int train_number, int seat_number, enum SEAT* seat_availability) {
    //return 1: the function retrieve the seat status successfully
    //return -1: the function cannot retrieve the seat status successfully
    // enum SEAT: out parameter
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
    } else { // Cannot retrieve seat availability successfully.
        return -1;
    }
}

int cur_seat_stat(request* reqP, char* seat_str, int* seat_array) {//fail:-1; success, return how many seats had been reserved
    // Check if input pointers are valid
    if (reqP == NULL || seat_str == NULL) {
        return -1;  // Fail if input is invalid
    }

    int seat_reserved_num = 0;

    //initialize
    for(int i = 0; i<= SEAT_NUM; i++){
        seat_str[i] =0;
    }

    char temp[8] = {0};  // Temporary buffer to hold each seat number
    // Traverse the cur_chosen_seat array to concatenate the chosen seats
    for (int i = 1; i <= SEAT_NUM; i++) {
        if (seat_array[i] == 1) {
            // If seat_str is not empty, add a comma before concatenating the next seat number
            if (seat_str[0] != '\0') {
                strcat(seat_str, ",");
            }
            // Add the seat number to seat_str
            snprintf(temp, sizeof(temp), "%d", i);  // Convert the seat number to a string
            strcat(seat_str, temp);  // Concatenate the seat number to seat_str

            seat_reserved_num++;
        }
    }

    // If seat_str is still empty, no seats were chosen, return failure
    if (seat_str[0] == '\0') {
        return 0;  // No chosen seats, return fail
    }

    return seat_reserved_num;  // Success, seat_str contains the concatenated seat numbers
}

int modify_booked_seat(int train_number, int seat_number){//一次只改一個number
    int train_fd = train_number_to_fd(train_number);
    printf("seat_number:%d\n", seat_number);
    printf("train_number:%d\n", train_number);
    printf("train_fd:%d\n", train_fd);

    int lseek_n = lseek(train_fd, (seat_number-1)*2 , SEEK_SET);
    if(lseek_n == -1){
        printf("errno:%d\n", errno);
        printf("lseek() fail.\n");
        return -1;
    }
    char seat_status[2] = {0};  // Buffer to hold the formatted string
    snprintf(seat_status, sizeof(seat_status), "%d", PAID);
    int write_n = write(train_fd, seat_status, 1);
    if(write_n == -1){
        printf("write() fail.\n");
        printf("errno:%d\n", errno);
        return -1;
    }

    return 1; //return 1-->successful
}

int write_loc(int train_number, int selected_seat){
    /* return 0: 沒有拿到loc，"因為有其他人已經拿到loc"
       return -1: 沒有拿到loc，"其他的error"
       return 1: 有順利拿到loc
    */
    int train_fd = train_number_to_fd (train_number);
    struct flock fl = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = (selected_seat-1)*2,
        .l_len = 1
    };
    if (fcntl(train_fd, F_SETLK, &fl) == -1){
        if (errno == EACCES || errno == EAGAIN){
            return 0; //有人已經在這邊有拿loc
        } else {
            return -1;//random loc
        }
    }
    return 1;
}

#endif

int main(int argc, char** argv) {
    //int Train_seat_left[5] = {40, 0, 21, 22, 26}; //How many seats left in each train. (e.g., 902001 -> 0, 902005 -> 4)
    //int If_Train_Full[5] = {0, 1, 0, 0, 0};//Full:1, Not-Full:0
    int train_index;

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
    // Buffer for seat availability message
    char seat_availability_msg[MAX_MSG_LEN] = {0};

    while (1) {
        // Now send the shift selection message if it's a READ_SERVER
        write(requestP[conn_fd].conn_fd, read_shift_msg, strlen(read_shift_msg));

        int ret = handle_read(&requestP[conn_fd]); //將User input 吃進&requestP[conn_fd]中
        //handle_read：讀client input,讀到它的internal buffer
	    if (ret < 0) { // -1: read failed user還沒寫就斷線，就沒寫到
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
            continue;
        } else if (ret == 0) { // 0: read EOF (client down)
            break;
        }

        // Client input is in requestP[conn_fd].buf, convert it to an integer
        requestP[conn_fd].booking_info.shift_id = atoi(requestP[conn_fd].buf);
        train_index = requestP[conn_fd].booking_info.shift_id - TRAIN_ID_START;

        if (requestP[conn_fd].booking_info.shift_id >= TRAIN_ID_START && requestP[conn_fd].booking_info.shift_id <= TRAIN_ID_END) {
            //這邊有將原本的print_train_info改成read_loc_read
            // read_loc_read 做的事是讀檔，並且將檔案的內容讀進buffer(seat_availability_msg)中，然後後續透過write，將這個msg output到那個connection的client output上 (requestP[conn_fd].conn_fd)
            //int read_loc_read(int train_number, char* seat_availability_msg, size_t msg_len)
            int read_loc_read_part = read_loc_read(requestP[conn_fd].booking_info.shift_id, seat_availability_msg, sizeof(seat_availability_msg));
            if(read_loc_read_part == 1){
                write(requestP[conn_fd].conn_fd, seat_availability_msg, strlen(seat_availability_msg));
            } else {//read_loc_read_part == -1
                printf("read_loc_success return -1:有error\n");
            }

        } else {
            // If the input is out of range, prompt the user again without terminating the loop
            // const char* invalid_op_msg = ">>> Invalid operation.\n";
            write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg));
            break;
        }
    }

#elif defined WRITE_SERVER
Retry_train_number:
        memset(requestP[conn_fd].booking_info.cur_chosen_seat, 0, sizeof(requestP[conn_fd].booking_info.cur_chosen_seat));
        memset(requestP[conn_fd].booking_info.all_paid_seat, 0, sizeof(requestP[conn_fd].booking_info.all_paid_seat));
        char seat_str[MAX_MSG_LEN] = {0};  // Initialize an empty string to store seat numbers
        char paid_seat_str[MAX_MSG_LEN] = {0}; // Initialize an empty string to store seat numbers (for all paid seats)
        write(requestP[conn_fd].conn_fd, write_shift_msg, strlen(write_shift_msg));

        int ret = handle_read(&requestP[conn_fd]); 

        requestP[conn_fd].booking_info.shift_id = atoi(requestP[conn_fd].buf);
        train_index = requestP[conn_fd].booking_info.shift_id - TRAIN_ID_START; // Get the index for the train number (e.g., 902001 -> 0, 902005 -> 4)
        //requestP[conn_fd].buf = 0;//clean up

        // Error handling: Check if the input is within the valid range
        if (ret <0) { // -1: read failed user還沒寫就斷線，就沒寫到
            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
        } else if (ret == 0){
            printf("Client exit;");
        } else if(ret >0 && requestP[conn_fd].booking_info.shift_id >= TRAIN_ID_START && requestP[conn_fd].booking_info.shift_id <= TRAIN_ID_END) {
            if(if_train_full(requestP[conn_fd].booking_info.shift_id) == 1){
                write(requestP[conn_fd].conn_fd, full_msg, strlen(full_msg));// full_msg = ">>> The shift is fully booked.\n";
                goto Retry_train_number;
            } else {// the train is not full
                // Send the formatted booking info to the client
                char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                snprintf(info_buf, sizeof(info_buf),"\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): \n|- Paid: \n\n", requestP[conn_fd].booking_info.shift_id);
                write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));
                while(1){
                    write(requestP[conn_fd].conn_fd, write_seat_msg, strlen(write_seat_msg));
                    //write_seat_msg = "Select the seat [1-40] or type \"pay\" to confirm: ";

                    //如果user寫進的數字，已經有在cur_chosen_seat_no裡面的話，就要cancel，不然的話就做原本的
                    int seat_ret = handle_read(&requestP[conn_fd]); 
                    //printf("seat_ret = %d\n", seat_ret);
                    if (seat_ret < 0) { 
                        fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                        continue;
                    } else if (seat_ret == 0) {
                        break;
                    }

                    // Convert the seat input to an integer (seat number)
                    int selected_seat = atoi(requestP[conn_fd].buf);
                    if(strcmp(requestP[conn_fd].buf, "pay") != 0 && (selected_seat > SEAT_NUM || selected_seat < 1)){ 
                        write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg)); //selected seat number not in valid range
                        break;
                    }

                    if(strcmp(requestP[conn_fd].buf, "pay") != 0 && requestP[conn_fd].booking_info.cur_chosen_seat[selected_seat] == 0) {//這個位子還沒被選過
                        //printf("selected seat = %d\n", selected_seat);
                        if (selected_seat >= 1 && selected_seat <= SEAT_NUM) { //seat part fail 未處理
                        // Check if the seat number is valid (1-40)
                            // 先直接去拿write lock
                            // 如果不直接拿write_lock的話，讀到0去拿write loc的中間可能有其他人會進來拿write lock
                            int if_write_loc_success = write_loc(requestP[conn_fd].booking_info.shift_id, selected_seat);
                            if(if_write_loc_success == 1){
                                // Function to input train number and seat, then connect with enum SEAT 
                                enum SEAT seat_availability = INITIAL;
                                int if_seat_available_successfully_retrieve = if_seat_available (requestP[conn_fd].booking_info.shift_id, selected_seat, &seat_availability);
                                if (if_seat_available_successfully_retrieve == 1 && seat_availability == EMPTY){
                                    requestP[conn_fd].booking_info.cur_chosen_seat[selected_seat] = 1;//1-based

                                    cur_seat_stat(requestP, seat_str, requestP[conn_fd].booking_info.cur_chosen_seat);
                                    char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                                    snprintf(info_buf, sizeof(info_buf),"\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): %s\n|- Paid: %s\n\n", requestP[conn_fd].booking_info.shift_id, seat_str, paid_seat_str);
                                    write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));
                                    //後續pay的時候再release lock

                                } else if(if_seat_available_successfully_retrieve == 1 && seat_availability == PAID){// cur_seat if paid and release lock
                                    // Seat is already booked
                                    cur_seat_stat(requestP, seat_str, requestP[conn_fd].booking_info.cur_chosen_seat);//testcase 2-7 fail
                                    char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                                    snprintf(info_buf, sizeof(info_buf),"%s\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): %s\n|- Paid: %s\n\n", seat_booked_msg, requestP[conn_fd].booking_info.shift_id, seat_str, paid_seat_str);
                                    write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));

                                    unloc(requestP[conn_fd].booking_info.shift_id, selected_seat);//release lock
                                } else if(if_seat_available_successfully_retrieve == -1){ // should release lock
                                    printf("Cannot retrieve seat availability successfully.");

                                    unloc(requestP[conn_fd].booking_info.shift_id, selected_seat);//release lock
                                }
                            } else if (if_write_loc_success == 0){
                                // Seat is reserved by someone else
                                // 試圖拿write lock但是被refuse:有可能現在是在read lock 或 write lock
                                char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                                snprintf(info_buf, sizeof(info_buf),"%s\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): %s\n|- Paid: %s\n\n", lock_msg, requestP[conn_fd].booking_info.shift_id, seat_str, paid_seat_str);
                                write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));
                            } else if (if_write_loc_success == -1){
                                printf("write_loc return -1: 沒有拿到loc, 有其他的error\n");
                            }
                        } else {
                            // Invalid seat number
                            write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg));
                        }
                    } else if(strcmp(requestP[conn_fd].buf, "pay") != 0 && requestP[conn_fd].booking_info.cur_chosen_seat[selected_seat] == 1){ // requestP.booking_info.cur_chosen_seat[selected_seat] == 1
                        //The client want to cancel the reserved seat 這個位子被選過，現在要cancel：要記得realease loc!!
                        requestP[conn_fd].booking_info.cur_chosen_seat[selected_seat] = 0;//1-based
                        unloc(requestP[conn_fd].booking_info.shift_id, selected_seat);//release lock

                        cur_seat_stat(requestP, seat_str, requestP[conn_fd].booking_info.cur_chosen_seat);

                        char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                        snprintf(info_buf, sizeof(info_buf), "%s\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): %s\n|- Paid: %s\n\n", cancel_msg, requestP[conn_fd].booking_info.shift_id, seat_str, paid_seat_str);
                        write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));

                        //*******Release write loc of the canceled seat number */

                    } else if (strcmp(requestP[conn_fd].buf, "pay") == 0 && cur_seat_stat(requestP, seat_str, requestP[conn_fd].booking_info.cur_chosen_seat) == 0){ // strlen(requestP[conn_fd].buf) == 3 -->the client click 'pay'
                        char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                        snprintf(info_buf, sizeof(info_buf), "%s\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): \n|- Paid: %s\n\n", no_seat_msg, requestP[conn_fd].booking_info.shift_id, paid_seat_str);
                        write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));
                    } else if (strcmp(requestP[conn_fd].buf, "pay") == 0 && cur_seat_stat(requestP, seat_str, requestP[conn_fd].booking_info.cur_chosen_seat) > 0) {
                        // cur_chosen_seat裡有東西，client要買車票
                        // for(int i = 1; i< (SEAT_NUM+1);i++){
                        //     printf("requestP[conn_fd].booking_info.cur_chosen_seat[%d]:%d\n", i, requestP[conn_fd].booking_info.cur_chosen_seat[i]);
                        // }
                        // printf("selected_seat:%d\n",selected_seat);
                        for(int i = 1; i< (SEAT_NUM+1);i++){
                            if(requestP[conn_fd].booking_info.cur_chosen_seat[i]==1){
                                int modified_seat_n = (modify_booked_seat(requestP[conn_fd].booking_info.shift_id, i));
                                requestP[conn_fd].booking_info.all_paid_seat[i] = 1;
                                requestP[conn_fd].booking_info.cur_chosen_seat[i] = 0;
                                unloc(requestP[conn_fd].booking_info.shift_id, i);//release lock selected seat i
                            } //IGNORE ERROR
                        }
                        // for(int i = 1; i< (SEAT_NUM+1);i++){
                        //     printf("requestP[conn_fd].booking_info.all_paid_seat[%d]:%d\n", i, requestP[conn_fd].booking_info.cur_chosen_seat[i]);
                        // }

                        cur_seat_stat(requestP, paid_seat_str, requestP[conn_fd].booking_info.all_paid_seat);
                        char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                        snprintf(info_buf, sizeof(info_buf), "%s\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): \n|- Paid: %s\n\n%s", book_succ_msg, requestP[conn_fd].booking_info.shift_id, paid_seat_str, write_seat_or_exit_msg);
                        write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));

                        int ret = handle_read(&requestP[conn_fd]); 
                        //handle_read：讀client input,讀到它的internal buffer
                        if (ret < 0) { // -1: read failed user還沒寫就斷線，就沒寫到
                            fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                            continue;
                        } else if (ret == 0) { // 0: read EOF (client down)
                            break;
                        }

                        if(ret == 1){ // read successfully
                            if (strcmp(requestP[conn_fd].buf, "seat") == 0) {
                                char info_buf[MAX_MSG_LEN];  // Buffer to hold the formatted string
                                snprintf(info_buf, sizeof(info_buf), "\nBooking info\n|- Shift ID: %d\n|- Chose seat(s): \n|- Paid: %s\n\n", requestP[conn_fd].booking_info.shift_id, paid_seat_str);
                                write(requestP[conn_fd].conn_fd, info_buf, strlen(info_buf));
                                continue;
                            } else {
                                write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg));
                                break;
                            }
                        }
                    }
                }
            }
        } else if(ret>0 && (requestP[conn_fd].booking_info.shift_id < TRAIN_ID_START || requestP[conn_fd].booking_info.shift_id > TRAIN_ID_END)){
            write(requestP[conn_fd].conn_fd, invalid_op_msg, strlen(invalid_op_msg)); 
        }

        //WRITE SERVER:有人有chose seat, 但是還沒pay就離開(exit)，那麼那個chosen的位置要清空並且原本拿的write lock要放掉
        //printf("leaving\n");
        //write(requestP[conn_fd].conn_fd, "leaving", strlen("leaving")); 
        for(int i = 1;i<=SEAT_NUM;i++){
            if(requestP[conn_fd].booking_info.cur_chosen_seat[i] == 1){
                //char bufffff[MAX_MSG_LEN] = {0};
                //snprintf(bufffff, sizeof(bufffff), "unlocking %d\n", i);
                //write(requestP[conn_fd].conn_fd, bufffff, strlen(bufffff)); 
                requestP[conn_fd].booking_info.cur_chosen_seat[i] = 0;
                unloc(requestP[conn_fd].booking_info.shift_id, i);
            }
        }
    
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
    
    init_request(&requestP[conn_fd]);
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
    
    for (int i = 0; i < (SEAT_NUM+1); i++) {
        reqP->booking_info.cur_chosen_seat[i] = 0;
        //reqP->booking_info.cur_paid_seat[i] = 0;
        reqP->booking_info.all_paid_seat[i] = 0;
    }
    
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