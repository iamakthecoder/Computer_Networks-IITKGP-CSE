#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAX_LEN 300

typedef struct Info{
    char name[20];
    char IP[20];
    int port;
}info;

void set_values(info user_info[]){
    for(int i=0; i<3; i++){
        sprintf(user_info[i].name, "user_%d", i+1);
        sprintf(user_info[i].IP, "127.0.0.1");
        user_info[i].port = 50000+i;
    }
}

int get_client(char *name, char *IP, int *port, info user_info[]){
    for(int i=0; i<3; i++){
        if(strcmp(user_info[i].name,name)==0){
            sprintf(IP, user_info[i].IP);
            *port = user_info[i].port;
            return 1;
        }
    }
    return 0;
}

int main(int argc, char* argv[]){
    if(argc==1){
        printf("Give the port number as the command line argument.\n");
        exit(0);
    }

    info user_info[3];
    set_values(user_info);

    int my_port;
    sscanf(argv[1], "%d", &my_port);

    if(my_port!=50000 && my_port!=50001 && my_port!=50002){
        printf("Enter a valid port number (one of 50000, 50001 or 50002)\n");
        exit(0);
    }

    char my_name[20];
    for(int i=0; i<3; i++){
        if(user_info[i].port==my_port){
            sprintf(my_name, user_info[i].name);
        }
    }

    int sockfd;
    //socket creation
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

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

    listen(sockfd, 3);

    fd_set readset;
    struct timeval timeout;
    timeout.tv_sec = 5*60; //5 mins

    int fd_connected[3] = {-1,-1,-1};

    while(1){
        FD_ZERO(&readset);
        FD_SET(sockfd, &readset);
        FD_SET(STDIN_FILENO, &readset);

        int maxFD = sockfd;
        maxFD++;

        int temp = select(maxFD, &readset, NULL, NULL, &timeout);

        if(temp<0){
            printf("SELECT error!\n");
            break;
        }
        if(temp==0){
            printf("SELECT time-out!\n");
            break;
        }

        if(FD_ISSET(sockfd, &readset)){ //connection request from client to this server
            char buff[MAX_LEN];

            struct sockaddr_in cliaddr;
            socklen_t client_len;
            client_len = sizeof(cliaddr);
            int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &client_len);

            ssize_t total_read = 0;
            ssize_t read = 0;
            while((read = recv(newsockfd, buff+total_read, sizeof(buff)-total_read, 0))>0){
                total_read += read;
            }
            buff[total_read]='\0';
            
            // printf("received: %s, %d\n", buff, read);
            char client_name[20];
            char msg[MAX_LEN];
            int flag = 0; int j=0;
            for(int i=0; i<strlen(buff); i++){
                if(flag==0){
                    if(buff[i]==':'){
                        flag = 1;
                        client_name[j]='\0';
                        j=0;
                        continue;
                    }
                    client_name[j++] = buff[i];
                }
                if(flag==1){
                    msg[j++] = buff[i];
                }
            }
            msg[j]='\0';

            printf("Message from %s: %s\n", client_name, msg);

            close(newsockfd);
        }

        if(FD_ISSET(STDIN_FILENO, &readset)){ //input from STDIN
            char buff[MAX_LEN];
            scanf("%[^\n]s", buff);
            fflush(stdin);
            char client_name[20];
            char msg[MAX_LEN];
            int flag = 0; int j=0;
            for(int i=0; i<strlen(buff); i++){
                if(flag==0){
                    if(buff[i]=='/'){
                        flag=1;
                        client_name[j]='\0';
                        j=0;
                        continue;
                    }
                    client_name[j++] = buff[i];
                }
                if(flag==1){
                    msg[j++] = buff[i];
                }
            }
            msg[j]='\0';

            // printf("%s: %s\n", client_name, msg);

            char client_IP[20];
            int client_port;
            int err = get_client(client_name, client_IP, &client_port, user_info);
            if(err==0){
                printf("NO client with the name %s\n", client_name);
            }

            // printf("%s: %d\n", client_IP, client_port);

            struct sockaddr_in clientaddr;
            clientaddr.sin_family = AF_INET;
            inet_aton(client_IP, &clientaddr.sin_addr);
            clientaddr.sin_port = htons(client_port);

            int newsockfd;
            //socket creation
            if((newsockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
            }

            char send_msg[MAX_LEN];
            sprintf(send_msg, "%s: %s", my_name, msg);

            if((connect(newsockfd, (struct sockaddr *)&clientaddr, sizeof(clientaddr))) < 0){
                perror("Unable to connect to the server");
            }

            ssize_t sent = send(newsockfd, send_msg, strlen(send_msg), 0);
            // printf("%d send\n", sent);
            close(newsockfd);
        }
    }

}