#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 

#define MAXLEN 1024

int main(){

    //socket creation
    int sockfd;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd<0){
        perror("SOCKET CREATION FAILED");
        exit(EXIT_FAILURE);
    }

    //server address...
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(10010);
    //binding the socket
    if(bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0 ){
        perror("SOCKET BINDING WITH THE SERVER ADDRESS FAILED");
        exit(EXIT_FAILURE);
    }

    printf("\nServer running...\n");

    //client address variables...
    struct sockaddr_in cliaddr;
    memset(&cliaddr, 0, sizeof(cliaddr));
    socklen_t len = sizeof(cliaddr);

    FILE* fptr = NULL; //file to be opened

    char recieve_buffer[MAXLEN]; //buffer to store the received message
    int filename_received = 0; //flag to check if the filename has been received
    char send_buffer[MAXLEN]; //buffer to store the message to be sent
    int words_sent = 0; //counter to check the number of words sent till now
    int flag = 0;
    while(1){
        //receive the message from the client
        int n = recvfrom(sockfd, (char *)recieve_buffer, MAXLEN, 0, (struct sockaddr*)&cliaddr, &len);
        recieve_buffer[n] = '\0';

        if(filename_received==0){//if the filename is received, and needs to be opened now...
            fptr = fopen(recieve_buffer, "r");
        }
        if(fptr==NULL){ //if the request filename is not found...
            char file_not_found_msg[MAXLEN];
            sprintf(file_not_found_msg, "NOTFOUND %s", recieve_buffer);
            sendto(sockfd, file_not_found_msg, strlen(file_not_found_msg), 0, (struct sockaddr*)&cliaddr, len);
            continue;
        }

        //to check if the requested word matches accordingly with the number of sent till now
        if(filename_received){
            int word_num_requested;
            if(sscanf(recieve_buffer, "%*[^0-9]%d", &word_num_requested)!=1){
                continue;
            }
            if(word_num_requested != words_sent){
                continue;
            }
        }

        filename_received = 1;

        //send the required word to the client
        if(fscanf(fptr, "%s", send_buffer) != EOF){
            sendto(sockfd, send_buffer, strlen(send_buffer), 0, (struct sockaddr*)&cliaddr, len);
            words_sent++;
        }

        if(strcmp(send_buffer, "END")==0){
            filename_received = 0;
            words_sent = 0;
        }
        
    }

    close(sockfd); //close the socket
    if(fptr){
        fclose(fptr); //close the file
    }

    return 0;
}

/*
Q: Is this a good file transfer protocol?
A: No, since the UDP Protocol is unreliable, thus there is no guarantee if a message sent from one host will be received at the required destination.
    So, it might happen that, say client sends a request to the server, but the server does not receive it, thereby does not serve the client and the client keeps waiting for reply from the server.
*/