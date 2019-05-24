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

void doit(int clientfd, char *url_log, size_t *size);
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
        return;
    }
}

sem_t mutex;

/* pass args to thread for log printing */
struct addr_arg {
    struct sockaddr_storage clientaddr;
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
    struct addr_arg *clientaddr_arg;
    pthread_t tid;
    Sem_init(&mutex, 0, 1);
    Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        clientaddr_arg = Malloc(sizeof(struct addr_arg));
        clientaddr_arg->connfd = Accept(listenfd, (SA *)&clientaddr_arg->clientaddr, &clientlen);

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
    char log_string[MAXLINE], url_log[MAXLINE];

    doit(clientaddr_arg.connfd, url_log, &size);
    format_log_entry(log_string, (struct sockaddr_in *)& clientaddr_arg.clientaddr, url_log, size);
    P(&mutex);
    printf("%s\n", log_string);
    V(&mutex);
    size = 0;

    return NULL;
}

void doit(int clientfd,char *url_log,size_t *size)
{
    rio_t client_rio;
    rio_t server_rio;
    int serverfd;
    ssize_t n;
    int req_size = 0, resp_size = 0, resp_total_size = 0;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];

    /* Read request line and headers */
    Rio_readinitb(&client_rio, clientfd);
    if ((n = Rio_readlineb_w(&client_rio, buf, MAXLINE)) == 0)
        return;
    if (sscanf(buf, "%s %s %s", method, uri, version) < 3) {
        return;
    }
    strcpy(url_log, uri);

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        *size = 0;
        return;
    }

    /* Parse URI from GET request */
    if (parse_uri(uri, hostname, pathname, port) < 0) {
        return;
    }
    memset(buf, 0, MAXLINE);

    /* Connnect with end server and send request */
    if ((serverfd = Open_clientfd(hostname, port)) < 0) {
        return;
    }

    if (sprintf(buf, "%s /%s %s\r\n", method, pathname, version) == -1) {
        return;
    }

    /* send request to server */
    Rio_writen_w(serverfd, buf, strlen(buf));

    /* Send request header */
    while((n = Rio_readlineb_w(&client_rio, buf, MAXLINE)) != 0) {
        Rio_writen_w(serverfd, buf, strlen(buf));
        if(!strncasecmp(buf, "Content-Length", 14)) {
            req_size = atoi(buf + 15);
        }
        if (!strcmp(buf, "\r\n")) {
            break;
        }
    }

    /* Send request body */
    if (req_size > 0) {
        for (int i = 0; i < req_size; i++) {
            /* Deal with read error as encountering EOF */
            if ((n = Rio_readnb_w(&client_rio, buf, 1)) == 0) {
                break;
            }

            Rio_writen_w(serverfd, buf, 1);
        }
    }

    Rio_readinitb(&server_rio, serverfd);

    /* Send response header */
    while((n = Rio_readlineb_w(&server_rio, buf, MAXLINE)) != 0) {
        resp_total_size += n;
        Rio_writen_w(clientfd, buf, strlen(buf));
        if(!strncasecmp(buf, "Content-Length", 14)) {
            resp_size = atoi(buf + 15);
        }
        if (!strcmp(buf, "\r\n")) {
            break;
        }
    }

    /* Send response body */
    if (resp_size > 0) {
        for (int i = 0; i < resp_size; i++) {
            if ((n = Rio_readnb_w(&server_rio, buf, 1)) == 0) {
                break;
            }
            resp_total_size++;
            Rio_writen_w(clientfd, buf, 1);
        }
    }
    *size = resp_total_size;

    Close(serverfd);
    Close(clientfd);
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


