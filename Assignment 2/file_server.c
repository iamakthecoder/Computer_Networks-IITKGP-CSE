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

//perform the encryption (as per the key value) on the characters stored in buffer (on the text of the given size)
void encrypt_text(char *buffer, int key, int size){
    int count = 0;
    while(count<size){
        char c = *(buffer+count);
        if((c>='a' && c<='z') || (c>='A' && c<='Z')){
            int temp = 0;
            char base;
            if(c>='a' && c<='z'){
                temp = c-'a';
                base='a';
            }
            else{
                temp = c-'A';
                base='A';
            }

            temp = (temp+key)%26;
            *(buffer+count) = (char)(base+temp);
        }
        count++;
    }

}

//perform the encryption, on the contents from orig_filefd, and store it in the encrypted_filefd
int caesar_cipher_encryption(int orig_filefd, int encrypted_filefd, int key){
    char buffer[BUFFSIZE];
    int bytes_read;
    while((bytes_read = read(orig_filefd, buffer, BUFFSIZE)) > 0){
        encrypt_text(buffer, key, bytes_read);
        if(write(encrypted_filefd, buffer, bytes_read) == -1){
            perror("Error while writing the encrypted text");
            return 0; //returns 0 on failure
        }
    }

    return 1; //returns 1 on success
}

void handle_client(int sockfd){
    int key;
    ssize_t bytes_received = 0;
    //receive the key from the client, for encryption
    while(bytes_received < sizeof(int)){
        ssize_t bytes_read = recv(sockfd, ((char *)&key)+bytes_received, sizeof(int)-bytes_received, 0);
        if(bytes_read==-1){
            perror("Error receiving key");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        else if(bytes_read==0){
            perror("Connection closed while receiving the key");
            close(sockfd);
            exit(0);
        }
        else{
            bytes_received += bytes_read;
        }
    }

    //get the client's IP address and port number...
    char clientIP[INET_ADDRSTRLEN];
    struct sockaddr_in cliaddr;
    socklen_t clientlen = sizeof(cliaddr);
    if(getpeername(sockfd, (struct sockaddr *)&cliaddr, &clientlen) == -1){
        perror("Error getting client IP details");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    inet_ntop(AF_INET, &(cliaddr.sin_addr), clientIP, INET_ADDRSTRLEN);

    //create the temporary file to store the file content received from the client
    char filename[50];
    sprintf(filename, "%s.%d.txt", clientIP, ntohs(cliaddr.sin_port));
   
    int filefd = open(filename, O_RDWR | O_CREAT | O_TRUNC , S_IRUSR | S_IWUSR);
    if(filefd == -1){
        perror("Error creating temporary file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    //receive the file content from the client, for encryption
    char buffer[BUFFSIZE];
    while((bytes_received = recv(sockfd, buffer, BUFFSIZE, 0)) > 0){
        //store the received file content to the temporary file
        if(write(filefd, buffer, bytes_received) == -1){
            perror("Error writing to the temporary file");
            close(filefd);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }
    
    lseek(filefd, 0, SEEK_SET); //moving the cursor to the beginning

    //creating the file to store the encrypted text
    char encrypted_filename[60];
    sprintf(encrypted_filename, "%s.enc", filename);

    int encrypted_filefd = open(encrypted_filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if(encrypted_filefd == -1){
        perror("Error opening the file for encryption");
        close(filefd);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    //perform the caesar cipher encryption, which will be stored in the required file
    int flag = caesar_cipher_encryption(filefd, encrypted_filefd, key);
    if(flag==0){ //if the above encryption failed
        printf("Encryption failed.\n");
        close(filefd);
        close(encrypted_filefd);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    close(filefd);

    lseek(encrypted_filefd, 0, SEEK_SET); //moving the cursor to the beginning
    //read and send the encrypted file contents to the client
    while((bytes_received=read(encrypted_filefd, buffer, BUFFSIZE)) > 0){
        ssize_t bytes_sent = 0;
        while(bytes_sent<bytes_received){
            ssize_t sent = send(sockfd, buffer+bytes_sent, bytes_received-bytes_sent, 0);
            if(sent==-1){
                perror("Error sending encrypted file to the client");
                close(encrypted_filefd);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            else if(sent==0){
                perror("Connection closed while sending the encrypted file contents.");
                close(encrypted_filefd);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            else{
                bytes_sent += sent;
            }
        }
    }

    close(encrypted_filefd);

    shutdown(sockfd, SHUT_WR); //to indicate the EOF (i.e. no further content to be sent from server to client)

    return;
}

int main(){

    int sockfd, newsockfd;
    //socket creation
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    //setting up server address and port
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(10010);

    //binding the socket
    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        perror("Socket binding failed");
        exit(EXIT_FAILURE);
    }

    printf("Server running...\n");

    listen(sockfd, 5);

    struct sockaddr_in cliaddr;
    socklen_t client_len;
    while(1){

        client_len = sizeof(cliaddr);
        //accept the connection from a client
        newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &client_len);

        if(newsockfd < 0){
            perror("Connection-Accept error");
            exit(0);
        }

        if(fork() == 0){
            close(sockfd);

            handle_client(newsockfd);

            close(newsockfd);
            exit(0);
        }

        close(newsockfd);
    }
}