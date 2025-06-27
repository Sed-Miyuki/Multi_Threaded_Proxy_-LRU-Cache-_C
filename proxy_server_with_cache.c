#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>

typedef struct cache_element cache_element;
struct cache_element
{
    char *data;
    int len;
    char *url;
    time_t lru_time_record;
    cache_element *next;
};
int port_number=8080;
int proxy_socketId;
int Max_Bytes=4096;
const int maxClients=1;
#define max_Size 200*(1<<20)     //size of the cache
#define max_Element_Size 10*(1<<20)     //max size of an element in cache

pthread_t tid[maxClients];
static sem_t semaphore;                //   sem_wait() and sem_signal
pthread_mutex_t lock;           //   for race condition

cache_element *head;
int cache_size;

cache_element* find(char* url)
{
    cache_element* site=NULL;
    int temp_lock_val=pthread_mutex_lock(&lock);
    printf("Cache Lock Acquired (Status: %d)\n",temp_lock_val);
    if(head!=NULL) 
    {
        site=head;
        while(site!=NULL) 
        {
            if(!strcmp(site->url,url)) 
            {
                printf("URL found![%s]\n",url);
                printf("LRU Time Before Update: %ld\n",site->lru_time_record);
                site->lru_time_record=time(NULL);
                printf("LRU Time After Update: %ld\n",site->lru_time_record);
                break;
            }
            site=site->next;
        }
    } 
    else printf("URL not found in cache: [%s]\n",url);
    temp_lock_val=pthread_mutex_unlock(&lock);
    printf("Cache Lock Released (Status: %d)\n",temp_lock_val);
    return site;
}
void remove_cache_element()
{
    //the least lru_time_record and delete it.

    //basic Linked_List stuff
    cache_element *p;
    cache_element *q;
    cache_element *temp;

    int temp_lock_val=pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n",temp_lock_val);
    if(head!=NULL)
    {
        for(q=head,p=head,temp=head;q->next!=NULL;q=q->next)
        {
            if(((q->next)->lru_time_record)<(temp->lru_time_record))
            {
                temp=q->next;
                p=q;
            }
        }
        if(temp==head) head=head->next;
        else p->next=temp->next;

        cache_size=cache_size-(temp->len)-sizeof(cache_element)-strlen(temp->url)-1;
        free(temp->data);
        free(temp->url);
        free(temp);
    }
    temp_lock_val=pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}
int add_cache_element(char *data, int size, char* url)
{
    int temp_lock_val=pthread_mutex_lock(&lock);
    printf("Remove cache Lock acquired %d\n", temp_lock_val);
    int element_size=size+1+strlen(url)+sizeof(cache_element);
    if(element_size>max_Element_Size)
    {
        // element is too big, do something else
        temp_lock_val=pthread_mutex_unlock(&lock);
        printf("Add cache lock is unlocked");
    } 
    else 
    {
        while(cache_size+element_size>max_Size) remove_cache_element();
        cache_element* element=(cache_element*)malloc(sizeof(cache_element));
        element->data=(char*)malloc(size+1);
        strcpy(element->data,data);
        element->url=(char*)malloc(1+(strlen(url)*sizeof(char)));
        strcpy(element->url,url);
        element->lru_time_record=time(NULL);
        // element->head and then head->element i.e backword
        element->next=head;
        head=element;
        element->len=size;

        cache_size+=element_size;
        temp_lock_val=pthread_mutex_unlock(&lock);
        printf("Add cache lock is unlocked\n");
        return 1;
    }
    return 0;
}


