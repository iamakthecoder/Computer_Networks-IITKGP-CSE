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
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <netdb.h>
#include <net/if.h>

#define MAX_BUFFER_SIZE 1009
#define MAX_DOMAINS 10
#define RETRIES 4 // Number of retries before giving up
#define TTL 64
#define LITTLE_ENDIAN_ORDER 0
#define BIG_ENDIAN_ORDER 1
#define PROTOCOL 254

#define INTERFACE "lo"
#define SOURCE_MAC "ec:63:d7:3a:06:dd"
#define LOCAL_IP_ADDRESS "127.0.0.1"

#define DROP_PROB 0.2

// #define DEBUG   USE THIS BY UNCOMMENTING FOR PRINTING MESSAGES

unsigned char buffer[1010];
unsigned char reply[1000];
int pos = 0;
int siz_of_reply = 0;
int id = 750;
unsigned short ip_id = 5;

// Returns 1 if the message is dropped, 0 otherwise
int drop_message(double probability) {
    // Generate a random number between 0 and 1
    double random_number = ((double)rand() / RAND_MAX);

    // Compare the random number with the probability threshold
    if (random_number <= probability) {
        // Message is dropped
        return 1;
    } else {
        // Message is not dropped
        return 0;
    }
}

int get_ipid()
{

    ip_id++;
    ip_id = ip_id % (1 << 16);
    return ip_id;
}

void create_reply(unsigned char *start_)
{

    int start_pos = 0;


    reply[pos++] = start_[start_pos++];
    reply[pos++] = start_[start_pos++];


    reply[pos++] = 0x01; // Message bit: Query
    start_pos++;

    reply[pos++] = start_[start_pos++]; // Number of domains


    int num_of_domains = reply[pos - 1];

    for (int i = 0; i < num_of_domains; i++)
    {

        int y = (start_[start_pos++] & 0xFF) << 24;

        y |= (start_[start_pos++] & 0xFF) << 16;
        y |= (start_[start_pos++] & 0xFF) << 8;
        y |= (start_[start_pos++] & 0xFF);

        unsigned char domain[35];

#ifdef DEBUG
        printf("y= %d\n", y);
        for (int j = 0; j < y; j++)
        {
            printf("%c ", start_[j + start_pos]);
            // domain[j] = start_[j + start_pos];
        }
        printf("\n");
#endif

        memcpy(domain, start_ + start_pos, y);

        start_pos += y;

        domain[y] = '\0';

        printf(" domain %d = %s\n", i + 1, domain);

        // Retrieve host information
        struct hostent *host;
        host = gethostbyname((char *)domain);
        if (host == NULL)
        {
            reply[pos++] = 0x00;
            pos += 4;
            printf("got NULL for domain %s\n", domain);
            continue;
        }

        reply[pos++] = 0x01;

        // Get the first IPv4 address (assuming IPv4 only)
        struct in_addr **addr_list = (struct in_addr **)host->h_addr_list;
        struct in_addr ipv4_addr = *addr_list[0];

        // Convert IP address to 32-bit integer
        uint32_t ip_binary = ipv4_addr.s_addr;

        // Copy IP address to character buffer
        memcpy(reply + pos, &ip_binary, sizeof(ip_binary));

        pos += 4;

    }
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

    #ifdef DEBUG
        
        printf("checksum sent = %d\n",(uint16_t)~sum);
   
    #endif

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

int main(int argc, char *argv[])
{
    srand(time(0));

    // create raw socket
    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0)
    {
        perror("Error during creation of raw socket\n");
        exit(1);
    }

    struct sockaddr_ll sockaddr;

    memset(&sockaddr, 0, sizeof(sockaddr));

    sockaddr.sll_family = AF_PACKET;
    sockaddr.sll_protocol = htons(ETH_P_ALL);

    // wlp0s20f3
    sockaddr.sll_ifindex = if_nametoindex(INTERFACE);
    char source_mac[20];
    sprintf(source_mac, SOURCE_MAC);    

    sscanf((char *)source_mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &sockaddr.sll_addr[0], &sockaddr.sll_addr[1], &sockaddr.sll_addr[2], &sockaddr.sll_addr[3], &sockaddr.sll_addr[4], &sockaddr.sll_addr[5]);



    int len = sizeof(sockaddr);

    if (bind(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
    {
        perror("Error in bind() call\n");
        exit(1);
    }

    while (1)
    {
        // recieve packet

        int siz = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0, NULL, NULL);

        if (siz < 0)
        {
            perror("Error in recvfrom() call\n");
            exit(1);
        }

        if(drop_message(DROP_PROB)){
            continue;
        }

        struct ethhdr *eth_header = (struct ethhdr *)buffer;

        struct iphdr *ip_header = (struct iphdr *)(buffer + sizeof(struct ethhdr));

        unsigned char *start_of_data = buffer + sizeof(struct iphdr) + sizeof(struct ethhdr);

        char *src_ip = inet_ntoa(*(struct in_addr *)&ip_header->saddr);
        // check protocol

        uint16_t cksum = calculate_reply_ip_checksum(ip_header);
        if (cksum != 0)
        {

#ifdef DEBUG
            printf("--------Checksum incorrect dropping packet\n");
#endif

            continue;
        }

        if (ip_header->protocol != (PROTOCOL & 0xFF))
        {      
            continue;
        }

        if (start_of_data[2] != 0)
        {
            continue;
        }

#ifdef DEBUG
        printf(" size of packet recieved %d\n", siz);
        for (int i = sizeof(struct ethhdr) + sizeof(struct iphdr); i < siz; i++)
        {
            printf("%d  ", buffer[i]);
        }
        printf("\n");
#endif

        // create reply
        pos = 0;
        struct ethhdr *eth_header2 = (struct ethhdr *)reply;

        memcpy(eth_header2->h_dest, eth_header->h_source, 6);
        memcpy(eth_header2->h_source, eth_header->h_dest, 6);

        eth_header2->h_proto = htons(ETH_P_IP);


        pos += sizeof(struct ethhdr);

        struct iphdr *ip_header2 = (struct iphdr *)(reply + sizeof(struct ethhdr));

        pos += sizeof(struct iphdr);

        create_reply(start_of_data);

        ip_header2->version = 4 & 0x0F; // IP version

        ip_header2->ihl = 5 & 0x0F; // IP header length (5 words)

        ip_header2->tos = htons(0); // Type of Service

        ip_header2->tot_len = htons(pos - sizeof(struct ethhdr)); // Total length

        ip_header2->id = htons(get_ipid()); // Identification

        ip_header2->frag_off = htons(0); // Fragment offset

        ip_header2->ttl = TTL & 0xFF; // Time to Live

        ip_header2->protocol = PROTOCOL & 0xFF; // Protocol

        ip_header2->check = htons(0); // Checksum (filled later)

        ip_header2->saddr = inet_addr(LOCAL_IP_ADDRESS); // Source IP address

        ip_header2->daddr = ip_header->saddr; // Destination IP address

        ip_header2->check = calculate_ip_checksum(ip_header2);


        int bytes_sent = sendto(sockfd, reply, pos, 0, &sockaddr, len);

        if (bytes_sent < 0)
        {
            perror("send\n");
            close(sockfd);
            exit(1);
        }

        #ifdef DEBUG
        printf("%d bytes sent\n", bytes_sent);
        #endif
    }
}