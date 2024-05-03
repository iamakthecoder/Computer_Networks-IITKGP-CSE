all: server client

server: simDNSServer.c
	gcc simDNSServer.c -o server

client: simDNSClient.c
	gcc simDNSClient.c -o client

clean:
	rm -rf server client