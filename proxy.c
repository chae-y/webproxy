#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int fd);
void parse_uri(char *url, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

int main(int argc, char **argv) {
  // printf("%s", user_agent_hdr);
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];

  struct sockaddr_storage clientaddr; //generic sockaddr struct which is 28bytes. the same use as sockadder

  if(argc !=2){
    fprintf(stderr, "usage :%s <port> \n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);//듣기 소켓을 오픈한 후에
  while(1){//전형적인 무한 서버 루프를 실행하고
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    //print acceted message
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Acceepted connection from (%s %s).\n", hostname, port);

    //sequential handle the client transaction
    doit(connfd); // 반복적인 트랜잭션을 수행하고

    Close(connfd);// 자신 쪽의 연결을 닫는다
  }
  return 0;
}

//handle the client HTTP transaction
void doit(int fd){
  int end_serverfd; // the end server file descriptor

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  //store the request line arguments
  char hostname[MAXLINE], path[MAXLINE];
  int port;

  rio_t rio, server_rio;

//요청라인을 읽고 분석한다
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);; //read the client request line

  if(strcasecmp(method, "GET")){
    printf("Proxy does not implement the method");
    return;
  }
  //parse the uri to get hostname, file path, port
  parse_uri(uri, hostname, path, &port);

  //bulid the http header which will send to the end server
  build_http_header(endserver_http_header, hostname, path, port, &rio);

  //connet to the end serer
  end_serverfd = connect_endServer(hostname, port, endserver_http_header);
  if(end_serverfd < 0){
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd);
  //write the http header to endserver
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

  //receive message from end sever and send to the client
  size_t n;
  while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0){
    printf("proxy receiver %d bytes, then send\n", n);
    Rio_writen(fd, buf, n);
  }
  Close(end_serverfd);
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio){
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  //request line
  sprintf(request_hdr, requestlint_hdr_format, path);
  //get other request header for client rio and change it
  while(Rio_readlineb(client_rio, buf, MAXLINE) > 0){
    if(strcmp(buf, endof_hdr) == 0) break;//eof
    if(!strncasecmp(buf, host_key, strlen(host_key))){//host:
      strcpy(host_hdr, buf);
      continue;
    }

    if(!strncasecmp(buf, connection_key, strlen(connection_key)) 
      && !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
      && !strncasecmp(buf, user_agent_key, strlen(user_agent_key))){
        strcat(other_hdr, buf);
    }
  }
  if(strlen(host_hdr) == 0){
    sprintf(host_hdr, host_hdr_format, hostname);
  }
  sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
  return;
}

//connect to the end server
inline int connect_endServer(char *hostname, int port, char *http_header){
  char portStr[100];
  sprintf(portStr, "%d", port);
  return Open_clientfd(hostname, portStr);
}

//parse the uri to get hostname, file path, port
void parse_uri(char *uri, char *hostname, char *path, int *port){
  *port = 80;
  char *pos = strstr(uri, "//");

  pos = pos != NULL ? pos+2:uri;

  char *pos2 = strstr(pos, ":");
  if(pos2 != NULL){
    *pos2 = '\0';
    sscanf(pos, "%s", hostname);
    sscanf(pos2+1, "%d%s", port, path);
  }else{
    pos2 = strstr(pos, "/");
    if(pos2 != NULL){
      *pos2 = '\0';
      sscanf(pos, "%s", hostname);
      *pos2 = '/';
      sscanf(pos2, "%s", path);
    }else{
      scanf(pos, "%s", hostname);
    }
  }
  return ;
}
