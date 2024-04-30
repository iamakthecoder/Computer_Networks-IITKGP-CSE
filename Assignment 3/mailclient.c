#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAX_LINE_LENGTH 100
#define MAX_LINES 50
#define MAX_MAILS 50

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

//to receive multi-line response from POP3 server
//as per the protocol, the content will end with <CRLF>.<CRLF>
int tcp_recv_ml(int sockfd, char *buff, ssize_t buff_size){
    char *end_of_reply;
    ssize_t bytes_recvd = 0;
    ssize_t recvd = 0;
    int positive=0;
    char temp[10];
    sprintf(temp, "\r\n");
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
        if(positive==0){
            char check[10];
            sscanf(buff, "%s", check);
            if(strcmp(check,"+OK")==0){
                positive = 1;
                sprintf(temp, "\r\n.\r\n");
            }
        }

        int temp1 = (int)bytes_recvd - (int)recvd - 5;
        if(temp1<0)temp1 = 0;

        if((end_of_reply = strstr(buff+temp1, temp)) != NULL){
            break;
        }
    }

    if(end_of_reply!=NULL){
        *end_of_reply = '\0';
        
        return 1;
    }

    return -1;
}

//checks if the email is in valid format
int isValidEmailFormat(const char *email) {
    int atCount = 0;
    int index = -1;
    for (int i = 0; email[i] != '\0'; i++) {
        if (email[i] == '@') {
            index = i;
            atCount++;
        }
    }
    return (atCount == 1) && (index>0) && (index<strlen(email)-1);
}