int connectRemoteServer(char* host_addr, int port_number) 
{
    int remoteSocket=socket(AF_INET,SOCK_STREAM,0);
    if(remoteSocket<0) 
    {
        perror("Error: Failed to create socket");
        return -1;
    }
    printf("Socket created successfully. (Socket FD: %d)\n", remoteSocket);

    // Get host by the provided hostname or IP address
    struct hostent *host=gethostbyname(host_addr);
    if(host==NULL) 
    {
        fprintf(stderr,"Error: No such host \"%s\" exists.\n",host_addr);
        close(remoteSocket);
        return -1;
    }
    printf("Host found: %s\n",host_addr);

    struct sockaddr_in server_addr;
    bzero((char *)&server_addr,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_number);
    bcopy((char *)host->h_addr_list[0],(char *)&server_addr.sin_addr.s_addr,host->h_length);
    printf("Preparing to connect to %s on port %d...\n", inet_ntoa(server_addr.sin_addr), port_number);

    if(connect(remoteSocket,(struct sockaddr *)&server_addr,(socklen_t)sizeof(server_addr))<0) 
    {
        perror("Error: Failed to connect to remote server");
        close(remoteSocket);
        return -1;
    }
    
    printf("Successfully connected to remote server at %s on port %d.\n",inet_ntoa(server_addr.sin_addr), port_number);
    
    return remoteSocket;
}
int handle_request(int clientSocketId,ParsedRequest *request,char *treq)
{
    char *buf=(char*)malloc(sizeof(char)*Max_Bytes);
    strcpy(buf,"GET ");
    strcat(buf,request->path);
    strcat(buf," ");
    strcat(buf,request->version);
    strcat(buf,"\r\n");

    size_t len=strlen(buf);

    // Ensure the "Host" header is set
    if(ParsedHeader_get(request,"Host")==NULL) 
    {
        if(ParsedHeader_set(request,"Host",request->host)<0) 
        {
            printf("Warning: Failed to set 'Host' header.\n");
        }
    }

    // Unparse the headers
    if(ParsedRequest_unparse_headers(request,buf+len,(size_t)Max_Bytes-len)<0) 
    {
        printf("Warning: Failed to unparse headers.\n");
        // Still try to send the request without the header
    }

    int server_port=80; // Default remote server port
    if(request->port!=NULL) server_port=atoi(request->port);

    // Connect to the remote server
    int remoteSocketID=connectRemoteServer(request->host,server_port);
    if(remoteSocketID<0) 
    {
        printf("Error: Unable to connect to remote server at %s:%d.\n",request->host,server_port);
        return -1;
    }
    printf("Connected to remote server %s on port %d.\n",request->host,server_port);

    int bytes_send=send(remoteSocketID,buf,strlen(buf),0);
    if(bytes_send<0) 
    {
        perror("Error: Failed to send request to remote server.\n");
        close(remoteSocketID);
        free(buf);
        return -1;
    }
    printf("Sent request to server (%d bytes).\n",bytes_send);

    bzero(buf,Max_Bytes);
    bytes_send=recv(remoteSocketID,buf,Max_Bytes-1,0);       //as ex-> a v i d /0
    char *temp_buffer=(char*)malloc(sizeof(char)*Max_Bytes); //Temp buffer for caching
    int temp_buffer_size=Max_Bytes;
    int temp_buffer_index=0;

    while(bytes_send>0) 
    {
        // Send the response to the client
        bytes_send=send(clientSocketId,buf,bytes_send,0);
        if(bytes_send<0) 
        {
            perror("Error: Failed to send data to client socket.\n");
            break;
        }
        printf("Sent data to client socket (%d bytes).\n",bytes_send);

        for(int i=0;i<(int)(bytes_send/sizeof(int));i++) 
        {
            temp_buffer[temp_buffer_index]=buf[i];
            temp_buffer_index++;    
        }

        // Reallocate temp buffer if needed
        temp_buffer_size+=Max_Bytes;
        temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size);

        bzero(buf,Max_Bytes);
        bytes_send=recv(remoteSocketID,buf,Max_Bytes-1,0);
    }

    temp_buffer[temp_buffer_index]='\0';
    add_cache_element(temp_buffer,strlen(temp_buffer),treq);
    printf("Response added to cache (size: %lu bytes).\n",strlen(temp_buffer));

    free(buf);
    free(temp_buffer);
    close(remoteSocketID);
    printf("Request handling completed.\n");

    return 0;
}

