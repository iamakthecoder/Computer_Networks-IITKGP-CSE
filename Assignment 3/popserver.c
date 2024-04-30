#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>

#define AUTHORIZATION 1
#define TRANSACTION 2
#define UPDATE 3

#define MAX_LINE_LENGTH 100
#define MAX_LINES 50
#define MAX_MAILS 50

typedef struct Mail{
    int serial_no;
    int to_delete;
    int size;
    char mail_body[MAX_LINE_LENGTH*MAX_LINES];
}mail;

//sends from the 'send_buffer' using the TCP connection
void tcp_send(int sockfd, char *send_buffer, ssize_t send_size){
    ssize_t bytes_sent = 0;
    while(bytes_sent < send_size){
        ssize_t sent = send(sockfd, (send_buffer)+bytes_sent, (send_size)-bytes_sent, 0);
        if(sent==-1){
            perror("Error sending message");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        else if(sent==0){
            perror("Connection closed while sending the message");
            close(sockfd);
            exit(0);
        }
        else{
            bytes_sent += sent;
        }
    }
}

//receives in the 'recv_buffer' using the TCP connection
int tcp_recv(int sockfd, char *recv_buff, ssize_t recv_size){
    char *end_of_reply;
    ssize_t bytes_recvd=0;
    // Keep receiving data until "\r\n" is encountered
    while ((end_of_reply = strstr(recv_buff, "\r\n")) == NULL) {
        ssize_t recvd = recv(sockfd, (recv_buff)+bytes_recvd, (recv_size)-bytes_recvd, 0);
        if (recvd == -1) {
            perror("Error receiving message");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        else if(recvd==0){
            perror("Connection closed while receiving the message");
            close(sockfd);
            exit(0);
        }
        else{
            bytes_recvd += recvd;
        }

    }
    return end_of_reply-recv_buff;
}

//authenticates username
int check_username(char *username){

    FILE *file = fopen("user.txt", "r");
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file) != NULL) {
        // Tokenize the line to get username and password
        char *token = strtok(line, " \t\n");

        if (token != NULL) {
            // Compare the given username with the one from the file
            if (strcmp(username, token) == 0) {
                fclose(file);
                return 1;
            }
        }
    }
    fclose(file);
    return 0;
}

//authenticates password
int check_password(char *username, char *password){
    FILE *file = fopen("user.txt", "r");
    char line[MAX_LINE_LENGTH];
    int i=0;
    while (fgets(line, sizeof(line), file) != NULL) {
        // Tokenize the line to get username and password
        char *token = strtok(line, " \t\n");
        while(token!=NULL){
            if(i==0){
                if(strcmp(username, token)==0){
                    i++;
                    token = strtok(NULL, " \t\n");
                    if(token==NULL){
                        fclose(file);return 0;
                    }
                }
                else{
                    break;
                }
            }
            if(i==1){
                if(strcmp(password, token)==0){
                    fclose(file);
                    return 1;
                }
                fclose(file);
                return 0;
            }
        }
    }
    fclose(file);
    return 0;
}

//cache the mailbox when enter in TRANSACTION state
int get_inbox(mail inbox[], FILE *mailbox){
    char line[MAX_LINE_LENGTH];
    char buffer[MAX_LINE_LENGTH*MAX_LINES];
    int id = -1;
    int inMail = 0;
    while(fgets(line, sizeof(line), mailbox)){
        if(inMail){
            strcat(buffer, line);
            if((strcmp(line, ".\n")==0) || (strcmp(line, ".\r\n")==0)){
                inbox[id].serial_no = id+1;
                inbox[id].to_delete = 0;
                strcpy(inbox[id].mail_body, buffer);
                inbox[id].size = strlen(inbox[id].mail_body);
                inMail = 0;
                buffer[0]='\0';
            }
        }
        else{
            if (strncmp(line, "From:", 5) == 0){
                inMail = 1;
                id++;
                strcat(buffer, line);
            }
        }
    }
    return id+1;
}

//response of STAT command
void get_stat(mail inbox[], int num_mails, char *response){
    int num = 0;
    int sz = 0;
    for(int i=0; i<num_mails; i++){
        if(inbox[i].to_delete)continue;

        num++;
        sz+=inbox[i].size;
    }
    sprintf(response, "+OK %d %d\r\n", num, sz);
}

//response of LIST command
void send_list_all(int sockfd, mail inbox[], int num_mails, char *response){
    int num = 0;
    int sz = 0;
    for(int i=0; i<num_mails; i++){
        if(inbox[i].to_delete)continue;
        num++;
        sz+=inbox[i].size;
    }
    sprintf(response, "+OK %d messages (%d octets)\r\n", num, sz);
    tcp_send(sockfd, response, strlen(response));
    for(int i=0; i<num_mails; i++){
        if(inbox[i].to_delete)continue;
        sprintf(response, "%d %d\r\n", inbox[i].serial_no, inbox[i].size);
        tcp_send(sockfd, response, strlen(response));
    }
    sprintf(response, ".\r\n");
    tcp_send(sockfd, response, strlen(response));
}