//sends the mail content received from the user using SMTP protocol
int send_mail(int sockfd, char *from, char *to, char *subject, char message[MAX_LINES][MAX_LINE_LENGTH], int lineCount){
    int len = 0;
    const char *domain = "mymail.client.com";
    char command_text[MAX_LINE_LENGTH];
    char response_text[MAX_LINE_LENGTH];
    int response_code=-1;

    //sends the HELO command
    sprintf(command_text, "HELO %s\r\n", domain);
    tcp_send(sockfd, command_text, strlen(command_text));
    //response to the HELO command: expecting a 250 response from server
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=250){
        printf("Received a non-250 reply code on HELO command: %s\n", response_text);
        return -1;
    }

    //sends the MAIL command
    sprintf(command_text, "MAIL FROM:<%s>\r\n", from);
    tcp_send(sockfd, command_text, strlen(command_text));
    //response to the MAIL command: expecting a 250 response from server
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=250){
        printf("Received a non-250 reply code on MAIL command: %s\n", response_text);
        return -1;
    }

    //sends the RCPT command
    sprintf(command_text, "RCPT TO:<%s>\r\n", to);
    tcp_send(sockfd, command_text, strlen(command_text));
    //response to the RCPT command: expecting a 250 response from server
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=250){
        printf("Received a non-250 reply code on RCPT command: %s\n", response_text);
        return -1;
    }

    //sends the DATA command
    sprintf(command_text,"DATA\r\n");
    tcp_send(sockfd, command_text, strlen(command_text));
    //response to the DATA command: expecting a 354 response from the server
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=354){
        printf("Received a non-354 reply code on DATA command: %s\n", response_text);
        return -1;
    }

    //sends the mail content
    char mail_data_buff[MAX_LINE_LENGTH];
    for(int lineno=0; lineno<=lineCount; lineno++){
        sprintf(mail_data_buff, "%s\r\n", message[lineno]);
        tcp_send(sockfd, mail_data_buff, strlen(mail_data_buff));
    }
    //expecting a 250 response from the server after sending the mail contents
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=250){
        printf("Received a non-250 reply code on sending the mail contents: %s\n", response_text);
        return -1;
    }

    //sends the QUIT command
    sprintf(command_text, "QUIT\r\n");
    tcp_send(sockfd, command_text, strlen(command_text));
    //response to the QUIT command: expecting a 221 response from the server
    response_text[0]='\0';
    len = tcp_recv(sockfd, response_text, sizeof(response_text));
    response_text[len]='\0';
    if (sscanf(response_text, "%3d", &response_code) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        return -1;
    }
    if(response_code!=221){
        printf("Received a non-221 reply code on QUIT command: %s\n", response_text);
        return -1;
    }

    return 0;
}
int check_cnt(char *buff, int flag){
    char line[MAX_LINE_LENGTH];
    strcpy(line, buff);
    int temp = 0;
    if(flag==0){
        temp = strlen("From:");
    }
    if(flag==1){
        temp = strlen("To:");
    }

    char *token = strtok(line+temp, " ");
    int i=0;
    while(token!=NULL){
        i++;
        token = strtok(NULL, " ");
    }

    if(i==1)return 1;
    return 0;
}
//interface for sending mails
int send_mail_func(char *server_IP, int smtp_port){
    int sockfd;

    //socket creation
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    inet_aton(server_IP, &servaddr.sin_addr);
    servaddr.sin_port = htons(smtp_port);

    //make connection to the server
    if((connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0){
        perror("Unable to connect to the server");
        return -1;
    }

    int reply_code_recvd=0;
    char reply_text_recvd[MAX_LINE_LENGTH];
    //expecting a 220 response from the server on making the connection
    reply_text_recvd[0]='\0';
    int len = tcp_recv(sockfd, reply_text_recvd, sizeof(reply_text_recvd));
    reply_text_recvd[len]='\0';
    if (sscanf(reply_text_recvd, "%3d", &reply_code_recvd) != 1) {
        fprintf(stderr, "Error parsing reply code\n");
        close(sockfd);
        return -1;
    }
    if(reply_code_recvd!=220){
        printf("Received a non-220 reply code on making connnection to the server: %s\n", reply_text_recvd);
        close(sockfd);
        return -1;
    }

    char from[MAX_LINE_LENGTH], to[MAX_LINE_LENGTH], subject[MAX_LINE_LENGTH], message[MAX_LINES][MAX_LINE_LENGTH];
    int lineCount = 0;
    printf("Enter the mail:\n");
    //inputs and format checking...
    // Reading From
    fflush(stdin);
    scanf("%[^\n]s", message[lineCount]);
    if (sscanf(message[lineCount], "From: %s", from) != 1 || !isValidEmailFormat(from) || !check_cnt(message[lineCount], 0)) {
        printf("Incorrect format\n");
        close(sockfd);
        return -1;
    }else lineCount++;
    

    // Reading To
    fflush(stdin);
    scanf("%[^\n]s", message[lineCount]);
    if (sscanf(message[lineCount], "To: %s", to) != 1 || !isValidEmailFormat(to) || !check_cnt(message[lineCount], 1)) {
        printf("Incorrect format\n");
        close(sockfd);
        return -1;
    }else lineCount++;

    // Reading Subject
    fflush(stdin);
    scanf("%[^\n]s", message[lineCount]);
    if (sscanf(message[lineCount], "Subject: %[^\n]", subject) != 1 || strlen(subject)==0) {
        printf("Incorrect format\n");
        close(sockfd);
        return -1;
    }else lineCount++;

    // Reading Message Body
    int flag = 0;
    while (1) {
        fflush(stdin);
        if (scanf("%[^\n]s", message[lineCount]) != 1) {
            printf("Incorrect format\n");
            flag = 1;
            break;
        }

        if (strcmp(message[lineCount], ".") == 0) {
            break;
        }

        lineCount++;
    }
    if(flag){
        close(sockfd);
        return -1;
    }

    //send the mail content received using the SMTP protocol
    int err = send_mail(sockfd, from,to,subject,message, lineCount);

    close(sockfd);
    if(err==-1){
        return -1;
    }
    printf("Mail sent successfully.\n");

    return 0;
 
}

//to send the commands for authentication to POP3 server
int authenticate(int sockfd, char *username, char *password){
    char command[MAX_LINE_LENGTH];
    char response[MAX_LINE_LENGTH];
    char *token = NULL;

    //username
    sprintf(command, "USER %s\r\n", username);
    tcp_send(sockfd, command, strlen(command));
    memset(response, '\0', sizeof(response));
    int len = tcp_recv(sockfd, response, sizeof(response));
    response[len]='\0';
    token = strtok(response, " ");
    if(strcmp(token, "+OK")!=0){
        printf("NOT OK response received after USER\n");
        return 0;
    }

    //password
    sprintf(command, "PASS %s\r\n", password);
    tcp_send(sockfd, command, strlen(command));
    memset(response, '\0', sizeof(response));
    len = tcp_recv(sockfd, response, sizeof(response));
    response[len]='\0';
    token = strtok(response, " ");
    if(strcmp(token, "+OK")!=0){
        printf("NOT OK response received after PASS\n");
        return 0;
    }

    return 1;
}

//to make and display the table, showing the inbox of the user
typedef struct TableRow {
    int serial_no;
    char sender_email[MAX_LINE_LENGTH];
    char received[MAX_LINE_LENGTH];
    char subject[MAX_LINE_LENGTH];
} TableRow;
int insert_row(TableRow mail_rows[], int idx, int serial_no, char *response){
    char temp[MAX_LINE_LENGTH];

    char *token = strtok(response, "\r\n");
    sscanf(token,"%s",temp);
    if(strcmp(temp, "+OK")!=0){
        printf("NOT OK response received on RETR %d\n", serial_no);
        return 0;
    }

    int got_sender=0, got_time=0, got_sub=0;
    token = strtok(NULL,"\r\n");
    while(token!=NULL){

        if (strncmp(token, "From:", strlen("From:")) == 0){
            sscanf(token + strlen("From:"), "%s", mail_rows[idx].sender_email);
            got_sender=1;
        }
        else if(strncmp(token, "Received:", strlen("Received:")) == 0){
            sscanf(token + strlen("Received:"), "%[^\n]s", mail_rows[idx].received);
            got_time=1;
        }
        else if(strncmp(token, "Subject:", strlen("Subject:")) == 0){
            sscanf(token + strlen("Subject:"), "%[^\n]s", mail_rows[idx].subject);
            got_sub=1;
        }
        if(got_sender+got_time+got_sub==3)break;
        token = strtok(NULL,"\r\n");
    }

    if(got_sender+got_time+got_sub!=3){
        return 0;
    }

    mail_rows[idx].serial_no = serial_no;
    return 1;
}
//displays the inbox table
int display_inbox(int sockfd){

    char temp[MAX_LINE_LENGTH];
    char command[MAX_LINE_LENGTH];
    char response[MAX_LINE_LENGTH*MAX_LINES];

    sprintf(command, "LIST\r\n");
    tcp_send(sockfd, command, strlen(command));
    memset(response, '\0', sizeof(response));
    tcp_recv_ml(sockfd, response, sizeof(response));

    char *token = strtok(response, "\r\n");
    sscanf(token, "%s", temp);
    if(strcmp(temp, "+OK")!=0){
        printf("NOT OK response received after LIST\n");
        return 0;
    }

    int mail_ids[MAX_MAILS];
    int num=0;
    token = strtok(NULL, "\r\n");
    while(token!=NULL){
        int mail_no=-1;
        sscanf(token,"%d",&mail_no);
        mail_ids[num++] = mail_no;
        token = strtok(NULL, "\r\n");
    }

    TableRow mail_row[num];

    for(int i=0; i<num; i++){
        sprintf(command, "RETR %d\r\n", mail_ids[i]);
        tcp_send(sockfd, command, strlen(command));
        memset(response,'\0',sizeof(response));
        tcp_recv_ml(sockfd, response, sizeof(response));
        int err = insert_row(mail_row, i, mail_ids[i], response);
        if(err==0){
            return 0;
        }
    }

    // Print table header
    printf("+--------+----------------------+---------------------------+--------------------------------+\n");
    printf("| Sl. No.|    Sender's Email    |       When Received       |             Subject            |\n");
    printf("+--------+----------------------+---------------------------+--------------------------------+\n");

    // Print table rows
    for (int i = 0; i < num; i++) {
        printf("| %-6d | %-20s | %-25s | %-30s |\n", 
               mail_row[i].serial_no, mail_row[i].sender_email, mail_row[i].received, mail_row[i].subject);
    }

    // Print table footer
    printf("+--------+----------------------+---------------------------+--------------------------------+\n");


    return 1;
}

//to get and display an email contents
int display_email(int sockfd, int mail_no){
    char command[MAX_LINE_LENGTH];
    char response[MAX_LINE_LENGTH*MAX_LINES];
    char temp[MAX_LINE_LENGTH];

    sprintf(command,"RETR %d\r\n", mail_no);

    tcp_send(sockfd, command, strlen(command));

    memset(response,'\0',sizeof(memset));

    tcp_recv_ml(sockfd, response, sizeof(response));

    char *token = strtok(response,"\r\n");

    sscanf(token,"%s",temp);
    if(strcmp(temp,"+OK")!=0){
        printf("NOT OK response received on RETR %d\n", mail_no);
        return 0;
    }

    token = strtok(NULL,"\r\n");
    while(token!=NULL){
        if(strcmp(token,".")==0){
            break;
        }
        if(strncmp(token,".",1)==0){
            sscanf(token+1,"%[^\n]s",temp);
        }
        else{
            sscanf(token,"%[^\n]s",temp);
        }
        printf("%s\n",temp);
        token = strtok(NULL,"\r\n");
    }

    return 1;
}

//interface for POP3 server communication
void manage_mail(char *server_IP, int pop3_port, char *username, char *pswd){
    int sockfd;

    //socket creation
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket creation failed");
        return;
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    inet_aton(server_IP, &servaddr.sin_addr);
    servaddr.sin_port = htons(pop3_port);

    //make connection to the server
    if((connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0){
        perror("Unable to connect to the server");
        return;
    }

    char *token=NULL;
    char response[MAX_LINE_LENGTH];
    char command[MAX_LINE_LENGTH];
    memset(response, '\0', sizeof(response));
    int len = tcp_recv(sockfd, response, sizeof(response));
    response[len]='\0';
    token = strtok(response, " ");
    if(strcmp(token, "+OK")!=0){
        printf("NOT OK response received after connection\n");
        close(sockfd);
        return;
    }

    int auth = authenticate(sockfd, username, pswd);
    if(auth==0){
        printf("Authentication failed.\n");
        close(sockfd);
        return;
    }

    //TRANSACTION state now...
    while(1){
        int err = display_inbox(sockfd);
        if(err==0){
            printf("Error displaying the inbox.\n");
            break;
        }

        int mail_no;
        int check = 0;
        int quit = 0;
        while(check==0){
            printf("Enter mail no. to see: ");
            fflush(stdin);
            scanf("%d", &mail_no);
            fflush(stdin);
            
            if(mail_no==-1){
                quit = 1;
                break;
            }
            check = display_email(sockfd, mail_no); 
            if(check==0){
                printf("Could not display the mail body for the given mail no., try again...\n");
            }
        }

        if(quit)break;
        fflush(stdin);
        char c = getchar(); //check if the mail has to be deleted or not
        fflush(stdin);
        if(c=='d'){
            //to delete a mail
            sprintf(command, "DELE %d\r\n", mail_no);
            tcp_send(sockfd, command, strlen(command));

            memset(response, '\0', sizeof(response));
            len = tcp_recv(sockfd, response, sizeof(response));
            response[len] = '\0';
            token = strtok(response, " ");
            if(strcmp(token, "+OK")!=0){
                printf("NOT OK response received after DELE %d\n", mail_no);
            }
        }
    }
    
    
    
    //transition to UPDATE state now...
    sprintf(command, "QUIT\r\n");
    tcp_send(sockfd, command, strlen(command));
    
    memset(response, '\0', sizeof(response));
    len = tcp_recv(sockfd, response, sizeof(response));
    response[len] = '\0';
    token = strtok(response, " ");
    if(strcmp(token, "+OK")!=0){
        printf("NOT OK response received after QUIT\n");
        close(sockfd);
        return;
    }
     
    close(sockfd);
}

int main(int argc, char *argv[]){
    if(argc<4){
        printf("Not enough arguments...\n");
        exit(0);
    }

    char server_IP[MAX_LINE_LENGTH];
    sprintf(server_IP, "%s", argv[1]);
    int smtp_port, pop3_port;
    sscanf(argv[2], "%d", &smtp_port);
    sscanf(argv[3], "%d", &pop3_port);

    char username[MAX_LINE_LENGTH],password[MAX_LINE_LENGTH];
    printf("Username: ");
    scanf("%s", username);
    printf("Password: ");
    scanf("%s", password);

    while(1){
        printf("1. Manage Mail : Shows the stored mails of the logged in user only\n");
        printf("2. Send Mail : Allows the user to send a mail\n");
        printf("3. Quit : Quits the program\n");
        printf("Enter input: ");
        int input;
        scanf("%d", &input);
        if(input==1){
            manage_mail(server_IP, pop3_port, username, password);
        }
        else if(input==2){
            int err = send_mail_func(server_IP, smtp_port);
            if(err==-1){
                printf("Error in sending mail...\n");
                continue;
            }
        }
        else if(input==3){
            break;
        }
        else{
            printf("Invalid input! Enter again.\n");
            continue;
        }
    }

    return 0;
}