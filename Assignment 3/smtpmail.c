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

#define MAX_LINE_LENGTH 100
#define MAX_LINES 50

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

//to send the 500 error response if the command is not recognized (possibly due to syntax errors)
void send_error(int sockfd){
    int reply_code = 500;
    char response[MAX_LINE_LENGTH];
    sprintf(response, "%d Command not recognized\r\n", reply_code);
    tcp_send(sockfd, response, strlen(response));
}

//extract the mail id from the MAIL and RCPT commands
int extract_email(char *arg, char *reverse_path_buff, int flag){
    char temp[10];
    int len = 0;
    if(flag==0){
        sprintf(temp, "FROM:<");
        len=6;
    }
    else{
        sprintf(temp, "TO:<");
        len=4;
    }
    // Check if the input string starts with ...
    //...'FROM:<' if MAIL command, or 'TO:<' if RCPT command
    //and ends with ">"
    if (strncmp(arg, temp, len) == 0 && arg[strlen(arg) - 1] == '>') {
        // Extract the email address between "<" and ">"
        int startIndex = len;
        int endIndex = strlen(arg) - 2;

        int length = endIndex - startIndex + 1;

        // Copy the email address to the destination buffer
        strncpy(reverse_path_buff, arg + startIndex, length);
        reverse_path_buff[length] = '\0';

        return 1;
    }

    return -1;
}

//receive the mail content over the TCP connection
//as per the protocol, the content will end with <CRLF>.<CRLF>
int get_mail(int sockfd, char buff[], ssize_t buff_size){
    char *end_of_reply;
    ssize_t bytes_recvd = 0;
    ssize_t recvd = 0;
    while(bytes_recvd<buff_size){
        recvd = recv(sockfd, (buff)+bytes_recvd, (buff_size)-bytes_recvd, 0);
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

        int temp = (int)bytes_recvd - (int)recvd - 5;
        if(temp<0)temp = 0;

        if((end_of_reply = strstr(buff+temp, "\r\n.\r\n")) != NULL){
            break;
        }
    }

    if(end_of_reply!=NULL){
        end_of_reply += 5;
        *end_of_reply = '\0';
        
        return 1;
    }

    return -1;
}

//get the username from the mail id
void get_username(char *username, char *mail_id){
    char *atSymbol = strchr(mail_id, '@');
    size_t usernameLength = atSymbol - mail_id;
    strncpy(username, mail_id, usernameLength);
    username[usernameLength] = '\0';
}

// Function to get the current time in the required format
void getCurrentTime(char *timeString) {
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);

    strftime(timeString, 20, "%Y-%m-%d : %H : %M", tm_info);
}

//write the received mail contents to the mailbox file
int write_mail_to_inbox(char *mail_id, char *mail_buff){
    char username[MAX_LINE_LENGTH-20];
    get_username(username, mail_id);
    char mailbox_path[MAX_LINE_LENGTH];
    sprintf(mailbox_path, "%s/mymailbox.txt", username);

    FILE *mailbox = fopen(mailbox_path, "a");
    if(mailbox == NULL){
        perror("Error opening the mail box file");
        return -1;
    }

    // Get current time
    char timeString[20];
    getCurrentTime(timeString);

    char *token = strtok(mail_buff, "\r\n");
    int i=0;
    while(token!=NULL){
        if(i==3){
            fprintf(mailbox, "Received: %s\n", timeString);
            i++;
            continue;
        }
        fprintf(mailbox, "%s\n", token);
        i++;
        token = strtok(NULL, "\r\n");
    }

    return 1;
}

