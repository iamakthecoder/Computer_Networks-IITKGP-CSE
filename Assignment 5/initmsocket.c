#include "msocket.h"

// Global variables
int shmid_SM = -1; // Shared memory ID for MTPSocketEntry SM
int shmid_sock_info = -1; // Shared memory ID for SOCK_INFO
MTPSocketEntry *SM = NULL; // Pointer to the shared memory segment for MTPSocketEntry SM
SOCK_INFO *sock_info = NULL; // Pointer to the shared memory segment for SOCK_INFO
sem_t *Sem1 = NULL; // Semaphore for synchronization (used for m_socket and m_bind calls)
sem_t *Sem2 = NULL; // Semaphore for synchronization (used for m_socket and m_bind calls)
sem_t *SM_mutex = NULL; // Semaphore for mutual exclusion (used for accessing shared resource SM)
int num_messages = 0; // Number of messages sent
int num_transmissions = 0; // Number of transmissions made

// Structure for persistence timer
typedef struct {
    int flag; //to indicate the no. of timeouts, to set the exponential backoff waiting time (within an upper limit)
    time_t last_time; //latest time when ACK with window size 0 was received
    int ack_seq_no; //sequence number of the ACK message with 0 window size
} Persistence_Timer_;

// Array to store persistence timers for each socket
Persistence_Timer_ persistence_timer[MAX_MTP_SOCKETS];

// Cleanup function to be called on exit
void cleanup_on_exit() {
    // Calculate and print average number of transmissions per message
    if(num_messages>0){
        double avg_trans_per_msg = (double)(num_transmissions) / (num_messages);
        printf("\n==>> The average number of transmissions made to send each message (for p = %lf): %lf\n", P, avg_trans_per_msg);
    }
    else{
        printf("\n=> No message has been sent.\n");
    }

    // Detach the shared memory segments
    if (SM != NULL) {
        if (shmdt(SM) == -1) {
            perror("shmdt(SM)");
        }
    }
    if (sock_info != NULL) {
        if (shmdt(sock_info) == -1) {
            perror("shmdt(sock_info)");
        }
    }

    // Destroy the semaphores
    if (Sem1 != NULL){
        sem_close(Sem1);
        sem_unlink("/Sem1");
    }
    if (Sem2 != NULL){ 
        sem_close(Sem2);
        sem_unlink("/Sem2");
    }
    if (SM_mutex != NULL) {
        sem_close(SM_mutex);
        sem_unlink("/SM_mutex");
    }

    // Delete shared memory segments
    if (shmid_SM != -1) {
        if (shmctl(shmid_SM, IPC_RMID, NULL) == -1) {
            perror("shmctl(IPC_RMID)");
        }
    }
    if (shmid_sock_info != -1) {
        if (shmctl(shmid_sock_info, IPC_RMID, NULL) == -1) {
            perror("shmctl(IPC_RMID)");
        }
    }

    printf("Shared memory and semaphores detached and destroyed successfully.\n");
}

// Signal handler for SIGINT (Ctrl+C) and SIGQUIT
void signal_handler(int signum) {
    if (signum == SIGINT)
        printf("\nReceived SIGINT. Detaching shared memory and quitting.\n");
    else if (signum == SIGQUIT)
        printf("\nReceived SIGQUIT. Detaching shared memory and quitting.\n");
    // Detach the shared memory segments
    exit(EXIT_SUCCESS);
}

