/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

void doit(int clientfd, size_t *size);
void redir_response(rio_t *rp, int clientfd, size_t *res_size);
void *thread(void *vargp);

ssize_t Rio_readn_w(int fd, void *usrbuf, size_t n)
{
    ssize_t size;
    if ((size = rio_readn(fd, usrbuf, n)) < 0)
    {
        size = 0;
        fprintf(stderr, "Rio_readn error");
    }
    return size;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t size;
    if ((size = rio_readnb(rp, usrbuf, n)) < 0)
    {
        size = 0;
        fprintf(stderr, "Rio_readnb error");
    }
    return size;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t size;
    if ((size = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    {
        size = 0;
        fprintf(stderr, "Rio_readlineb error");
    }
    return size;
}

void Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n)
    {
        fprintf(stderr, "Rio_writen error");
    }
}

sem_t mutex;

/* pass args to thread for log printing */
struct addr_arg {
    struct sockaddr_storage clientaddr;
    //char client_hostname[MAXLINE];
    //char client_port[MAXLINE];
    int connfd;
};

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    int listenfd;
    socklen_t clientlen;
    //struct sockaddr_storage clientaddr;
    struct addr_arg *clientaddr_arg;
    pthread_t tid;
    Sem_init(&mutex, 0, 1);

    listenfd = Open_listenfd(argv[1]);
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        clientaddr_arg = Malloc(sizeof(struct addr_arg));
        clientaddr_arg->connfd = Accept(listenfd, (SA *)&clientaddr_arg->clientaddr, &clientlen);
printf("connected");
        Pthread_create(&tid, NULL, thread, clientaddr_arg);
    }
}

/*
 * thread - Thread routine
 * create a thread to deal with connection request
 */
void *thread(void *vargp)
{
    struct addr_arg clientaddr_arg = *((struct addr_arg *)vargp);
    Pthread_detach(Pthread_self());
    Free(vargp);

    size_t size;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    char log_string[MAXLINE];

    socklen_t clientlen = sizeof(struct sockaddr_storage);
    Getnameinfo((SA *) &clientaddr_arg.clientaddr, clientlen, client_hostname, MAXLINE,
                client_port, MAXLINE, 0);
    P(&mutex);
    printf("Connected to (%s %s)\n", client_hostname, client_port);
    V(&mutex);
    doit(clientaddr_arg.connfd, &size);
    format_log_entry(log_string, (struct sockaddr_in *)& clientaddr_arg.clientaddr, client_hostname, size);
    P(&mutex);
    printf("%s\n", log_string);
    V(&mutex);
    size = 0;

    return NULL;
}

void doit(int clientfd,size_t *size)
{
    rio_t client_rio;
    rio_t server_rio;
    int serverfd;
    ssize_t n;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];

    // Read request line and headers
    Rio_readinitb(&client_rio, clientfd);
    if ((n = Rio_readlineb_w(&client_rio, buf, MAXLINE)) == 0)
        return;
    sscanf(buf, "%s %s %s", method, uri, version);

    printf("req:%s", buf);

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        *size = 0;
        return;
    }

    // Parse URI from GET request
    parse_uri(uri, hostname, pathname, port);
    memset(buf, 0, MAXLINE);

    // Connnect with end server and send request
    //printf("conn server: %s %s\n",hostname, port);
    serverfd = Open_clientfd(hostname, port);

    int bufn = sprintf(buf, "%s /%s %s\r\n", method, pathname, version);
    if (bufn == -1) {
        return;
    }
    //printf("send req: %s to serverfd: %d with bufn: %d\n", buf, serverfd, bufn);
    Rio_writen_w(serverfd, buf, strlen(buf));  // send request to server
    //printf("get HOSTNAME %s:%s\n", hostname, port);
    //printf("request:%s",buf);


    // define size to count req and resp total size (header and body)
    int req_size = 0, resp_size = 0;

    // Send request header
    printf("req header\n");
    while((n = Rio_readlineb_w(&client_rio, buf, MAXLINE)) != 0) {
        printf("write block for buf: %s\n", buf);
        Rio_writen_w(serverfd, buf, strlen(buf));
        if(!strncasecmp(buf, "Content-Length", 14)) {
            req_size = atoi(buf + 15);
        }
        if (!strcmp(buf, "\r\n")) {
            break;
        }
        printf("write end\n");
    }

    // Send request body
    printf("req body size : %d\n", req_size);
    if (req_size > 0) {
        for (int i = 0; i < req_size; i++) {
            printf("read byte\n");
            Rio_readnb_w(&client_rio, buf, 1);
            printf("write byte: %s\n", buf);
            Rio_writen_w(serverfd, buf, 1);
            printf("write byte end\n");
        }
    }

    Rio_readinitb(&server_rio, serverfd);

    // Send response header
    printf("resp header\n");
    while((n = Rio_readlineb_w(&server_rio, buf, MAXLINE)) != 0) {
        printf("write resp for buf: %s\n", buf);
        Rio_writen_w(clientfd, buf, strlen(buf));
        if(!strncasecmp(buf, "Content-Length", 14)) {
            resp_size = atoi(buf + 15);
        }
        if (!strcmp(buf, "\r\n")) {
            break;
        }
    }

    // Send response body
    printf("resp body size : %d\n", resp_size);
    if (resp_size > 0) {
        for (int i = 0; i < resp_size; i++) {
            Rio_readnb_w(&server_rio, buf, 1);
            Rio_writen_w(clientfd, buf, 1);
        }
    }

    Close(serverfd);
    Close(clientfd);
}

/*
 * redir_response
 *
 *
 * Read response from end server and forward it to client, extract the
 * response size.
 */
void redir_response(rio_t *rp, int clientfd, size_t *res_size) {
    char buf[MAXLINE];
    size_t n;
    char *sizestr;

    while((n = Rio_readlineb_w(rp, buf, MAXLINE)) != 0) {
        if(strncasecmp(buf, "Content-Length:", 15) == 0) {
            sizestr = strchr(buf, ' ');
            sizestr++;
            *res_size = (size_t) atoi(sizestr);
        }
        Rio_writen_w(clientfd, buf, n);
        if (!strcmp(buf, "\r\n")) {
            break;
        }
    }
}


/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 12, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri, size);
}