//check if the mail id received in the RCPT command is available or not
int check_rcpt(char *mail_id){
    char username[MAX_LINE_LENGTH];
    get_username(username, mail_id);

    FILE *file = fopen("user.txt", "r");
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file) != NULL) {
        // Tokenize the line to get username and password
        char *token = strtok(line, " \t");

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

void handle_client(int sockfd){
    char reverse_path_buffer[MAX_LINE_LENGTH-30];
    char forward_path_buffer[MAX_LINE_LENGTH-30];
    char mail_data_buffer[MAX_LINES * MAX_LINE_LENGTH];

    const char *domain = "mymail.server.com";
    int reply_code = 0;
    int len = 0;
    char response_text[MAX_LINE_LENGTH];
    char command_recvd[MAX_LINE_LENGTH];
    char command[MAX_LINE_LENGTH];
    char temp[MAX_LINE_LENGTH];
    char *token = NULL;

    //send 220 response to client on accepting connection
    reply_code = 220;
    sprintf(response_text, "%d %s Server Ready\r\n", reply_code, domain);
    tcp_send(sockfd, response_text, strlen(response_text));

    //receive command: expecting 'HELO' command
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    char client_domain[MAX_LINE_LENGTH-30] = "";
    int i=0;
    while(token!=NULL){
        if(i==0)
            sprintf(command, "%s", token);
        else if(i==1)
            sprintf(client_domain, "%s", token);
        else
            break;
        i++;
        token = strtok(NULL, " ");
    }
    if(strcmp(command, "HELO")==0){ //send 250 response on HELO
        reply_code = 250;
        sprintf(response_text, "%d OK Hello %s\r\n", reply_code, client_domain);
        tcp_send(sockfd, response_text, strlen(response_text));
    }else{
        send_error(sockfd);
        return;
    }

    //receive command: expecting 'MAIL' command
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    i=0;
    while(token!=NULL){
        if(i==0){
            sprintf(command, "%s", token);
        }
        else if(i==1){
            sprintf(temp, "%s", token);
        }
        else{
            break;
        }
        i++;
        token = strtok(NULL, " ");
    }
    if(strcmp(command, "MAIL")==0){
        //clear all the buffers
        memset(reverse_path_buffer,'\0',sizeof(reverse_path_buffer));
        memset(forward_path_buffer,'\0',sizeof(forward_path_buffer));
        memset(mail_data_buffer,'\0',sizeof(mail_data_buffer));

        int err = extract_email(temp, reverse_path_buffer, 0); 
        if(err==-1){
            send_error(sockfd);
            return;
        }
        //send 250 response on MAIL
        reply_code = 250;
        sprintf(response_text, "%d OK ...<%s> Sender ok\r\n", reply_code, reverse_path_buffer);
        tcp_send(sockfd, response_text, strlen(response_text));
    }else{
        send_error(sockfd);
        return;
    }

    //receive command: expecting 'RCPT' command
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    i=0;
    while(token!=NULL){
        if(i==0){
            sprintf(command, "%s", token);
        }
        else if(i==1){
            sprintf(temp, "%s", token);
        }
        else{
            break;
        }
        i++;
        token = strtok(NULL, " ");
    }
    if(strcmp(command, "RCPT")==0){
        int err = extract_email(temp, forward_path_buffer, 1); 
        if(err==-1){
            send_error(sockfd);
            return;
        }
        int check = check_rcpt(forward_path_buffer);
        if(check==0){ //send 550 response on RCPT, if the user is not found
            reply_code = 550;
            sprintf(response_text, "%d No such user\r\n", reply_code);
            tcp_send(sockfd, response_text, strlen(response_text));
            return;
        }
        //send 250 response on RCPT
        reply_code = 250;
        sprintf(response_text, "%d OK ...<%s> Recipient ok\r\n", reply_code, forward_path_buffer);
        tcp_send(sockfd, response_text, strlen(response_text));
    }else{
        send_error(sockfd);
        return;
    }

    //receive command: expecting 'DATA' command
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    sprintf(command, "%s", token);
    if(strcmp(command, "DATA")==0){ //send 354 response on DATA command
        reply_code = 354;
        sprintf(response_text, "%d Enter mail, end with \".\" on a line by itself\r\n", reply_code);
        tcp_send(sockfd, response_text, strlen(response_text));
    }else{
        send_error(sockfd);
        return;
    }

    //receive mail contents from client
    int err = get_mail(sockfd, mail_data_buffer, sizeof(mail_data_buffer));
    if(err == -1){
        //send 554 response if the mail is not received in the required format
        reply_code = 554;
        sprintf(response_text, "%d Mail content not accepted for delivery\r\n", reply_code);
        tcp_send(sockfd, response_text, strlen(response_text));
        return;
    }
    //send 250 response on receiving the mail contents
    reply_code = 250;
    sprintf(response_text, "%d OK Message accepted for delivery\r\n", reply_code);
    tcp_send(sockfd, response_text, strlen(response_text));

    //write the received mail contents to the mailbox
    err = write_mail_to_inbox(forward_path_buffer, mail_data_buffer);
    if(err==-1){
        printf("Error in writing mail to the mailbox.\n");
    }

    //receive command: expecting 'QUIT' command
    memset(command_recvd,'\0',sizeof(command_recvd));
    len = tcp_recv(sockfd, command_recvd, sizeof(command_recvd));
    command_recvd[len]='\0';
    token = strtok(command_recvd, " ");
    sprintf(command, "%s", token);
    if(strcmp(command,"QUIT")==0){
        //send 221 response on QUIT command
        reply_code = 221;
        sprintf(response_text, "%d Closing connection\r\n", reply_code);
        tcp_send(sockfd, response_text, strlen(response_text));
    }else{
        send_error(sockfd);
        return;
    }
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

    int my_port;
    sscanf(argv[1], "%d", &my_port); //port number for the smtp server

    //setting up server address and port
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(my_port);

    //binding the socket
    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("Socket binding failed");
        exit(EXIT_FAILURE);
    }

    printf("SMTP server running...\n");

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