// Function to handle incoming messages and send ACKs
void *thread_R() {

    fd_set readfds;
    int max_sd, activity;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    Message msg;

    //structure to set the timeout over select() function
    struct timeval timeout;
    timeout.tv_sec = T;
    timeout.tv_usec = 0;

    //initialization with the available sockets to listen on, using select()
    FD_ZERO(&readfds);
    if (sem_wait(SM_mutex) == -1) {
        perror("sem_wait");
        return NULL;
    }
    max_sd = -1;
    for (int i = 0; i < MAX_MTP_SOCKETS; i++) {
        if(SM[i].socket_alloted){
            FD_SET(SM[i].udp_socket_id, &readfds);
            if (SM[i].udp_socket_id > max_sd) {
                max_sd = SM[i].udp_socket_id;
            }
        }
    }
    if (sem_post(SM_mutex) == -1) {
        perror("sem_post");
        return NULL;
    }
    
    // Main loop for handling incoming messages
    while (1) {
        // set the socket set to listen on
        fd_set temp = readfds;

        // Use select to monitor socket activity
        activity = select(max_sd + 1, &temp, NULL, NULL, &timeout);

        // if select() returns negative value, reset the socket set
        if (activity < 0) {
            perror("select");
            if (sem_wait(SM_mutex) == -1) {
                perror("sem_wait");
                return NULL;
            }
            FD_ZERO(&readfds);
            max_sd = -1;
            for(int i=0; i<MAX_MTP_SOCKETS; i++){
                if(SM[i].socket_alloted){
                    FD_SET(SM[i].udp_socket_id, &readfds);
                    if (SM[i].udp_socket_id > max_sd) {
                        max_sd = SM[i].udp_socket_id;
                    }
                }
            }
            if (sem_post(SM_mutex) == -1) {
                perror("sem_post");
                return NULL;
            }
            continue;
        }

        //if timeout occurs, reset the socket set, to include any new MTP sockets that have been formed
        if(activity == 0){
            //reset the timer
            timeout.tv_sec = T;
            timeout.tv_usec = 0;

            if (sem_wait(SM_mutex) == -1) {
                perror("sem_wait");
                return NULL;
            }
            // Add UDP sockets to the set
            FD_ZERO(&readfds);
            max_sd = -1;
            for (int i = 0; i < MAX_MTP_SOCKETS; i++) {
                if(SM[i].socket_alloted){
                    
                    FD_SET(SM[i].udp_socket_id, &readfds);
                    if (SM[i].udp_socket_id > max_sd) {
                        max_sd = SM[i].udp_socket_id;
                    }
                    //if the 'nospace' flag was set but currently window is available, send an ACK to update with the new window size
                    if(SM[i].recv_window.nospace==1 && SM[i].recv_window.window_size>0){
                        client_addr = SM[i].destination_addr;
                        Message ack_msg;
                        ack_msg.header.msg_type = 'A';
                        ack_msg.header.seq_no = (SM[i].recv_window.next_seq_no==1)? 15 : (SM[i].recv_window.next_seq_no-1);
                        sprintf(ack_msg.msg, "%d", SM[i].recv_window.window_size);
                        sendto(SM[i].udp_socket_id, &ack_msg, sizeof(Message), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
                        SM[i].recv_window.nospace = 0;
                    }

                }
            }
            if (sem_post(SM_mutex) == -1) {
                perror("sem_post");
                return NULL;
            }

            continue;
        }

        // Check each UDP socket for incoming messages
        for (int i = 0; i < MAX_MTP_SOCKETS; i++) {
            if(sem_wait(SM_mutex) == -1){
                perror("sem_wait");
                return NULL;
            }
            
            if (FD_ISSET(SM[i].udp_socket_id, &temp)) {
                
                // Receive message from the UDP socket

                ssize_t recv_len = recvfrom(SM[i].udp_socket_id, &msg, sizeof(Message), 0,
                                            (struct sockaddr *)&client_addr, &addr_len);
                //if the socket has been closed, drop the message
                if(SM[i].socket_alloted<=0){
                    FD_CLR(SM[i].udp_socket_id, &readfds);
                    sem_post(SM_mutex);
                    continue;
                }
                //if error in receiving the message
                if (recv_len < 0) {
                    if(sem_post(SM_mutex)==-1){
                        perror("sem_post");
                        return NULL;
                    }
                    perror("recvfrom");
                    continue;
                }

                // drop the received message with probability P
                if(dropMessage(P)){

                    if(sem_post(SM_mutex)==-1){
                        perror("sem_post");
                        return NULL;
                    }
                    continue;
                }

                // check if the message is received from the paired source only, if not, drop the message
                if(client_addr.sin_addr.s_addr != SM[i].destination_addr.sin_addr.s_addr || client_addr.sin_port != SM[i].destination_addr.sin_port){
                    if(sem_post(SM_mutex)==-1){
                        perror("sem_post");
                        return NULL;
                    }
                    continue;
                }

                // Process the received message...
                // If the received message is a DATA message
                if(msg.header.msg_type=='D'){   
                    // If the received message is an in-order message
                    if(msg.header.seq_no == SM[i].recv_window.next_seq_no && SM[i].recv_window.window_size>0){
                        int idx = SM[i].recv_window.index_to_write;
                        SM[i].recv_window.recv_buff[idx].ack_no = msg.header.seq_no;
                        memcpy(SM[i].recv_window.recv_buff[idx].message, msg.msg, MAX_MSG_SIZE); //write the message to the buffer
                        SM[i].recv_window.window_size--;
                        if(SM[i].recv_window.window_size==0)SM[i].recv_window.nospace = 1;
                        //now find the last in order message received and the next index to write
                        int last_in_order = msg.header.seq_no;
                        int next_idx_to_write = SM[i].recv_window.index_to_write;
                        for(int k=1; k<RECEIVER_MSG_BUFFER; k++){
                            if(SM[i].recv_window.window_size==0)break;
                            int new_idx = (SM[i].recv_window.index_to_write+k)%RECEIVER_MSG_BUFFER;
                            if(SM[i].recv_window.recv_buff[new_idx].ack_no==-1){
                                break;
                            }
                            int next_exp_seq_no = (last_in_order==15)? 1 : (last_in_order+1);
                            if(SM[i].recv_window.recv_buff[new_idx].ack_no!=next_exp_seq_no)break;
                            last_in_order = next_exp_seq_no;
                            next_idx_to_write = new_idx;
                            SM[i].recv_window.window_size--;
                            if(SM[i].recv_window.window_size==0)SM[i].recv_window.nospace = 1;
                        }
                        SM[i].recv_window.index_to_write = (next_idx_to_write+1)%RECEIVER_MSG_BUFFER;
                        SM[i].recv_window.next_seq_no = (last_in_order==15)? 1 : (last_in_order+1);

                        //send the corresponding ACK for the message received
                        Message ack;
                        ack.header.msg_type = 'A';
                        ack.header.seq_no = last_in_order;
                        sprintf(ack.msg, "%d", SM[i].recv_window.window_size);
                        sendto(SM[i].udp_socket_id, &ack, sizeof(Message), 0, (struct sockaddr*)&client_addr, addr_len);

                        if(sem_post(SM_mutex)==-1){
                            perror("sem_post");
                            return NULL;
                        }
                        continue;
                    }
                    //If not in-order, check if the received message is in-window (though out-of-order)
                    int inWindow = 0;
                    int expected_seq_no = SM[i].recv_window.next_seq_no;
                    int new_idx = -1;
                    for(int k=0; k<SM[i].recv_window.window_size; k++){
                        if(msg.header.seq_no==expected_seq_no){
                            new_idx = (SM[i].recv_window.index_to_write + k)%RECEIVER_MSG_BUFFER;
                            inWindow = 1;
                            break;
                        }
                        expected_seq_no = (expected_seq_no+1)%16;
                        if(expected_seq_no==0)expected_seq_no++;
                    }
                    //If the received message is in-window and out-of-order message
                    if(inWindow){

                        if(SM[i].recv_window.recv_buff[new_idx].ack_no!=msg.header.seq_no){
                            SM[i].recv_window.recv_buff[new_idx].ack_no = msg.header.seq_no;
                            memcpy(SM[i].recv_window.recv_buff[new_idx].message, msg.msg, MAX_MSG_SIZE); //store the message at the required position in the buffer
                        }
                        //send the corresponding ACK for the message received
                        Message ack;
                        ack.header.msg_type = 'A';
                        ack.header.seq_no = (SM[i].recv_window.next_seq_no==1)? 15 : SM[i].recv_window.next_seq_no-1;
                        sprintf(ack.msg, "%d", SM[i].recv_window.window_size);
                        sendto(SM[i].udp_socket_id, &ack, sizeof(Message), 0, (struct sockaddr*)&client_addr, addr_len);

                        if(sem_post(SM_mutex)==-1){
                            perror("sem_post");
                            return NULL;
                        }
                        continue;
                    }
                    //If the received message is out-of-window
                    else{
                        //send the corresponding ACK for the message received (without storing the message in the buffer)
                        Message ack;
                        ack.header.msg_type = 'A';
                        ack.header.seq_no = (SM[i].recv_window.next_seq_no==1)? 15 : SM[i].recv_window.next_seq_no-1;
                        sprintf(ack.msg, "%d", SM[i].recv_window.window_size);
                        sendto(SM[i].udp_socket_id, &ack, sizeof(Message), 0, (struct sockaddr*)&client_addr, addr_len);
                        
                        if(sem_post(SM_mutex)==-1){
                            perror("sem_post");
                            return NULL;
                        }
                        continue;
                    }
                }
                //If the received message is an ACK message
                else if(msg.header.msg_type=='A'){
                    //check if the sequence number of the ACK is in window
                    int idx = SM[i].send_window.window_start_index;
                    int ack_in_window = -1;
                    int len = -1;
                    for(int k = 0; k<SM[i].send_window.window_size; k++){
                        int new_idx = (idx+k)%SENDER_MSG_BUFFER;
                        if(SM[i].send_window.send_buff[new_idx].ack_no==-1)break;
                        if(SM[i].send_window.send_buff[new_idx].ack_no==msg.header.seq_no){
                            ack_in_window = new_idx;
                            len = k;
                            break;
                        }
                    }
                    //if the ACK is received for an in-window message
                    if(ack_in_window!=-1){
                        //update the send window as per the last-in-order message received by receiver (as indicated by the sequence no. of ACK message)
                        for(int k = 0; k<=len; k++){
                            int new_idx = (idx+k)%SENDER_MSG_BUFFER;
                            SM[i].send_window.send_buff[new_idx].ack_no = -1;
                        }
                        SM[i].send_window.window_start_index = (ack_in_window+1)%SENDER_MSG_BUFFER;
                    }
                    //update the send window size as per the window size indicated by the ACK message
                    SM[i].send_window.window_size = atoi(msg.msg);

                    //To handle the 'ACK with window size 0' deadlock situation... (by using the 'Persistence Timer' solution)
                    //if the window size is greater than 0, turn off the persistence timer
                    if (SM[i].send_window.window_size>0){
                        persistence_timer[i].flag = 0;
                    }
                    //if the window size is equal to 0, set the persistence timer
                    if(SM[i].send_window.window_size==0){
                        if(persistence_timer[i].flag==0)persistence_timer[i].flag=1;
                        persistence_timer[i].last_time = time(NULL);
                        persistence_timer[i].ack_seq_no = msg.header.seq_no; //store the sequence no. of the ACK message
                    }

                    if(sem_post(SM_mutex)==-1){
                        perror("sem_post");
                        return NULL;
                    }
                    continue;
                }
                //if the received message is PROBE message (sent when the persistence timer goes off)
                else if(msg.header.msg_type=='P'){
                    //send the corresponding ACK with the current window size
                    Message ack;
                    ack.header.msg_type = 'A';
                    ack.header.seq_no = msg.header.seq_no;
                    sprintf(ack.msg, "%d", SM[i].recv_window.window_size);
                    sendto(SM[i].udp_socket_id, &ack, sizeof(Message), 0, (struct sockaddr*)&client_addr, addr_len);
                    
                    if(sem_post(SM_mutex)==-1){
                        perror("sem_post");
                        return NULL;
                    }
                    continue;
                }


                if(sem_post(SM_mutex)==-1){
                    perror("sem_post");
                    return NULL;
                }
            }else{

                if(sem_post(SM_mutex)==-1){
                    perror("sem_post");
                    return NULL;
                }
            }
        }
    }


    return NULL;
}

// Function to handle sending messages
void *thread_S() {
    time_t current_time;
    struct sockaddr_in addr;

    while (1) {
        // Sleep for some time
        usleep((T / 2) * 700000);

        // Acquire the mutex to access shared resources
        if (sem_wait(SM_mutex) == -1) {
            perror("sem_wait");
            return NULL;
        }

        // Iterate over all sockets
        for (int i = 0; i < MAX_MTP_SOCKETS; i++) {
            if (SM[i].socket_alloted) { // Check if the socket is allocated
                // Check if the persistence timer is set and if it has gone off
                if(persistence_timer[i].flag > 0){
                    int multiplier = (1 << (persistence_timer[i].flag - 1));
                    if(difftime(time(NULL), persistence_timer[i].last_time) >= multiplier * T){
                        // Send probe message
                        Message probe_msg;
                        probe_msg.header.msg_type = 'P';
                        probe_msg.header.seq_no = persistence_timer[i].ack_seq_no;
                        addr = SM[i].destination_addr;
                        memset(probe_msg.msg, 0, sizeof(probe_msg.msg));
                        sendto(SM[i].udp_socket_id, &(probe_msg), sizeof(Message), 0, (struct sockaddr *)&addr, sizeof(addr));
                        persistence_timer[i].last_time = time(NULL);
                        if(persistence_timer[i].flag <= 3) persistence_timer[i].flag++; //to maintain exponential backoff waiting time (within an upper limit)
                        continue;
                    }
                }
                // Iterate over the send window
                for(int offset = 0; offset < SM[i].send_window.window_size; offset++){
                    int idx = (SM[i].send_window.window_start_index + offset) % SENDER_MSG_BUFFER;

                    if(SM[i].send_window.send_buff[idx].ack_no == -1) break;

                    if(SM[i].send_window.send_buff[idx].sent == 0){
                        // Send the message if it hasn't been sent yet
                        addr = SM[i].destination_addr;
                        sendto(SM[i].udp_socket_id, &(SM[i].send_window.send_buff[idx].message), sizeof(Message), 0, (struct sockaddr *)&addr, sizeof(addr));
                        num_messages++;
                        num_transmissions++;
                        SM[i].send_window.send_buff[idx].time = time(NULL);
                        SM[i].send_window.send_buff[idx].sent = 1;
                        continue;
                    }
                    // Check if it's time to resend the message
                    time(&current_time);
                    double time_gap = difftime(current_time, SM[i].send_window.send_buff[idx].time);
                    if(time_gap >= T){
                        // Resend the message  
                        addr = SM[i].destination_addr;
                        sendto(SM[i].udp_socket_id, &(SM[i].send_window.send_buff[idx].message), sizeof(Message), 0, (struct sockaddr *)&addr, sizeof(addr));
                        num_transmissions++;
                        SM[i].send_window.send_buff[idx].time = time(NULL);
                        continue;
                    }
                }
            }
        }

        // Release the mutex
        if (sem_post(SM_mutex) == -1) {
            perror("sem_post");
            return NULL;
        }
    }

    return NULL;
}

//garbage collector process G to clean up the corresponding entry in the MTP socket if the corresponding process is killed
void* thread_G(){

    while(1){
        // Sleep for a period of 3*T seconds
        sleep(3*T);
        
        // Acquire the mutex to access shared resources safely
        if (sem_wait(SM_mutex) == -1) {
            perror("sem_wait");
            return NULL;
        }
        
        // Iterate through the MTP socket entries
        for(int i=0;i<MAX_MTP_SOCKETS;i++){
            // Check if the socket is allocated
            if(SM[i].socket_alloted <= 0 ) continue;
            
            // Get the process ID associated with the socket
            pid_t p=SM[i].process_id;
            // Check if the process is still running
            if(kill(p,0)==0) continue;
            // If the process is no longer running, close the socket and free the allocated resources
            if(errno==ESRCH){
                close(SM[i].udp_socket_id);
                SM[i].socket_alloted = 0;
                printf("\n***************************\n");
                printf("-> Socket %d closed by the garbage collector thread\n", i);
                printf("***************************\n");
            }
        }
        // Release the mutex
        if (sem_post(SM_mutex) == -1) {
            perror("sem_post");
            return NULL;
        }
    }
}



int main() {
     // Register exit handler
    if (atexit(cleanup_on_exit) != 0) {
        perror("atexit");
        return EXIT_FAILURE;
    }

    key_t key_SM = ftok("msocket.h", 'M'); // Generate a key for the shared memory segment

    // Create the shared memory segment for SM
    if ((shmid_SM = shmget(key_SM, MAX_MTP_SOCKETS * sizeof(MTPSocketEntry), IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Attach the shared memory segment for SM to the process's address space
    SM = (MTPSocketEntry *)shmat(shmid_SM, NULL, 0);
    if (SM == (MTPSocketEntry *)(-1)) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    key_t key_sockinfo = ftok("msocket.h", 'S');
    // Create the shared memory segment for sock_info
    if ((shmid_sock_info = shmget(key_sockinfo, sizeof(SOCK_INFO), IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Attach the shared memory segment for sock_info to the process's address space
    sock_info = (SOCK_INFO *)shmat(shmid_sock_info, NULL, 0);
    if (sock_info == (SOCK_INFO *)(-1)) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    // Create and initialize Sem1
    if ((Sem1 = sem_open("/Sem1", O_CREAT, SEM_PERMS, 0)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    // Create and initialize Sem2
    if ((Sem2 = sem_open("/Sem2", O_CREAT, SEM_PERMS, 0)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    // Create and initialize SM_mutex (for shared memory SM)
    if ((SM_mutex = sem_open("/SM_mutex", O_CREAT, SEM_PERMS, 1)) == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    // Initialize the shared memory entries for SM
    for (int i = 0; i < MAX_MTP_SOCKETS; i++) {
        SM[i].socket_alloted = 0; // Mark the socket as free
        SM[i].process_id = -1;     // Set process ID to -1 indicating no process has created the socket
        SM[i].udp_socket_id = -1;
        // Initialize other fields as needed
        memset(&(SM[i].destination_addr), 0, sizeof(struct sockaddr_in));
        memset(&(SM[i].send_window), 0, sizeof(swnd));
        memset(&(SM[i].recv_window), 0, sizeof(rwnd));
        SM[i].send_window.last_seq_no = 0;
        SM[i].send_window.window_size = 5;
        SM[i].send_window.window_start_index = 0;
        SM[i].recv_window.index_to_read = 0;
        SM[i].recv_window.next_seq_no = 1;
        SM[i].recv_window.window_size = 5;
        SM[i].recv_window.index_to_write = 0;
        SM[i].recv_window.nospace = 0;
        for(int j=0; j<SENDER_MSG_BUFFER; j++){
            SM[i].send_window.send_buff[j].ack_no = -1;
            SM[i].send_window.send_buff[j].sent = 0;
        }
        for(int j=0; j<RECEIVER_MSG_BUFFER; j++){
            SM[i].recv_window.recv_buff[j].ack_no = -1;
        }
        
    }
    // Clear the sock_info structure
    memset(sock_info, 0, sizeof(SOCK_INFO));
    // Initialization of the persistence timer for each socket (initially turned off)
    for(int i=0; i<MAX_MTP_SOCKETS; i++){
        persistence_timer[i].flag = 0;
    }

    // Register the signal handler for SIGINT
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }
    // Register the signal handler for SIGQUIT
    if (signal(SIGQUIT, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    printf("Press Ctrl+C to detach shared memory and quit.\n");
    //Thread to handle sending messages
    pthread_t thread_S_tid;
    if (pthread_create(&thread_S_tid, NULL, thread_S, NULL) != 0) {
        perror("pthread_create: Sender thread\n");
        exit(EXIT_FAILURE);
    }
    //Thread to handle receiving messages and sending ACKs
    pthread_t thread_R_tid;
    if (pthread_create(&thread_R_tid, NULL, thread_R, NULL) != 0) {
        perror("pthread_create: Receiver thread\n");
        exit(EXIT_FAILURE);
    }
    //Thread for cleaning up shared memory SM as garbage collector
    pthread_t thread_G_tid;
    if (pthread_create(&thread_G_tid, NULL, thread_G, NULL) != 0) {
        perror("pthread_create: Garbage collector\n");
        exit(EXIT_FAILURE);
    }

    
    printf("\n***************************\n");
    printf("initmsocket is initialized and running...\n");
    printf(" [Make sure to close all the user programs before closing this program] \n");
    printf("***************************\n");

    // Wait indefinitely to handle m_socket() and m_bind() calls
    while (1) {
        // Wait on Sem1
        if (sem_wait(Sem1) == -1) {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }

        // Process SOCK_INFO
        if (sock_info->sock_id == 0 && sock_info->IP == 0 && sock_info->port == 0) {
            // m_socket call
            sock_info->sock_id = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock_info->sock_id == -1) {
                sock_info->errno_val = errno;
            }
            // Signal Sem2
            if (sem_post(Sem2) == -1) {
                perror("sem_post");
                exit(EXIT_FAILURE);
            }
        } else if (sock_info->sock_id != 0 && sock_info->port != 0) {
            // m_bind call
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = sock_info->port;
            addr.sin_addr.s_addr = sock_info->IP;
            int ret = bind(sock_info->sock_id, (struct sockaddr *)&addr, sizeof(addr));
            if (ret == -1) {
                sock_info->errno_val = errno;
                sock_info->sock_id = -1; // Reset sock_id
            }
            // Signal Sem2
            if (sem_post(Sem2) == -1) {
                perror("sem_post");
                exit(EXIT_FAILURE);
            }
        } else if(sock_info->sock_id!=0 && sock_info->IP==0 && sock_info->port == 0){
            int status = close(sock_info->sock_id);
            if(status==-1){
                sock_info->sock_id = -1;
                sock_info->errno_val = errno;
            }
            // Signal Sem2
            if (sem_post(Sem2) == -1) {
                perror("sem_post");
                exit(EXIT_FAILURE);
            }
        }

    }

    return 0;
}
