#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define BUFFSIZE 100

void sendFile(int sockfd, int filefd, int key){
    ssize_t bytes_sent = 0;
    //send the key (for encryption) to the server
    while(bytes_sent < sizeof(int)){
        ssize_t bytes = send(sockfd, ((char *)&key)+bytes_sent, sizeof(int)-bytes_sent, 0);
        if(bytes==-1){
            perror("Error sending key");
            close(sockfd);
            close(filefd);
            exit(EXIT_FAILURE);
        }
        else if(bytes==0){
            perror("Connection closed while sending the key");
            close(sockfd);
            close(filefd);
            exit(0);
        }
        else{
            bytes_sent += bytes;
        }
    }

    char buffer[BUFFSIZE];
    ssize_t bytes_read;
    //read and send the file contents to the server
    while((bytes_read = read(filefd, buffer, BUFFSIZE)) > 0){
        bytes_sent = 0;
        while(bytes_sent<bytes_read){
            ssize_t sent = send(sockfd, buffer+bytes_sent, bytes_read-bytes_sent, 0);
            if(sent==-1){
                perror("Error sending the file contents.");
                close(sockfd);
                close(filefd);
                exit(EXIT_FAILURE);
            }
            else if(sent==0){
                perror("Connection closed while sending the file contents.");
                close(filefd);
                close(sockfd);
                exit(0);
            }
            else{
                bytes_sent += sent;
            }
        }
    }

    shutdown(sockfd, SHUT_WR); //to indicate the EOF (i.e. no further content to be sent from client to the server)

    return;
}

void recvFile(int sockfd, int filefd){
    char buffer[BUFFSIZE];
    ssize_t bytes_received;
    //receive encrypted text from server (and store it in the required file)
    while((bytes_received = recv(sockfd, buffer, BUFFSIZE, 0)) > 0){
        if(write(filefd, buffer, bytes_received) == -1){
            perror("Error writing the received encrypted text to the file.");
            close(filefd);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    return;
}

int main(){

    int sockfd;

    //setting up the server details (IP and port), to establish contact with...
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    inet_aton("127.0.0.1", &servaddr.sin_addr);
    servaddr.sin_port = htons(10010);

    char cont = 'y';
    char filename[50];

    while(cont == 'y' || cont == 'Y'){
        fflush(stdin);
        printf("Do you want to encrypt a file? (enter [Y/y] for 'yes'): ");
        scanf("%c", &cont);
        getchar();
        if(cont!='y' && cont!='Y'){
            break;
        }

        //socket creation
        if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        //input file name
        int filefd = -1;
        while(filefd==-1){
            printf("Enter the file name: ");
            scanf("%s", &filename);

            filefd = open(filename, O_RDONLY);
            if(filefd==-1){
                printf("Error opening the file.\n");
            }
        }

        //input key for encryption
        int key;
        printf("Enter the key (for encryption): ");
        scanf("%d", &key);

        //make connection to the server
        if((connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0){
            perror("Unable to connect to the server");
            close(filefd);
            exit(EXIT_FAILURE);
        }

        //send file contents to the server
        sendFile(sockfd, filefd, key);
        close(filefd);
        
        //create the file to receive and store the encrypted text from the server
        char encrypted_filename[60];
        sprintf(encrypted_filename, "%s.enc", filename);
        int encrypted_filefd = open(encrypted_filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if(encrypted_filefd == -1){
            perror("Error creating the file to receive the encrypted text");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        //receive the encrypted text from the server
        recvFile(sockfd, encrypted_filefd);
        close(encrypted_filefd);

        printf("The file is encrypted.\n");
        printf("Original file name: %s\n", filename);
        printf("Encrypted file name: %s\n", encrypted_filename);

        close(sockfd);
    }

    return 0;
}