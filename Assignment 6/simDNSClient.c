#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h> // for ETH_P_ALL
#include <netinet/ip.h>
#include <netinet/ether.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <ctype.h>
#include <sys/select.h>
#include <net/if.h>

// #define DEBUG // COMMENT THIS TO JUST PRINT THE REQUIRED OUTPUT, MEANT FOR DEBUGGING

#define MAX_DOMAINS 10
#define RETRIES 4 // Number of retries before giving up
#define TTL 64
#define LITTLE_ENDIAN_ORDER 0
#define BIG_ENDIAN_ORDER 1
#define PROTOCOL 254

#define LOOP_BACK_IP "127.0.0.1"

#define LOCAL_IP_ADDRESS "127.0.0.1"
#define DEST_IP_ADDRESS "127.0.0.1"
#define INTERFACE "lo"


#define LOOP_INTERFACE "lo"



int num_domains;
char *domains[MAX_DOMAINS];

unsigned char payload[1000];
unsigned char reply[1000];

int start_of_reply_pos = 0;
int pos = 0;

int siz_of_packet = 0;

uint16_t id = 0;
uint16_t ip_id = 5;
uint16_t reply_id;

int myEndianNess = -1;




int getEndianNess()
{
    int x = 1;
    char *c = (char *)&x;
    if (*c)
    {
        return LITTLE_ENDIAN_ORDER;
    }
    else
    {
        return BIG_ENDIAN_ORDER;
    }
}

u_int16_t get_ipid()
{

    ip_id++;
    ip_id = ip_id % (1 << 16);
    return ip_id;
}

u_int16_t get_packid()
{
    id++;
    id = id % (1 << 16);
    return id;
}

// Calculate IP checksum

uint16_t calculate_ip_checksum(struct iphdr *ip_header)
{

    uint32_t sum = 0;

    ip_header->check = 0;

    uint16_t *words = (uint16_t *)ip_header;
    int num_words = sizeof(struct iphdr) / sizeof(uint16_t);

    // Add up all 16-bit words
    for (int i = 0; i < num_words; i++)
    {
        sum += words[i];
    }

    // Add carry if sum exceeds 16 bits
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Take one's complement
    return (uint16_t)~sum;
}

