/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); //듣기 소켓을 오픈한 후에
  while (1) {//전형적인 무한 서버 루프를 실행하고
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit 반복적인 트랜잭션을 수행하고
    Close(connfd);  // line:netp:tiny:close 자신 쪽의 연결을 닫는다
  }
}


void doit(int fd){
  int is_static;
  struct stat sbuf;//?
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  //read request line and headers  요청 라인을 읽고 분석한다 (Rio는 10장임)
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if(strcasecmp(method, "GET")){ //겟만 지원 strcasecmp->string 비교 같을 때 0이다.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  //읽어들임
  read_requesthdrs(&rio);

  //Parse URI from GET request
  //URI를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그 설정
  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0){ //디스크 상에 있지 않으면
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
 
  if(is_static){//Serve static content 정적이라면
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){ // 보통파일인지, 읽기권한을 가지고 있는지 확인
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);//컨텐츠를 클라이언트에게 제공
  }else{// Serve dynamic content
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){ //이 파일이 실행가능한지 확인
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // 동적컨텐츠 제공
  }

}

//tiny는 요청 헤더 내의 어떤 정보도 사용하지 않는다.
//read_request함수를 호출해서 이들을 읽고 무시한다.
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

//tiny는 정적 컨텐츠를 위한 홈 디렉토리가 자신의 현재 디렉토리이고, 실행파일의 홈 디렉토리느 /cgi-bin이라고 가정한다.
//strinf cgi-bin을 포함하는 모든 uri는 동적 컨텐츠를 요청하는 것을 나타낸다고 가정한다
//기본 파일 이름은 ./home.html
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

  if(!strstr(uri, "cgi-bin")){//static content 정적 컨텐츠를 위한 것이라면 strstr- 문자열 안에서 문자열로 검색하기
    strcpy(cgiargs, ""); //cgiargs를 지우고
    strcpy(filename, "."); 
    strcat(filename, uri); // uri를 ./indes.html같은 상대 리눅스 경로이름으로 변환
    if(uri[strlen(uri)-1] == '/') // 만약 uri가 /로 끝난다면
      strcat(filename, "home.html");//기본파일 이름을 추가한다
    return 1;
  }else{//Dynamic content 동적컨텐츠를 위한 것이라면
    //모든 cgi인자를 추출하고
    ptr = index(uri, '?');
    if(ptr){
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }else{
      strcpy(cgiargs, "");
    }
    //나머지 uri부분을 상대 리눅스 파일 이름으로 변환한다.
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

//5개의 서로 다른 정적컨텐츠 지원 (HTML 파일, 무형식 텍스트파일, GIF, PNG, JPEG로 인코딩된 영상)
//지역파일의 내용을 포함하고 있는 본체를 갖는 HTTP 응답을 보낸다
void serve_static(int fd, char *filename, int filesize){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  //Send response headers to client
  get_filetype(filename, filetype); // 접미어 부분을 검사해서 파일타입을 결정하고
  //클라이언트에게 응답줄과 응답헤더를 보낸다
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);
  //빈줄 한개가 헤더를 종료하고 있다는 점에 주목해야한다

  //Send response body to client
  srcfd = Open(filename, O_RDONLY, 0); //읽기 위해서 filename을 오픈하고, 식별자를 얻어온다
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //리눅스 mmap함수는 요청한 파일을 가상 메모리 영역으로 매핑한다
  Close(srcfd);//이 파일을 닫는다. 이렇게 하지 않으면 치명적일 수 있는 메모리 누수가 발생할 수 있다
  Rio_writen(fd, srcp, filesize); // 실제로 파일을 클라이언트에게 전송한다.
  //Rio_writen함수는 주소 srcp에서 시작하는 filesize바이트를 클라이언트에게 연결식별자로 복사한다.
  Munmap(srcp, filesize);// 매핑된 가상메모리 주소를 반환한다.
}

//Derive file type from filename
void get_filetype(char *filename, char *filetype){
  if(strstr(filename, ".html")){
    strcpy(filetype, "text/html");
  }else if(strstr(filename, ".gif")){
    strcpy(filetype, "image/gif");
  }else if(strstr(filename, ".png")){
    strcpy(filetype, "image/png");
  }else if(strstr(filename, ".jpg")){
    strcpy(filetype, "image/jpeg");
  }else{
    strcpy(filetype, "text/plain");
  }
}

//tiny는 자식 프로세스를 fork하고 그 후에 CGI 프로그램을 자식의 컨텍스트에서 실행하며 모든 종류의 동적 컨텐츠를 제공한다.
void serve_dynamic(int fd, char *filename, char *cgiargs){
  char buf[MAXLINE], *emptylist[] = {NULL};

  //Return first part of HTTP response
  //클라이언트에 성공을 알려주는 응답라인을 보내는 것으로 시작
  //CGI 프로그램은 응답의 나머지 부분을 보내야한다 
  //이것은 우리가 기대하는 것 만큼 견고하지 않은데, 그 이유는 이것이 CGI프로그램이 에러를 만날 수 있다는 가능성을 염두에 두지 않았기 때문
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  //이 응답의 첫번째 부분을 보낸 후에
  // 새로운 자식 프로세스를 포크한다
  if(Fork() == 0){//child
    //Real server woudl set all CGI vars here
    setenv("QUERY_STRING", cgiargs, 1);// 자식은 QUERY_STRING 환경변수를 요청 URI의 CGI 인자들로 추기화 한다
    //실제 서버는 여기서 다른 CGI 환경변수들도 마찬가지로 설정한다는 점에 유의하라. 단순성을 위해 이 단계는 생각했다
    Dup2(fd, STDOUT_FILENO); // Redirect stdout to client 자식은 자식의 표준 출력을 연결파일 식별자로 재지정하고
    Execve(filename, emptylist, environ); // Run CGI program CGI 프롤그램을 로드하고 실행한다.
    //CGI 프로그램이 자식 컨텍스트에서 실행되기 때문에 execve함수를 호출하기 전에 존재하던 열린 파일들과 환경변수들에도 동일하게 접근 할 수 있다.
    //그래서 CGI프로그램이 표준 출력에 쓰는 모든 것은 직접 클라이언트 프로세스로 부모 프로세스의 어떠 ㄴ간섭도 없이 전달된다
  }
  //한편 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait함수에서 블록된다
  Wait(NULL); //Parent waits for and reaps child
}

//명백한 오류 체크
//HTTP 응답을 응답라인에 적절한 상태 코드와 상태 메세지와 함께 보낸다.
//HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야한다.
//그래서 HTML컨텐츠를 한개의 스트링 으로 만드는 선택을 하였으면, 이로인해 그 크기를 쉽게 결정 할 수 있다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  //Build the HTTP response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s $s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}