//response of LIST [arg] command
void get_list(mail inbox[], int num_mails, char *response, int mail_no){
    if(mail_no>num_mails || mail_no<=0){
        sprintf(response, "-ERR no such message\r\n");
        return;
    }
    if(inbox[mail_no-1].to_delete){
        sprintf(response, "-ERR no such message\r\n");
        return;
    }
    sprintf(response, "+OK %d %d\r\n", inbox[mail_no-1].serial_no, inbox[mail_no-1].size);
}

//response of RETR [arg] command
void send_retr(int sockfd, mail inbox[], int num_mails, int mail_no){
    char response[MAX_LINE_LENGTH+10];
    if(mail_no>num_mails || mail_no<=0){
        sprintf(response, "-ERR no such message\r\n");
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    if(inbox[mail_no-1].to_delete){
        sprintf(response, "-ERR no such message\r\n");
        tcp_send(sockfd, response, strlen(response));
        return;
    }

    sprintf(response, "+OK %d octets\r\n", inbox[mail_no-1].size);
    tcp_send(sockfd, response, strlen(response));

    char buff[MAX_LINE_LENGTH*MAX_LINES];
    strcpy(buff,inbox[mail_no-1].mail_body);

    char *token = strtok(buff, "\r\n");
    while(token!=NULL){
        if((strcmp(token, ".")!=0) && (strncmp(token, ".", 1)==0)){
            sprintf(response,".%s\r\n",token);
            tcp_send(sockfd, response, strlen(response));
        }
        else{
            sprintf(response,"%s\r\n", token);
            tcp_send(sockfd, response, strlen(response));
        }
        token=strtok(NULL,"\r\n");
    }
}

//response of DELE [arg] command
void mail_delete(mail inbox[], int num_mails, int mail_no, char *response){
    if(mail_no>num_mails || mail_no<=0){
        sprintf(response, "-ERR no such message\r\n");
        return;
    }
    if(inbox[mail_no-1].to_delete){
        sprintf(response, "-ERR no such message, already deleted\r\n");
        return;
    }

    inbox[mail_no-1].to_delete = 1;
    sprintf(response, "+OK message %d deleted\r\n", mail_no);
}

//response of RSET command
void rset(mail inbox[], int num_mails, char *response){
    int num=0;
    int sz=0;
    for(int i=0; i<num_mails; i++){
        inbox[i].to_delete = 0;
        num++;
        sz+=inbox[i].size;
    }
    sprintf(response,"+OK inbox has %d messages (%d octets)\r\n", num, sz);
}

//to handle each client...
void handle_client(int sockfd){
    int state=-1;
    char response[MAX_LINE_LENGTH];
    char command_recvd[MAX_LINE_LENGTH];
    int len;
    char *token = NULL;\
    char temp[MAX_LINE_LENGTH];
    char command[MAX_LINE_LENGTH];
    char username[MAX_LINE_LENGTH]; memset(username, '\0', sizeof(username));
    int err = 0;

    state = AUTHORIZATION;
    //AUTHORIZATION state...
    sprintf(response, "+OK POP3 server ready\r\n");
    tcp_send(sockfd, response, strlen(response));

    //receive username
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    int i=0;
    sprintf(command,"USER");
    while(token!=NULL){
        if(i==0){
            sprintf(temp,"%s", token);
            if(strcmp(temp,command)!=0){
                err = 1;
                break;
            }
        }
        else if(i==1){
            sprintf(username,"%s", token);
            break;
        }
        else break;
        i++;
        token = strtok(NULL, " ");
    }
    if(err){
        sprintf(response, "-ERR unexpected command (expected:%s, received:%s)\r\n", command, temp);
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    int check = check_username(username);
    if(check==0){
        sprintf(response, "-ERR username:<%s> not valid\r\n", username);
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    sprintf(response, "+OK valid username\r\n");
    tcp_send(sockfd, response, strlen(response));

    //receive password
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    i=0;
    sprintf(command,"PASS");
    while(token!=NULL){
        if(i==0){
            sprintf(temp,"%s", token);
            if(strcmp(temp,command)!=0){
                err = 1;
                break;
            }
        }
        else if(i==1){
            sprintf(temp,"%s", token);
            break;
        }
        else break;
        i++;
        token = strtok(NULL, " ");
        memset(temp,'\0',sizeof(temp));
    }
    if(err){
        sprintf(response, "-ERR unexpected command (expected:%s, received:%s)\r\n", command, temp);
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    check = check_password(username,temp);
    if(check==0){
        sprintf(response, "-ERR password not valid\r\n");
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    //open mailbox
    char mailbox_path[MAX_LINE_LENGTH];
    sprintf(mailbox_path,"%s/mymailbox.txt",username);
    FILE *mailbox = fopen(mailbox_path,"r+");
    if(mailbox==NULL){
        sprintf(response, "-ERR mailbox could not be opened\r\n");
        tcp_send(sockfd, response, strlen(response));
        return;
    }
    sprintf(response, "+OK mailbox opened\r\n");
    tcp_send(sockfd, response, strlen(response));

    state = TRANSACTION; 
    //TRANSACTION state now...

    mail inbox[MAX_MAILS];
    int num_mails = get_inbox(inbox, mailbox);
    
    while(state == TRANSACTION){
        memset(command_recvd,'\0',sizeof(command_recvd));
        len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
        command_recvd[len]='\0';


        token = strtok(command_recvd, " ");

        //STAT command received
        if(strcmp(token, "STAT") == 0){
            get_stat(inbox, num_mails, response);
            tcp_send(sockfd, response, strlen(response));
            continue;
        }

        //LIST command received
        if(strcmp(token, "LIST") == 0){
            token = strtok(NULL, " ");
            if(token==NULL){
                send_list_all(sockfd, inbox, num_mails, response);
            }
            else{
                int mail_no=-1;
                sscanf(token, "%d", &mail_no);
                get_list(inbox, num_mails, response, mail_no);
                tcp_send(sockfd, response, strlen(response));
            }
            continue;
        }

        //RETR command received
        if(strcmp(token, "RETR")==0){
            token = strtok(NULL, " ");
            if(token==NULL){
                sprintf(response, "-ERR invalid format, argument required for RETR\r\n");
                tcp_send(sockfd, response, strlen(response));
            }
            else{
                int mail_no=-1;
                sscanf(token, "%d", &mail_no);
                send_retr(sockfd, inbox, num_mails, mail_no);
            }

            continue;
        }

        //DELE command received
        if(strcmp(token, "DELE")==0){
            token = strtok(NULL, " ");
            if(token==NULL){
                sprintf(response, "-ERR invalid format, argument required for DELE\r\n");
                tcp_send(sockfd, response, strlen(response));
            }
            else{
                int mail_no = -1;
                sscanf(token, "%d", &mail_no);
                mail_delete(inbox, num_mails, mail_no, response);
                tcp_send(sockfd, response, strlen(response));
            }
            continue;
        }

        //RSET command received
        if(strcmp(token, "RSET")==0){
            rset(inbox, num_mails, response);
            tcp_send(sockfd, response, strlen(response));
            continue;
        }

        //QUIT command received
        if(strcmp(token, "QUIT")==0){
            state=UPDATE;
            continue;
        }

        //invalid command received...
        sprintf(response, "-ERR command %s not recognized in TRANSACTION state\r\n", token);
        tcp_send(sockfd, response, strlen(response));
    }
    
    //UPDATE state...
    if(fseek(mailbox, 0, SEEK_SET) != 0) {
        sprintf(response, "-ERR could not make the desired changes to the mailbox\r\n");
        tcp_send(sockfd, response, strlen(response));
        perror("Error seeking to the start of the mailbox");
        fclose(mailbox);
        return;
    }
    //clear the mailbox (to be written again)...
    if (ftruncate(fileno(mailbox), 0) != 0) {
        sprintf(response, "-ERR could not make the desired changes to the mailbox\r\n");
        tcp_send(sockfd, response, strlen(response));
        perror("Error truncating file");
        fclose(mailbox);
        return;
    }
    //write back the mailbox (except the deleted mails)...
    for(int i=0; i<num_mails; i++){
        if(inbox[i].to_delete)continue;

        fprintf(mailbox, inbox[i].mail_body);
    }

    sprintf(response, "+OK POP3 server signing off\r\n"); //signing off
    tcp_send(sockfd, response, strlen(response));
    
    fclose(mailbox);
}

int main(int argc, char *argv[]){

     if(argc==1){
        printf("Give the port number as the command line argument.\n");
        exit(0);
    }

    int sockfd, newsockfd;
    //socket creation
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int pop3_port;
    sscanf(argv[1], "%d", &pop3_port); //port number for the smtp server

    //setting up server address and port
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(pop3_port);

    //binding the socket
    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("Socket binding failed");
        exit(EXIT_FAILURE);
    }

    printf("POP3 server running...\n");

    listen(sockfd, 5);

    struct sockaddr_in cliaddr;
    socklen_t client_len;
    while(1){

        client_len = sizeof(cliaddr);
        //accept the connection from a client
        newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &client_len);

        if(newsockfd<0){
            perror("Connection-Accept error");
            exit(0);
        }

        if(fork()==0){
            close(sockfd);

            handle_client(newsockfd);

            close(newsockfd);
            exit(0);
        }

        close(newsockfd);
    }

    return 0;
}