uint16_t calculate_reply_ip_checksum(struct iphdr *ip_header)
{

    uint32_t sum = 0;
    uint32_t ori_cksum = ip_header->check;

    #ifdef DEBUG
    printf("checksum of packet recieved is = %d\n",ori_cksum);
    #endif

    ip_header->check = 0;

    uint16_t *words = (uint16_t *)ip_header;
    int num_words = sizeof(struct iphdr) / sizeof(uint16_t);

    // Add up all 16-bit words
    for (int i = 0; i < num_words; i++)
    {
        sum += words[i];
    }

    // Add carry if sum exceeds 16 bits
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    #ifdef DEBUG
    printf("checksum calculated of packet recieved is = %d\n",(uint16_t)~sum);
    #endif

    if (ori_cksum == (uint16_t)~sum)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/*
            a. Only alphanumeric characters are used,
            b. Hyphens can also be used but it cannot be used at the beginning and at the end of a domain name. Two consecutive hyphens are not allowed.
            c. Spaces and special characters (such as !, $, &, _ and so on) are not allowed, except the dot,
            d. The minimum length is 3 characters, and the maximum length is 31 characters.

*/

int isValidDomain(char *domain)
{

    int length = strlen(domain);
    if (length < 3 || length > 31) // Check length
        return 0;

    if (!isalnum(domain[0]) || !isalnum(domain[length - 1])) // Check if alphanumeric at beginning and end
        return 0;

    for (int i = 0; i < length; i++)
    {
        if (!isalnum(domain[i]) && domain[i] != '-' && domain[i] != '.') // Check for valid characters
            return 0;
        if (domain[i] == '-' && (i == 0 || i == length - 1 || domain[i - 1] == '-')) // Check hyphens
            return 0;
    }

    return 1;
}

void construct_payload()
{
    //  ID
    payload[pos++] = (id >> 8) & (0xFF);
    payload[pos++] = (id) & (0xFF);

    // Message bit: Query
    payload[pos++] = 0x00;

    // Number of domains
    payload[pos++] = num_domains & 0x0F;

    // Query strings
    for (int i = 0; i < num_domains; i++)
    {
        // Length of domain name (4 bytes)
        uint32_t length = strlen(domains[i]);

        payload[pos++] = (length >> 24) & 0xFF;
        payload[pos++] = (length >> 16) & 0xFF;
        payload[pos++] = (length >> 8) & 0xFF;
        payload[pos++] = (length) & 0xFF;

        // strncpy(&payload[pos], domains[i], 35);
        strcpy(payload + pos, domains[i]);
        pos += length;
    }

}


void parse_reply_buffer(const char *buffer, int reply_domains)
{

    printf("-----------------------------------------------------------------------------\n");
    int j = 0;

    printf("Total query strings %d\n", reply_domains);
    printf("Query ID %d\n", reply_id);

    for (int i = 0; j < reply_domains; i += 5)
    {

        int valid_ip = buffer[i]; // Get the start byte indicating validity
        if (valid_ip)
        {

            // Extract the IP address (4 bytes)
            uint32_t ip_binary;
            memcpy(&ip_binary, buffer + i + 1, sizeof(ip_binary));

            // Convert IP address to dot-decimal format
            uint8_t *ip_bytes = (uint8_t *)&ip_binary;

            printf("Domain-%d: %s \t\t IP address: %hhu.%hhu.%hhu.%hhu\n", j+1, domains[j], ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);
        }

        else
        {
            printf("Domain-%d: %s \t\t No valid IP\n", j, domains[j]);
        }

        j++;
    }
    printf("-----------------------------------------------------------------------------\n\n");
}

int main(int argc, char *argv[])
{

    int sockfd;
    char *token;
    socklen_t len;
    unsigned char buff[500];
    unsigned char input[1000];
    struct sockaddr_ll sockaddr;
    int flag = 0;
    int flag2 = 0;

    if (argc < 3)
    {
        perror("Please provide source MAC and destination MAC address as command line argument\n");
        exit(1);
    }

#ifdef DEBUG
    printf("%s %s\n", argv[1], argv[2]);
#endif

    // Source MAC address
    // Destination MAC address

    // create raw socket
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0)
    {
        perror("Error during creation of raw socket\n");
        exit(1);
    }



    while (1)
    {

        flag = 0;

        while (flag == 0)
        {
            num_domains=0;

            printf("Enter input in the format 'getIP N <domain-1> <domain-2> ... <domain-N>' OR enter EXIT \n");

            fgets(input, sizeof(input), stdin);

            // Remove newline character if present
            input[strcspn(input, "\n")] = '\0';

            if (strcmp(input, "EXIT") == 0)
            {
                flag2 = 1;
                printf("Exiting prompt loop\n");
                break;
            }

            token = strtok(input, " ");
            if(token==NULL){
                printf("Error: Incorrect Format.\n");
                continue;
            }
            if (strcmp(token, "getIP") != 0)
            {
                printf("Incorrect input format, string should start with getIP\n");
                continue;
            }

            // Extracting N
            token = strtok(NULL, " ");
            if(token==NULL){
                printf("Error: Incorrect Format.\n");
                continue;
            }
            num_domains = atoi(token);

            // Check if N <= 8
            if (num_domains > 8)
            {
                printf("Error: N should be less than or equal to 8.\n");
                continue;
            }

            else if (num_domains <= 0)
            {
                printf("Error: Value of N should be greater than or equal to 1.\n"); 
                continue;
            }

            else
            {
                int flag3 = 0;

                for (int i = 0; i < num_domains; i++)
                {

                    token = strtok(NULL, " ");

                    if(token==NULL){
                        printf("Error: Insufficient domain name arguments.\n");
                        flag3 = 1;
                        break;
                    }

                    if (!isValidDomain(token))
                    {
                        printf("Error: Domain %d does not follow the correct format.\n\n", i + 1);
                        flag3 = 1;
                        break;
                    }

                    domains[i] = token;
                    printf("Domain %d: %s\n", i + 1, domains[i]);
                }

                if (flag3 == 1)
                    continue;
                else
                    flag = 1;
            }
        }

        if (flag2 == 1)
            break;

        // create DNS query

        // Initialize pos to start of buffer
        pos = 0;

        // fil ethernet header
        struct ethhdr *eth_header = (struct ethhdr *)payload; // Ethernet header structure
        sscanf((char *)argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &eth_header->h_dest[0], &eth_header->h_dest[1], &eth_header->h_dest[2], &eth_header->h_dest[3], &eth_header->h_dest[4], &eth_header->h_dest[5]);
        sscanf((char *)argv[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &eth_header->h_source[0], &eth_header->h_source[1], &eth_header->h_source[2], &eth_header->h_source[3], &eth_header->h_source[4], &eth_header->h_source[5]);

        eth_header->h_proto = htons(ETH_P_IP);

        pos += sizeof(struct ethhdr);

#ifdef DEBUG
        printf("%s %s %d\n", ether_ntoa(eth_header->h_dest), ether_ntoa(eth_header->h_source), pos);
#endif

        // ip header
        struct iphdr *ip_header = (struct iphdr *)(payload + sizeof(struct ethhdr)); // IP header structure
        pos += sizeof(struct iphdr);

        // construct the payload
        construct_payload();

        // fill ip header fields
        ip_header->version = 4 & 0x0F; // IP version

        ip_header->ihl = 5 & 0x0F; // IP header length (5 words)

        ip_header->tos = htons(0); // Type of Service

        ip_header->tot_len = htons(pos - sizeof(struct ethhdr)); // Total length or pos-sizeof(struct ethhdr)

        ip_header->id = htons(get_ipid()); // Identification

        ip_header->frag_off = htons(0); // Fragment offset

        ip_header->ttl = TTL & 0xFF; // Time to Live

        ip_header->protocol = PROTOCOL & 0xFF; // Protocol (e.g., UDP)

        ip_header->check = 0; // Checksum (filled later)

        ip_header->saddr = inet_addr(LOCAL_IP_ADDRESS); // Source IP address

        ip_header->daddr = inet_addr(DEST_IP_ADDRESS); // Destination IP address

        ip_header->check = calculate_ip_checksum(ip_header); // checksum filled

        // wait for response

        fd_set readfds;
        struct timeval timeout;

        int j = 0;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        while (j < RETRIES)
        {

            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds); // Add sockfd to the set

            struct sockaddr_ll sockaddr;

            memset(&sockaddr, 0, sizeof(sockaddr));

            sockaddr.sll_family = AF_PACKET;
            sockaddr.sll_protocol = htons(ETH_P_ALL);
            sockaddr.sll_ifindex = if_nametoindex(INTERFACE); // lo  //wlp0s20f3
            
            char source_mac[20];
            sprintf(source_mac, argv[1]); 

            sscanf((char *)source_mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &sockaddr.sll_addr[0], &sockaddr.sll_addr[1], &sockaddr.sll_addr[2], &sockaddr.sll_addr[3], &sockaddr.sll_addr[4], &sockaddr.sll_addr[5]);


            int len = sizeof(sockaddr);

            int bytes_sent = sendto(sockfd, payload, pos, 0, &sockaddr, len);

            printf("___________________%d bytes packet sent, Try number: %d\n\n", bytes_sent, j + 1);

            if (bytes_sent < 0)
            {
                perror("sendto\n");
                close(sockfd);
                exit(1);
            }

            while (1)
            {

                int t = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

                if (t < 0)
                {
                    perror("Select\n");
                    close(sockfd);
                    exit(1);
                }

                else if (t == 0)
                {

                    printf("___________________timeout %d\n", j + 1);
                    break;
                }

                else
                {

                    int bytes_recieved = recvfrom(sockfd, reply, 1000, 0, NULL, NULL);

                    if (bytes_recieved < 0)
                    {
                        perror("recvfrom");
                        close(sockfd);
                        exit(1);
                    }

                    int reply_buff_pos = 0;

                    struct ethhdr *eth_header2 = (struct ethhdr *)reply;

                    reply_buff_pos += sizeof(struct ethhdr);

                    struct iphdr *ip_header2 = (struct iphdr *)(reply + reply_buff_pos);

                    reply_buff_pos += sizeof(struct iphdr);

                    // compare source and protocol

                    uint16_t cksum = calculate_reply_ip_checksum(ip_header2);
                    if (cksum != 0)
                    {

                        continue;
                    }

                    if (!(memcmp(eth_header->h_dest, eth_header2->h_source, ETH_ALEN) == 0 &&
                          ip_header->daddr == ip_header2->saddr && ip_header->saddr == ip_header2->daddr &&
                          ip_header2->protocol == PROTOCOL & 0xFF))
                    {
                        continue;
                    }

                    if (reply[reply_buff_pos + 2] != 1)
                    {
                        continue;
                    }

                    unsigned char *start_of_reply = reply + reply_buff_pos;
                    start_of_reply_pos = 0;

                    reply_id = start_of_reply[start_of_reply_pos] * 256 + start_of_reply[start_of_reply_pos + 1];

                    start_of_reply_pos += 3;

                    if (reply_id != id)
                    {

                        continue;
                    }

                    else
                    {
                        // parse the packet
                        int reply_domains = start_of_reply[start_of_reply_pos++];

                        printf(" protocol : %d ", ip_header2->protocol);
                        printf("New packet recieved, reply_id = %d id = %d\n", reply_id, id);
                        parse_reply_buffer(start_of_reply + start_of_reply_pos, reply_domains);

                        j = RETRIES + 1;
                        break;
                    }
                }
            }

            j++;                 // increment counter
            timeout.tv_sec = 10; // Set timeout for select again (e.g., 5 seconds)
            timeout.tv_usec = 0;
            if (j == RETRIES)
            {
                printf("3 retries completed, reply not received yet, dropping query\n");
            }
        }

        id++;
        id = id % (1 << 16);
        
    }

    close(sockfd);
    return 0;
}