int checkHTTPversion(char *msg)
{
    int version=-1;
    if(strncmp(msg,"HTTP/1.1",8)==0) version=1;
    else if(strncmp(msg,"HTTP/1.0",8)==0) version=1;
    else version=-1;
    return version;
}
int sendErrorMessage(int socket,int status_code) 
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code) 
    {
        case 400: 
            // 400 Bad Request
            snprintf(str, sizeof(str), 
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 150\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n"
                "<BODY><H1>400 - Bad Request!</H1>\n"
                "<P>It seems your request couldn't be processed due to an invalid format. "
                "Please check your request syntax and try again.</P></BODY></HTML>", 
                currentTime);
            printf("Oops! 400 Bad Request (Client Error)\n");
            send(socket, str, strlen(str), 0);
            break;

        case 403: 
            // 403 Forbidden
            snprintf(str, sizeof(str), 
                "HTTP/1.1 403 Forbidden\r\n"
                "Content-Length: 180\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n"
                "<BODY><H1>403 - Access Denied!</H1>\n"
                "<P>You don't have permission to access the requested resource. "
                "If you believe this is an error, contact the site administrator.</P></BODY></HTML>", 
                currentTime);
            printf("403 Forbidden (Access Denied!)\n");
            send(socket, str, strlen(str), 0);
            break;

        case 404: 
            // 404 Not Found
            snprintf(str, sizeof(str), 
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 160\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n"
                "<BODY><H1>404 - Page Not Found!</H1>\n"
                "<P>Sorry, the page you're looking for doesn't exist. "
                "Please check the URL or return to the homepage.</P></BODY></HTML>", 
                currentTime);
            printf("404 Not Found (Page Missing!)\n");
            send(socket, str, strlen(str), 0);
            break;

        case 500: 
            // 500 Internal Server Error
            snprintf(str, sizeof(str), 
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Length: 200\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n"
                "<BODY><H1>500 - Server Error!</H1>\n"
                "<P>Something went wrong on our end. We're working on fixing the issue. "
                "Please try again later or contact support.</P></BODY></HTML>", 
                currentTime);
            printf("500 Internal Server Error (Something went wrong on our end!)\n");
            send(socket, str, strlen(str), 0);
            break;

        case 501: 
            // 501 Not Implemented
            snprintf(str, sizeof(str), 
                "HTTP/1.1 501 Not Implemented\r\n"
                "Content-Length: 170\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n"
                "<BODY><H1>501 - Feature Not Implemented!</H1>\n"
                "<P>Sorry, the feature you're trying to use is not implemented yet. "
                "Stay tuned for future updates!</P></BODY></HTML>", 
                currentTime);
            printf("501 Not Implemented (Feature coming soon!)\n");
            send(socket, str, strlen(str), 0);
            break;

        case 505: 
            // 505 HTTP Version Not Supported
            snprintf(str, sizeof(str), 
                "HTTP/1.1 505 HTTP Version Not Supported\r\n"
                "Content-Length: 190\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n"
                "Server: CustomServer/14785\r\n\r\n"
                "<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n"
                "<BODY><H1>505 - Unsupported HTTP Version!</H1>\n"
                "<P>Your browser is using an HTTP version that this server doesn't support. "
                "Please upgrade your browser or try a different one.</P></BODY></HTML>", 
                currentTime);
            printf("505 HTTP Version Not Supported (Please upgrade your browser!)\n");
            send(socket, str, strlen(str), 0);
            break;

        default:
            printf("Unknown status code: %d\n", status_code);
            return -1;
    }
    return 1;
}
void *thread_fn(void* socketNew)
{
    printf("Thread ID: %p\n", (void*)pthread_self());
    sem_wait(&semaphore);
    sleep(30);  // Simulate processing time
    int semaphore_value;
    sem_getvalue(&semaphore, &semaphore_value);
    printf("Semaphore value before processing: %d\n",semaphore_value);

    //for good practice
    int socket = *((int *)socketNew);
    free(socketNew);
    int bytes_sent_client,len; 

    char *buffer=(char*)calloc(Max_Bytes,sizeof(char));                 //calloc=mem+null initialise 
    bzero(buffer, Max_Bytes);                                           //malloc=mem
    bytes_sent_client=recv(socket,buffer,Max_Bytes,0);  
    while(bytes_sent_client>0) 
    {
        len=strlen(buffer);
        // Loop until the "\r\n\r\n" sequence is found in the buffer (end of request)
        if(strstr(buffer,"\r\n\r\n")==NULL)                              //first occurrence in a string
        {
            bytes_sent_client=recv(socket,buffer+len,Max_Bytes-len,0);
        } 
        else break;
    }

    // good practice
    char *tempReq=(char*)malloc(strlen(buffer)*sizeof(char)+1);
    for(size_t i=0;i<strlen(buffer);i++) tempReq[i]=buffer[i];

    struct cache_element* cached_response=find(tempReq);
    if(cached_response!=NULL) 
    {
        int cache_size=cached_response->len/sizeof(char);
        int pos=0;
        char response[Max_Bytes];

        while(pos<cache_size) 
        {
            bzero(response,Max_Bytes);
            for(int i=0;i<Max_Bytes && pos<cache_size;i++) response[i] = cached_response->data[pos++];
            send(socket,response,Max_Bytes,0);
        }
        printf("Data retrieved from the Cache!\n\n%s\n\n", response);
    } 
    // If no cache and valid request
    else if(bytes_sent_client>0) 
    {
        len=strlen(buffer);
        ParsedRequest* request = ParsedRequest_create();
        if(ParsedRequest_parse(request,buffer,len)<0) printf("Error: Failed to parse the request.\n");
        else 
        {
            bzero(buffer,Max_Bytes);
            if(!strcmp(request->method,"GET"))            // Only supporting GET method 
            { 
                if(request->host && request->path && checkHTTPversion(request->version)==1) 
                {
                    bytes_sent_client=handle_request(socket,request,tempReq); 
                    if(bytes_sent_client==-1) sendErrorMessage(socket,500);  //Internal Server Error
                } 
                else sendErrorMessage(socket,500); 
            } 
            else printf("This code only supports GET method. Received: %s\n", request->method);
        }
        ParsedRequest_destroy(request);
    } 
    else if(bytes_sent_client<0) perror("Error: Failed to receive data from client.\n");
    else if(bytes_sent_client==0) printf("Client disconnected!\n");

    // Clean up
    shutdown(socket,SHUT_RDWR);                 //RDWR=SHUT Read Write
    close(socket);
    free(buffer);
    sem_post(&semaphore);	

    sem_getvalue(&semaphore,&semaphore_value);
	printf("Semaphore post value:%d\n",semaphore_value);
	free(tempReq);
	return NULL;
}
int main(int argc,char *argv[])
{
    int client_socketId,client_len; // client_socketId == to store the client socket id
	struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned
    sem_init(&semaphore,0,maxClients);
    pthread_mutex_init(&lock,NULL);
    if(argc==2)
    {
        port_number=atoi(argv[1]);
    }
    else
    {
        printf("At least 2 Arguments\n");
        exit(1);
    }

    printf("Starting Proxy server on Port No:%d\n",port_number);
    proxy_socketId=socket(AF_INET,SOCK_STREAM,0);                   //  IPV4,TCP,default
    if(proxy_socketId<0)
    {
        perror("Failed to create Socket\n");
        exit(1);
    }
    int reuse=1;
    // reuse the socket
    if(setsockopt(proxy_socketId,SOL_SOCKET,SO_REUSEADDR,(char *)&reuse,sizeof(reuse))<0)
    {
        perror("setsockopt failed\n");
        exit(1);
    }

    bzero((char *)&server_addr,sizeof(server_addr));     // base value
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_number);             //host->network  i.e.  BIG ENDIAN
    server_addr.sin_addr.s_addr=INADDR_ANY;              //any addr

    if(bind(proxy_socketId,(const struct sockaddr *)&server_addr,sizeof(server_addr))<0)
    {
        perror("Faliure in binding\n");
        exit(1);
    }
    printf("Binding on port:%d\n",port_number);

    if(listen(proxy_socketId,maxClients)<0)
    {
        perror("Error in listening\n");
        exit(1);
    }

    int cnt=0;
    int connected_socketId[maxClients];
    while(1)
    {
        bzero((char *)&client_addr,sizeof(client_addr));
        client_len=sizeof(client_addr);
        client_socketId=accept(proxy_socketId,(struct sockaddr *)&client_addr,(socklen_t *)&client_len);
        if(client_socketId<0)
        {
            printf("Not able to connect");
            exit(1);
        } 
        // else connected_socketId[cnt] = client_socketId;


        struct sockaddr_in* client_pt=(struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr=client_pt->sin_addr;                            //client ipaddress
        char str[INET_ADDRSTRLEN];
        // The function converts the address from network format to presentation format
        inet_ntop(AF_INET,&ip_addr,str,INET_ADDRSTRLEN);
        printf("Client is connected with port number %d and ip address is %s\n",ntohs(client_addr.sin_port), str);

        int *socket_arg = (int *)malloc(sizeof(int));
        *socket_arg = client_socketId;
        pthread_t tid;
        pthread_create(&tid, NULL, thread_fn, (void *)socket_arg);
        pthread_detach(tid);  // Automatically reclaim resources when thread finishes
    }
}
