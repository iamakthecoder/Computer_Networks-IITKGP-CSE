#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 

#define MAXLEN 1024

int main(int argc, char* argv[]){
    //get the required filename through command lne arguments
    if(argc==1){
        printf("Please provide the required text file name as the command-line argument!\n");
        exit(0);
    }

    //socket creation
    int sockfd;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        perror("SOCKET CREATION FAILED");
        exit(EXIT_FAILURE);
    }

    //server address...
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(10010);
    int err = inet_aton("127.0.0.1", &servaddr.sin_addr);
    if(err==0){
        perror("IP-conversion failed");
        exit(EXIT_FAILURE);
    }

    char *txt_filename = argv[1]; //required filename 
    char request_buffer[MAXLEN]; //buffer to store the request message to send to server
    int words_received = 0; //count of words received by server
    char temp[MAXLEN];
    char temp1[] = "WORD";
    int file_name_sent = 0; //flag to check if the filename has been sent by client to the server
    char received_buffer[MAXLEN]; //buffer to store the message received by client
    char file_not_found[MAXLEN]; //to store the message the server will send if the given filename is not found
    sprintf(file_not_found, "NOTFOUND %s", txt_filename);
    FILE *fptr = NULL; //file to which the client will write the received words
    while(1){
        
        if(file_name_sent==0){//send the text file name to the server
            sendto(sockfd, (const char *)txt_filename, strlen(txt_filename), 0, (const struct sockaddr*)&servaddr, sizeof(servaddr));
            file_name_sent = 1;
        }
        else{//send the word number request to the server
            sprintf(temp, "%d", words_received);
            strcpy(request_buffer, temp1);
            strcat(request_buffer, temp);
            sendto(sockfd, (const char*)request_buffer, strlen(request_buffer), 0, (const struct sockaddr*)&servaddr, sizeof(servaddr));
        }

        //receive the message from the server
        int n = recvfrom(sockfd, (char *)received_buffer, MAXLEN, 0, NULL, NULL);
        received_buffer[n]='\0';

        //if the file is not found (by the server)
        if(strcmp(received_buffer, file_not_found)==0){
            printf("File %s not found\n", txt_filename);
            break;
        }
        
        if(words_received==0){//when the first word is sent by the server...
            if(strcmp(received_buffer, "HELLO")==0){//...should be HELLO
                words_received = 1;
                fptr = fopen("txt_file_by_client.txt", "w"); //to write the received words to the file
                if(fptr==NULL){
                    printf("COULD NOT CREATE THE TEXT FILE (by client)");
                    break;
                }
                continue;
            }
            else{
                printf("NOT EXPECTED: First word received is not 'HELLO' but '%s'!\n", received_buffer);
                break;
            }
        }

        //when the last word is received by the client (which will be 'END')
        if(strcmp(received_buffer, "END")==0){
            break;
        }

        //write the received word to the text file (created by the client)
        fprintf(fptr, received_buffer);
        fprintf(fptr, "\n");
        
        words_received++; //counter of the words received till now
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