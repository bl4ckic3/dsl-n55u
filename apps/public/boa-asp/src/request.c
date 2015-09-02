/*
 *  Boa, an http server
 *  Copyright (C) 1995 Paul Phillips <paulp@go2net.com>
 *  Some changes Copyright (C) 1996,97 Larry Doolittle <ldoolitt@boa.org>
 *  Some changes Copyright (C) 1996-2002 Jon Nelson <jnelson@boa.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* $Id: //BBN_Linux/Branch/Branch_for_SDK_Release_MultiChip_20111013/tclinux_phoenix/apps/public/boa-asp/src/request.c#2 $*/

#include "boa.h"
#include <stddef.h> /* for offsetof */
#include <crypt.h>
#include "tcutils.h"
#if !defined(TCSUPPORT_CT) 
#include "../../lib/libtcapi.h"

#ifdef TCSUPPORT_SYSLOG_ENHANCE
#include <syslog.h>
#endif

int total_connections;
struct status status;
char cur_username[129];
#if defined(TCSUPPORT_WEB_SAVE)
int flag_save = 0;
#endif

#ifdef TCSUPPORT_MODEL_NAME_CUSKIT
#define SYS_INFO_PRODUCT_NAME	"ProductName"
#define SYS_INFO_NODE_NAME "SysInfo"
#define SUB_NODE_NAME "Entry"
#endif

restrict_client_t restrict_client;

typedef struct cgibin_exception_file
{
	char filename[60];
} cgibin_exception_file_t;

cgibin_exception_file_t cgibin_exception_file_list[] =
{
		{"modemlog.txt"},
};

#define CGIBIN_EXCEPT_FILE_LIST_SIZE sizeof(cgibin_exception_file_list)/sizeof(cgibin_exception_file_t)

static int sockbufsize = SOCKETBUF_SIZE;

/* function prototypes located in this file only */
static void free_request(request ** list_head_addr, request * req);
static inline struct passwd *getrealpass(const char *user);


static int dynamicPassUrl(char * url);
/*
 * Name: new_request
 * Description: Obtains a request struct off the free list, or if the
 * free list is empty, allocates memory
 *
 * Return value: pointer to initialized request
 */
#if  defined(TCSUPPORT_WEBSERVER_SSL)
extern int server_ssl;		
static request*  newest_request = NULL;
extern int ssl_pending_requests;
extern SSL_CTX *ssl_ctx;
#endif


request *new_request(void)
{
    request *req;

    if (request_free) {
        req = request_free;     /* first on free list */
        dequeue(&request_free, request_free); /* dequeue the head */
    } else {
        req = (request *) malloc(sizeof (request));
        if (!req) {
            log_error_time();
            perror("malloc for new request");
            return NULL;
        }
    }

    memset(req, 0, offsetof(request, buffer) + 1);
		//lee 2006-4-20
		req->cookie = NULL;
		req->authorize = NULL;
		req->restrict_client = &restrict_client;
#if  defined(TCSUPPORT_WEBSERVER_SSL)
		req->ssl = NULL;
#endif
    return req;
}

/*
 * Name: get_request
 *
 * Description: Polls the server socket for a request.  If one exists,
 * does some basic initialization and adds it to the ready queue;.
 */

void get_request(int server_s)
{
    int fd;                     /* socket */
    struct SOCKADDR remote_addr; /* address */
    struct SOCKADDR salocal;
    int remote_addrlen = sizeof (struct SOCKADDR);
    request *conn;              /* connection */
    size_t len;
    static int system_bufsize = 0; /* Default size of SNDBUF given by system */

		//fprintf(stderr,"get_request1\n");
    remote_addr.S_FAMILY = 0xdead;
    fd = accept(server_s, (struct sockaddr *) &remote_addr,
                &remote_addrlen);
		//fprintf(stderr,"get_request2\n");
    if (fd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            /* abnormal error */
            WARN("accept");
        else
            /* no requests */
            pending_requests = 0;
        return;
    }
    if (fd >= FD_SETSIZE) {
        WARN("Got fd >= FD_SETSIZE.");
        close(fd);
	return;
    }
#ifdef DEBUGNONINET
    /* This shows up due to race conditions in some Linux kernels
       when the client closes the socket sometime between
       the select() and accept() syscalls.
       Code and description by Larry Doolittle <ldoolitt@boa.org>
     */
#define HEX(x) (((x)>9)?(('a'-10)+(x)):('0'+(x)))
    if (remote_addr.sin_family != AF_INET) {
        struct sockaddr *bogus = (struct sockaddr *) &remote_addr;
        char *ap, ablock[44];
        int i;
        close(fd);
        log_error_time();
        for (ap = ablock, i = 0; i < remote_addrlen && i < 14; i++) {
            *ap++ = ' ';
            *ap++ = HEX((bogus->sa_data[i] >> 4) & 0x0f);
            *ap++ = HEX(bogus->sa_data[i] & 0x0f);
        }
        *ap = '\0';
        fprintf(stderr, "non-INET connection attempt: socket %d, "
                "sa_family = %hu, sa_data[%d] = %s\n",
                fd, bogus->sa_family, remote_addrlen, ablock);
        return;
    }
#endif

/* XXX Either delete this, or document why it's needed */
/* Pointed out 3-Oct-1999 by Paul Saab <paul@mu.org> */
#ifdef REUSE_EACH_CLIENT_CONNECTION_SOCKET
    if ((setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &sock_opt,
                    sizeof (sock_opt))) == -1) {
        DIE("setsockopt: unable to set SO_REUSEADDR");
    }
#endif

    len = sizeof(salocal);

    if (getsockname(fd, (struct sockaddr *) &salocal, &len) != 0) {
        WARN("getsockname");
        close(fd);
        return;
    }
		//fprintf(stderr,"get_request3\n");
    conn = new_request();
    if (!conn) {
    	//fprintf(stderr,"get_request33\n");
        close(fd);
        return;
    }
    conn->fd = fd;
    conn->status = READ_HEADER;
    conn->header_line = conn->client_stream;
    conn->time_last = current_time;
    conn->kacount = ka_max;

    ascii_sockaddr(&salocal, conn->local_ip_addr, NI_MAXHOST);

    /* nonblocking socket */
    if (set_nonblock_fd(conn->fd) == -1)
        WARN("fcntl: unable to set new socket to non-block");

    /* set close on exec to true */
    if (fcntl(conn->fd, F_SETFD, 1) == -1)
        WARN("fctnl: unable to set close-on-exec for new socket");

    /* Increase buffer size if we have to.
     * Only ask the system the buffer size on the first request,
     * and assume all subsequent sockets have the same size.
     */
    if (system_bufsize == 0) {
        len = sizeof (system_bufsize);
        if (getsockopt
            (conn->fd, SOL_SOCKET, SO_SNDBUF, &system_bufsize, &len) == 0
            && len == sizeof (system_bufsize)) {
            /*
               fprintf(stderr, "%sgetsockopt reports SNDBUF %d\n",
               get_commonlog_time(), system_bufsize);
             */
            ;
        } else {
            WARN("getsockopt(SNDBUF)");
            system_bufsize = 1;
        }
    }
    if (system_bufsize < sockbufsize) {
        if (setsockopt
            (conn->fd, SOL_SOCKET, SO_SNDBUF, (void *) &sockbufsize,
             sizeof (sockbufsize)) == -1) {
            WARN("setsockopt: unable to set socket buffer size");
#ifdef DIE_ON_ERROR_TUNING_SNDBUF
            exit(errno);
#endif
        }
    }

    /* for log file and possible use by CGI programs */
    ascii_sockaddr(&remote_addr, conn->remote_ip_addr, NI_MAXHOST);

    /* for possible use by CGI programs */
    conn->remote_port = net_port(&remote_addr);

    status.requests++;

#ifdef USE_TCPNODELAY
    /* Thanks to Jef Poskanzer <jef@acme.com> for this tweak */
    {
        int one = 1;
        if (setsockopt(conn->fd, IPPROTO_TCP, TCP_NODELAY,
                       (void *) &one, sizeof (one)) == -1) {
            DIE("setsockopt: unable to set TCP_NODELAY");
        }

    }
#endif

#ifndef NO_RATE_LIMIT
    if (conn->fd > max_connections) {
        send_r_service_unavailable(conn);
        conn->status = DONE;
        pending_requests = 0;
    }
#endif                          /* NO_RATE_LIMIT */

    total_connections++;
#if  defined(TCSUPPORT_WEBSERVER_SSL)
	newest_request = conn;
#endif
    enqueue(&request_ready, conn);
    //fprintf(stderr,"get_request4\n");
}


/*
 * Name: free_request
 *
 * Description: Deallocates memory for a finished request and closes
 * down socket.
 */


static void free_request(request ** list_head_addr, request * req)
{
    int i;
    /* free_request should *never* get called by anything but
       process_requests */

    if (req->buffer_end && req->status != DEAD) {
        req->status = DONE;
        return;
    }
    /* put request on the free list */
    dequeue(list_head_addr, req); /* dequeue from ready or block list */

    if (req->logline)           /* access log */
        log_access(req);

    if (req->mmap_entry_var)
        release_mmap(req->mmap_entry_var);
    else if (req->data_mem)
        munmap(req->data_mem, req->filesize);

    if (req->data_fd)
        close(req->data_fd);

    if (req->post_data_fd)
        close(req->post_data_fd);

    if (req->response_status >= 400)
        status.errors++;

    for (i = COMMON_CGI_COUNT; i < req->cgi_env_index; ++i) {
        if (req->cgi_env[i]) {
            free(req->cgi_env[i]);
        } else {
            log_error_time();
            fprintf(stderr, "Warning: CGI Environment contains NULL value" \
                    "(index %d of %d).\n", i, req->cgi_env_index);
        }
    }

#if   defined(TCSUPPORT_WEBSERVER_SSL)
	if(req->ssl)
	{
		//Andy Chiu, 2015/02/09. mssl_conn_t would be free in boa_sslClose().
		boa_sslClose(req->ssl);
    		req->keepalive = KA_INACTIVE;
		req->ssl = NULL;
	}
#endif 
    if (req->pathname)
        free(req->pathname);
    if (req->path_info)
        free(req->path_info);
    if (req->path_translated)
        free(req->path_translated);
    if (req->script_name)
        free(req->script_name);
	//lee authorize	
		if(req->cookie)
			free(req->cookie);
		if(req->authorize)
			free(req->authorize);
									
    if ((req->keepalive == KA_ACTIVE) &&
        (req->response_status < 500) && req->kacount > 0) {
        int bytes_to_move;

        request *conn = new_request();
        if (!conn) {
            /* errors already reported */
//            enqueue(&request_free, req);
            close(req->fd);
        	//tcdbg_printf("free_request: ptr=%08lx\r\n", (unsigned long)req);
            free(req);
            total_connections--;
            return;
        }
        conn->fd = req->fd;
        conn->status = READ_HEADER;
        conn->header_line = conn->client_stream;
        conn->kacount = req->kacount - 1;

        /* close enough and we avoid a call to time(NULL) */
        conn->time_last = req->time_last;

        /* for log file and possible use by CGI programs */
        memcpy(conn->remote_ip_addr, req->remote_ip_addr, NI_MAXHOST);
        memcpy(conn->local_ip_addr, req->local_ip_addr, NI_MAXHOST);

        /* for possible use by CGI programs */
        conn->remote_port = req->remote_port;

        status.requests++;

        /* we haven't parsed beyond req->parse_pos, so... */
        bytes_to_move = req->client_stream_pos - req->parse_pos;
        if (bytes_to_move) {
            memcpy(conn->client_stream,
                   req->client_stream + req->parse_pos, bytes_to_move);
            conn->client_stream_pos = bytes_to_move;
        }
    
        enqueue(&request_block, conn);
        BOA_FD_SET(conn->fd, &block_read_fdset);

        //enqueue(&request_free, req);
    	//tcdbg_printf("free_request: ptr=%08lx\r\n", (unsigned long)req);
        free(req);
        return;
    }

    /*
     While debugging some weird errors, Jon Nelson learned that
     some versions of Netscape Navigator break the
     HTTP specification.

     Some research on the issue brought up:

     http://www.apache.org/docs/misc/known_client_problems.html

     As quoted here:

     "
     Trailing CRLF on POSTs

     This is a legacy issue. The CERN webserver required POST
     data to have an extra CRLF following it. Thus many
     clients send an extra CRLF that is not included in the
     Content-Length of the request. Apache works around this
     problem by eating any empty lines which appear before a
     request.
     "

     Boa will (for now) hack around this stupid bug in Netscape
     (and Internet Exploder)
     by reading up to 32k after the connection is all but closed.
     This should eliminate any remaining spurious crlf sent
     by the client.

     Building bugs *into* software to be compatable is
     just plain wrong
     */

    if (req->method == M_POST) {
        char *buf;

        buf = (char*)malloc(32768);
        read(req->fd, buf, 32768);
        free(buf);
    }
    close(req->fd);
    total_connections--;

//    enqueue(&request_free, req);
	//tcdbg_printf("free_request: ptr=%08lx\r\n", (unsigned long)req);
    free(req);
    return;
}

#if  defined(TCSUPPORT_WEBSERVER_SSL)
void get_ssl_request(void)
{
	request *conn = NULL;
	get_request(server_ssl);
	conn = newest_request;
	newest_request = NULL;
	if (!conn)
		return;
	if(boa_sslAccept(&conn->ssl,conn->fd, ssl_ctx)!=0)
	{
    		close(conn->fd);
    		total_connections--;
    		dequeue(&request_ready, conn);
		free(conn);
		return;
	}
	conn->buffer_start =  conn->buffer_end = 0;
	return;
}
#endif 
/*
 * Name: process_requests
 *
 * Description: Iterates through the ready queue, passing each request
 * to the appropriate handler for processing.  It monitors the
 * return value from handler functions, all of which return -1
 * to indicate a block, 0 on completion and 1 to remain on the
 * ready list for more procesing.
 */


void process_requests(int server_s)
{
    int retval = 0;
    request *current, *trailer;

    if (pending_requests ) {
        get_request(server_s);
#ifdef ORIGINAL_BEHAVIOR
        pending_requests = 0;
#endif
    }	
#if  defined(TCSUPPORT_WEBSERVER_SSL)
    if (ssl_pending_requests) {
        get_ssl_request();
	ssl_pending_requests = 0;
    }	
#endif 
    current = request_ready;

    while (current) {
        time(&current_time);
        if (current->buffer_end && /* there is data in the buffer */
            current->status != DEAD && current->status != DONE) {
            retval = req_flush(current);
            /*
             * retval can be -2=error, -1=blocked, or bytes left
             */
            if (retval == -2) { /* error */
                current->status = DEAD;
                retval = 0;
            } else if (retval >= 0) {
                /* notice the >= which is different from below?
                   Here, we may just be flushing headers.
                   We don't want to return 0 because we are not DONE
                   or DEAD */

                retval = 1;
            }
        } else {
            switch (current->status) {
            case READ_HEADER:
            case ONE_CR:
            case ONE_LF:
            case TWO_CR:
            		
                retval = read_header(current);
                break;
            case BODY_READ:
                retval = read_body(current);
                break;
            case BODY_WRITE:
                retval = write_body(current);
                break;
            case WRITE:
                retval = process_get(current);
                break;
            case PIPE_READ:            		
                retval = read_from_pipe(current);                
                break;
            case PIPE_WRITE:
                retval = write_from_pipe(current);
                break;
            case DONE:
                /* a non-status that will terminate the request */
                retval = req_flush(current);
                /*
                 * retval can be -2=error, -1=blocked, or bytes left
                 */
                if (retval == -2) { /* error */
                    current->status = DEAD;
                    retval = 0;
                } else if (retval > 0) {
                    retval = 1;
                }
                break;
            case DEAD:
                retval = 0;
                current->buffer_end = 0;
                SQUASH_KA(current);
                break;
            default:
                retval = 0;
                fprintf(stderr, "Unknown status (%d), "
                        "closing!\n", current->status);
                current->status = DEAD;
                break;
            }

        }

                
        if (sigterm_flag)
            SQUASH_KA(current);

        /* we put this here instead of after the switch so that
         * if we are on the last request, and get_request is successful,
         * current->next is valid!
         */
        if (pending_requests)
        {
            get_request(server_s);
		}


        switch (retval) {
        case -1:               /* request blocked */
            trailer = current;
            current = current->next;
            block_request(trailer);
         
            break;
        case 0:                /* request complete */
            current->time_last = current_time;
            trailer = current;
            current = current->next;
            free_request(&request_ready, trailer);

            break;
        case 1:                /* more to do */
            current->time_last = current_time;
            current = current->next;
            break;
        default:
            log_error_time();
            fprintf(stderr, "Unknown retval in process.c - "
                    "Status: %d, retval: %d\n", current->status, retval);
            current = current->next;
            break;
        }
    }

}

/*
 * Name: process_logline
 *
 * Description: This is called with the first req->header_line received
 * by a request, called "logline" because it is logged to a file.
 * It is parsed to determine request type and method, then passed to
 * translate_uri for further parsing.  Also sets up CGI environment if
 * needed.
 */

int process_logline(request * req)
{
    char *stop, *stop2;
    static char *SIMPLE_HTTP_VERSION = "HTTP/0.9";
	
    #ifdef BOA_DEBUG
    struct timeval tv;
	gettimeofday(&tv,NULL);
	fprintf(stderr,"process_logline:%u,%u\n",tv.tv_usec,tv.tv_sec);
	#endif
		
    req->logline = req->client_stream;
    if (!memcmp(req->logline, "GET ", 4))
        req->method = M_GET;
    else if (!memcmp(req->logline, "HEAD ", 5))
        /* head is just get w/no body */
        req->method = M_HEAD;
    else if (!memcmp(req->logline, "POST ", 5))
        req->method = M_POST;
    else {
    	#if 0
        log_error_time();
        fprintf(stderr, "malformed request: \"%s\"\n", req->logline);
        #endif
        send_r_not_implemented(req);
        return 0;
    }

    req->http_version = SIMPLE_HTTP_VERSION;
    req->simple = 1;

    /* Guaranteed to find ' ' since we matched a method above */
    stop = req->logline + 3;
    if (*stop != ' ')
        ++stop;

    /* scan to start of non-whitespace */
    while (*(++stop) == ' ');

    stop2 = stop;

    /* scan to end of non-whitespace */
    while (*stop2 != '\0' && *stop2 != ' ')
        ++stop2;

    if (stop2 - stop > MAX_HEADER_LENGTH) {
        log_error_time();
        fprintf(stderr, "URI too long %d: \"%s\"\n", MAX_HEADER_LENGTH,
                req->logline);
        send_r_bad_request(req);
        return 0;
    }
    memcpy(req->request_uri, stop, stop2 - stop);
    req->request_uri[stop2 - stop] = '\0';

    if (*stop2 == ' ') {
        /* if found, we should get an HTTP/x.x */
        unsigned int p1, p2;

        /* scan to end of whitespace */
        ++stop2;
        while (*stop2 == ' ' && *stop2 != '\0')
            ++stop2;

        /* scan in HTTP/major.minor */
        if (sscanf(stop2, "HTTP/%u.%u", &p1, &p2) == 2) {
            /* HTTP/{0.9,1.0,1.1} */
            if (p1 == 1 && (p2 == 0 || p2 == 1)) {
                req->http_version = stop2;
                req->simple = 0;
            } else if (p1 > 1 || (p1 != 0 && p2 > 1)) {
                goto BAD_VERSION;
            }
        } else {
            goto BAD_VERSION;
        }
    }

    if (req->method == M_HEAD && req->simple) {
        send_r_bad_request(req);
        return 0;
    }
    req->cgi_env_index = COMMON_CGI_COUNT;

    return 1;

BAD_VERSION:
    log_error_time();
    fprintf(stderr, "bogus HTTP version: \"%s\"\n", stop2);
    send_r_bad_request(req);
    return 0;
}

//lee 2006-4-11
#define NIL 0
static const char *my_strpbrk (char *cs,char *ct)
{
  char *s;
				/* search for delimiter until end of string */
  for (; *cs; cs++) for (s = ct; *s; s++) if (*s == *cs) return cs;
  return NIL;			/* not found */
}

#define JNK 0177
#define PAD 0100
static void *rfc822_base64 (unsigned char *src,unsigned long srcl,unsigned long *len)
{
  char c,*s,tmp[512];
  void *ret = malloc ((size_t) (*len = 4 + ((srcl * 3) / 4)));
  char *d = (char *) ret;
  int e;
  static char decode[256] = {
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,076,JNK,JNK,JNK,077,
   064,065,066,067,070,071,072,073,074,075,JNK,JNK,JNK,PAD,JNK,JNK,
   JNK,000,001,002,003,004,005,006,007,010,011,012,013,014,015,016,
   017,020,021,022,023,024,025,026,027,030,031,JNK,JNK,JNK,JNK,JNK,
   JNK,032,033,034,035,036,037,040,041,042,043,044,045,046,047,050,
   051,052,053,054,055,056,057,060,061,062,063,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,
   JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK,JNK
  };
  memset (ret,0,(size_t) *len);	/* initialize block */
  *len = 0;			/* in case we return an error */

				/* simple-minded decode */
  for (e = 0; srcl--; ) switch (c = decode[*src++]) {
  default:			/* valid BASE64 data character */
    switch (e++) {		/* install based on quantum position */
    case 0:
      *d = c << 2;		/* byte 1: high 6 bits */
      break;
    case 1:
      *d++ |= c >> 4;		/* byte 1: low 2 bits */
      *d = c << 4;		/* byte 2: high 4 bits */
      break;
    case 2:
      *d++ |= c >> 2;		/* byte 2: low 4 bits */
      *d = c << 6;		/* byte 3: high 2 bits */
      break;
    case 3:
      *d++ |= c;		/* byte 3: low 6 bits */
      e = 0;			/* reinitialize mechanism */
      break;
    }
    break;
  case JNK:			/* junk character */
    break;
  case PAD:			/* padding */
    switch (e++) {		/* check quantum position */
    case 3:			/* one = is good enough in quantum 3 */
				/* make sure no data characters in remainder */
      for (; srcl; --srcl) switch (decode[*src++]) {
      case JNK: case PAD:	/* ignore junk and extraneous padding */
	break;
      default:			/* valid BASE64 data character */
	/* This indicates bad MIME.  One way that it can be caused is if
	   a single-section message was BASE64 encoded and then something
	   (e.g. a mailing list processor) appended text.  The problem is
	   that in 1 out of 3 cases, there is no padding and hence no way
	   to detect the end of the data.  Consequently, prudent software
	   will always encapsulate a BASE64 segment inside a MULTIPART.
	   */
	sprintf (tmp,"Possible data truncation in rfc822_base64(): %.80s",
		 (char *) src - 1);
	s = my_strpbrk (tmp,"\015\012");
	if (s) *s = NIL;
	
	srcl = 1;		/* don't issue any more messages */
	break;
      }
      break;
    case 2:			/* expect a second = in quantum 2 */
      if (srcl && (*src == '=')) break;
    default:			/* impossible quantum position */
      free (ret);
      return NIL;
    }
    break;
  }
  *len = d - (char *) ret;	/* calculate data length */
  return ret;			/* return the string */
}


typedef struct param_s {
	char	*name;
	char	*value;
} s_param;

static void free_param(s_param **var)
{
	int i;
	#ifdef TRENDCHIP
	if(var){/*var can't be free if it point to 0*/
	#endif
	for (i = 0;var[i];i++)
	{
		if(var[i]->name)
			free(var[i]->name);
		if(var[i]->value)
			free(var[i]->value);
		free(var[i]);
	}
	free(var);
	#ifdef TRENDCHIP
	}
	#endif
}
static s_param **param_line (char *line,char separate1,char separate2)
{
    int numargs;
    char *cp, *ip, *esp, *sptr;
    s_param **result = NULL;
    int i, k, len;

    if (line) {
	    if (strlen(line)) {
		for (numargs=1,cp=line; *cp; cp++)
		    if (*cp == separate2) numargs++;
	    } else
		numargs = 0;


	    len = (numargs+1) * sizeof(s_param *);
	    if ((result = (s_param **)malloc (len)) == NULL)
	    {
			return NULL;
			}
	    memset (result, 0, len);

	    cp = line;
	    i=0;
	    while (*cp) {
		if ((ip = (char *)strchr(cp, separate2)) != NULL) {
		    *ip = '\0';
		}else
		    ip = cp + strlen(cp);

		if ((esp=(char *)strchr(cp, separate1)) == NULL) {
		    cp = ++ip;
		    continue;
		}

		if (!strlen(esp)) {
		    cp = ++ip;
		    continue;
		}
		
		while (*cp == 32)	//if parameter name start with ' ', ignore it. Sam 2012/10/24
			++cp;

		if (i<numargs) {

		    /* try to find out if there's already such a variable */
		    for (k=0; k<i && (strncmp (result[k]->name,cp, esp-cp) || !(strlen (result[k]->name) == esp-cp)); k++);

		    if (k == i) {	/* No such variable yet */
			if ((result[i] = (s_param *)malloc(sizeof(s_param))) == NULL)
			{
				    return NULL;
			}
			if ((result[i]->name = (char *)malloc((esp-cp+1) * sizeof(char))) == NULL)
			 {
		    return NULL;
		  }
		  
			memset (result[i]->name, 0, esp-cp+1);
			strncpy(result[i]->name, cp, esp-cp);
			cp = ++esp;
			if ((result[i]->value = (char *)malloc((ip-esp+1) * sizeof(char))) == NULL)
			{
			    return NULL;
			 }
			  
			memset (result[i]->value, 0, ip-esp+1);
			strncpy(result[i]->value, cp, ip-esp);
			//FIXME result[i]->value = cgiDecodeString(result[i]->value);
			i++;
		    } else {	/* There is already such a name, suppose a mutiple field */
			cp = ++esp;
			len = (strlen(result[k]->value)+(ip-esp)+2) * sizeof (char);
			if ((sptr = (char *)malloc(len)) == NULL)
			{
			    return NULL;
			  }
			  
			memset (sptr, 0, len);
			sprintf (sptr, "%s\n", result[k]->value);
			strncat(sptr, cp, ip-esp);
			free(result[k]->value);
		//FIXMEresult[k]->value = cgiDecodeString (sptr);
		result[k]->value = sptr;
	    }
	}
	cp = ++ip;
    }
  }

  return result;
}

int check_validate(char *usr,char*pwd,request *req,char *path)
{
	int level = 1;
	char str[256];
	struct passwd *passwdbuf;
	FILE *fd;
	char *pwdencode;
	
	passwdbuf = getrealpass(usr);
	if (passwdbuf == NULL)
	{
		#ifdef BOA_DEBUG
		fprintf(stderr,"check_validate err.\n");
		#endif
		return -1;
	}
	else
	{
		pwdencode = crypt(pwd, passwdbuf->pw_passwd);
		if (strcmp(pwdencode, passwdbuf->pw_passwd) == 0) 
  {
  	#ifdef BOA_DEBUG
  fprintf(stderr,"pw:%s\n",pwdencode);	
  #endif
  sprintf(str,"%d",(int)passwdbuf->pw_uid);
  strcpy(req->uid,str);
  sprintf(str,"%d",(int)passwdbuf->pw_gid);
  strcpy(req->gid,str);
  #ifdef BOA_DEBUG
  fprintf(stderr,"u:%s,g:%s\n",req->uid,req->gid);
  #endif


	fd = fopen(path,"r+");
	if(fd ==NULL)
	{
		#ifdef BOA_DEBUG
		fprintf(stderr,"check_validate open %s err\n",path);
		#endif
		return -1;
	}
		
	sprintf(str,"usr=%s\npwd=%s\nuid=%s\ngid=%s\nlevel=%d\nip=%s",usr,pwdencode,req->uid,req->gid,level,req->remote_ip_addr);
	//tcdbg_printf("usr=%s\npwd=%s\nuid=%s\ngid=%s\nlevel=%d\nremote_ip_addr=%s\n",usr,pwdencode,req->uid,req->gid,level,req->remote_ip_addr);

	fwrite(str,1,strlen(str),fd);
	fclose(fd);
	
			
	strcpy(req->usr_name,usr);
	strcpy(req->pwd,pwdencode);
	sprintf(str,"%d",level);
	strcpy(req->access_level,str); 
	#ifdef BOA_DEBUG
	fprintf(stderr,"authorize:%s,%s,%s\n",req->usr_name,req->pwd,req->access_level);
	#endif
  return 1;
		}
		else
		{
		  	#ifdef BOA_DEBUG
  				fprintf(stderr,"check_validate pwd err\n");
  			#endif
  			return -1;
		}
	}
	
}
		
static inline struct passwd *getrealpass(const char *user) 
{	
	struct passwd *pwp;
	pwp = getpwnam(user);
	if (pwp == NULL)
		return NULL;
	return pwp;
}

int check_session(char *path,char*ip,request *req)
{
	FILE *fd;
	char buf[256];
	s_param **sub;
	
	fd = fopen(path,"r+");
	if(fd == NULL)
	{
		#ifdef BOA_DEBUG
		fprintf(stderr,"check_session open %s err\n",path);
		#endif
		return -1;
	}
	
	while(fgets(buf,256,fd))
	{
		char *ret;
		ret = strchr(buf,'\r');
		if(ret)
			ret[0] = '\0';
		ret = strchr(buf,'\n');
		if(ret)
			ret[0] ='\0';
				
		sub = param_line(buf,'=',';');
		if(!strcmp("usr", sub[0]->name))
			sprintf(cur_username, "%s", sub[0]->value);
		#ifdef BOA_DEBUG
		fprintf (stderr,"check_session:%s=%s\n", sub[0]->name, sub[0]->value);
		#endif
		if(!strcmp("usr",sub[0]->name))
			strcpy(req->usr_name,sub[0]->value);
		if(!strcmp("pwd",sub[0]->name))
			strcpy(req->pwd ,sub[0]->value);
		if(!strcmp("level",sub[0]->name))
			strcpy(req->access_level,sub[0]->value);
		if(!strcmp("uid",sub[0]->name))
			strcpy(req->uid,sub[0]->value);
		if(!strcmp("gid",sub[0]->name))
			strcpy(req->gid,sub[0]->value);
			
		free_param(sub);		
	}
	fclose(fd);
	#ifdef BOA_DEBUG
	fprintf(stderr,"check_session:u:%s,p:%s,l:%s,gid:%s,uid:%s\n",req->usr_name,req->pwd,req->access_level,req->gid,req->uid);
	#endif
	return atoi(req->access_level);
		
}

#if !defined(TCSUPPORT_SELF_LOGIN_WINDOW)
int AllPassUrl(char *url)
{
	if(	strstr(url, ".js") ||
		strstr(url, ".gif") ||
		strstr(url, ".png") ||
		strstr(url, ".jpg") ||
		strstr(url, ".css") ||
		strstr(url, ".ico") ||
		strstr(url, "error_page.asp") ||
		strstr(url, "update_applist.asp")
		//strstr(url, "index.asp")
	)
		return 1;	//pass
	else
		return 0;
}

int FirstPassUrl(char *url)
{
	if( !tcapi_get_int("SysInfo_Entry", "x_Setting" ) ) {	//Reset Default
		if(	strstr(url, "QIS" )
		)
			return 1;	//pass
	}
	return 0;
}

int passURL(char * url)
{
	if( AllPassUrl(url) )
		return 0;
	//if( FirstPassUrl(url) )
		//return 0;
	return -1;
}


int http_authorize(request *req)
{
	s_param **var,**auth;
  	int i;
  	unsigned long len;
	struct stat buf;
	int ret=0;
	char path[256];
	int level=-1;
	FILE *fd;

	req->session = 0; //Ren.0001: For DSL-N66U, Bug#135

	//if cookie is null return
	if(req->cookie ==NULL)
	{
		#ifdef BOA_DEBUG
		fprintf(stderr,"cookie null\n");
		#endif
		return -1;
	}
	#ifdef BOA_DEBUG 
	fprintf(stderr,"cookie:%s\n",req->cookie);
	#endif

	//get sessionid in cookie
	var = param_line (req->cookie, '=',';');
	for(i = 0;var[i];i++)
	{	
		#ifdef BOA_DEBUG
		fprintf (stderr,"cookie param:%s=%s\t index:%d\n", var[i]->name, var[i]->value,i);
		if(var[i+1] != NULL)
			fprintf (stderr,"cookie param:%s=%s\t index:%d\n", var[i+1]->name, var[i+1]->value,i+1);
		#endif
		
		#ifdef TRENDCHIP /*add "mstnc" is for ie8.0.7601.17514 version*/
		if(!strcmp(var[i]->name,"SESSIONID") || !strcmp(var[i]->name,"mstnc"))
		#else
		if(!strcmp(var[i]->name,"SESSIONID"))
		#endif
		{
			sprintf(path,"/var/tmp/%s",var[i]->value);
			ret = stat(path,&buf);
			if(ret)//can't find session file
			{
				//create session file at send_r_unauthorized(), Sam 2014/3/11
				//fd = fopen(path,"w");
				//fclose(fd);
				//#ifdef BOA_DEBUG
				//fprintf(stderr,"session file not found,create_0:%s\n",path);
				//#endif
				free_param(var);
				return -1;
			}
			
			req->session = 1; //Ren.0001: found session file in /var/tmp/

			if (buf.st_size ==0)
			{
				#ifdef BOA_DEBUG
				fprintf(stderr,"buf.st_size = 0\n");
				#endif		
				if(req->authorize == NULL)
				{
					#ifdef BOA_DEBUG
					fprintf(stderr,"authorize = NULL and free param now\n");
					#endif
					free_param(var);
					return -1;	
				}
				//check authorize info
				req->authorize = req->authorize+6;
				req->authorize = rfc822_base64(req->authorize,strlen(req->authorize),&len);
				#ifdef BOA_DEBUG
				fprintf(stderr,"base64 decode:%s\n",req->authorize);
				#endif
				auth = param_line (req->authorize, ':', ';');
				sprintf(cur_username, "%s", auth[0]->name);
				#ifdef BOA_DEBUG
				fprintf (stderr,"auth:%s=%s\n", auth[0]->name, auth[0]->value);
				#endif
				level = check_validate(auth[0]->name,auth[0]->value,req,path);
				if (level== -1)
				{
					#ifdef BOA_DEBUG
					fprintf(stderr,"check_validate  %d err\n",level);
					#endif
					free_param(auth);
					free_param(var);
#ifdef TCSUPPORT_SYSLOG_ENHANCE
					openlog("TCSysLog WEB", 0, LOG_LOCAL2);
					syslog(LOG_INFO, "WEB login failed!\n");
					closelog();
#endif
					return -1;
				}
#ifdef TCSUPPORT_SYSLOG_ENHANCE
				else{
					char log[128];
					snprintf(log, sizeof(log), "WEB user <%s> login\n", auth[0]->name);
					openlog("TCSysLog WEB", 0, LOG_LOCAL2);
					syslog(LOG_INFO, log);
					closelog();
				}
#endif				


			}
			else
			{		
					level = check_session(path,req->remote_ip_addr,req);
					if (level== -1)
					{
						#ifdef BOA_DEBUG
						fprintf(stderr,"check_session  %d err\n",level);
						#endif
						free_param(var);
						unlink(path);
						return -1;
					}
			}

			if(strstr(req->request_uri, "Logout"))
				unlink(path);

			break;
			
		} 		
		
	}		
	free_param(var);
	
	return level;
}
#endif



#ifndef PURE_BRIDGE
#if !defined(TCSUPPORT_SELF_LOGIN_WINDOW) 
/*______________________________________________________________________________
**      pageAccessCheck
**
**      descriptions:
**              check the access of the page
**      parameters:
**
**      return:
**              Success:        0
**              FAIL: -1
**_______________________________________________________________________________
*/
static int pageAccessCheck(request * req)
{
        int ret = 0, i = 0 ,  j = 0 , break_flag = 0, maskBytes[9];
        char nodename[15], username[129], maskBuf[30];
        char bitMask[8] = {(1<<0), (1<<1), (1<<2), (1<<3), (1<<4), (1<<5), (1<<6), (1<<7)};
        struct pageset *pagesetNow = NULL , *pagesetNext = NULL;
        extern char cur_username[129];
        extern struct pageset* pageMap[PAGEMAP_I_NUM][PAGEMAP_J_NUM];
        
        memset(maskBytes , 0 , sizeof(maskBytes));
        memset(nodename , 0 , sizeof(nodename));
        memset(username , 0 , sizeof(username));
        memset(maskBuf , 0 , sizeof(maskBuf));
        
        for(i = 0 ; i < 1; i++){
                sprintf(nodename, "Account_Entry%d", i);
                ret = tcapi_get(nodename, "username", username);
                if(ret < 0){ //Fail to acquire username from romfile
                        fprintf(stderr, "Fail to acquire username from romfile: ret = %d\r\n", ret);
                        return -1;
                }

                if(!strcmp(cur_username, username)){
                        ret = tcapi_get(nodename, "display_mask", maskBuf);
                        if(ret < 0){ //Fail to set current username
                                fprintf(stderr, "Fail to get current username's dispaly mask: ret = %d\r\n", ret);
                                return -1;
                        }
                        break;
                }
        }
        sscanf(maskBuf, "%X %X %X %X %X %X %X %X %X", &maskBytes[0], &maskBytes[1], &maskBytes[2], &maskBytes[3], &maskBytes[4], &maskBytes[5], &maskBytes[6], &maskBytes[7], &maskBytes[8]);

        for(i = 0; i < PAGEMAP_I_NUM; i++){
                for(j = 0; j < PAGEMAP_J_NUM; j++){
                        if(pageMap[i][j]->page_name == NULL)
                                continue;
                        if(strstr(req->request_uri, pageMap[i][j]->page_name)){
                                fprintf(stderr, "Page Found\r\n");
                                break_flag = 1;
                                break;
                        }
                        pagesetNow = pageMap[i][j];
                        pagesetNext = pageMap[i][j]->next;
                        while(pagesetNext){
                                pagesetNow = pagesetNext;
                                pagesetNext = pagesetNow->next;
                                if(strstr(req->request_uri, pagesetNow->page_name)){
                                        fprintf(stderr, "Page Found \r\n");
                                        break_flag = 1;
                                        break;
                                }
                        }
                        if(break_flag){
                                fprintf(stderr, "Break inner loop\r\n");
                                break_flag = 1;
                                break;
                        }
                }
                if(break_flag){
                        fprintf(stderr, "Break outer loop\r\n");
                        break_flag = 0;
                        break;
                }
        }
        if(i < PAGEMAP_I_NUM && j < PAGEMAP_J_NUM && strcmp(req->request_uri, "/") && strcmp(req->request_uri, "/cgi-bin/index.asp")){
#if !defined(TCSUPPORT_CD_NEW_GUI) 
                if(!(maskBytes[0] & bitMask[i])){
                        fprintf(stderr, "navigation-xxx.asp is blocked\r\n");
                        return -1;
                }
                if(j >= 1 && !(maskBytes[i+1] & bitMask[j-1])){
                        fprintf(stderr, "This page is blocked\r\n");
                        return -1;
                }        

#endif
        }
        return 0;       
} 
#endif
#endif


/*
 * Name: uploadDownLoadUrl
 *
 * Description: check if the url is upload page or not
 * Returns 1 for found , 0 for not found
 */
int uploadDownLoadUrl(request * req)
{
        if(0 == strcmp(req->pathname,"/boaroot/cgi-bin/tools_update.asp") 
        )
        {
                return 1;
        }
        
        return 0;
}

/*
 * Name: releaseMemory
 *
 * Description: release memory to upload the romfile or tclinux.bin 
 */
void releaseMemory()
{
        int i = 0;
        char string[32] = "";
        
        memset(string , 0 , sizeof(string));
        for(i=0;i<4;i++)
        {
                sprintf(string, "/usr/script/wan_stop.sh %d", i);
                system(string);
        }
        system("killall -9 utelnetd");
        sleep(1);
        system("rmmod tc3162l2sar.ko");
        system("sys memwl b4000040 ffffffff");
        sleep(1);
        system("rmmod tc3162_dmt.ko");
        return;
}

#if 0
#ifdef TCSUPPORT_FW_UPGRADE_16M
#if !defined(TCSUPPORT_CD_NEW_GUI) 
void releaseMemory2()
#endif
{
#if !defined(TCSUPPORT_CD_NEW_GUI) 
	tcapi_set("System_Entry", "upgrade_status", "true");
	system("sh /usr/script/before_web_download.sh 1");
#endif
	sleep(1);	
	sync();
}
#endif
#endif

#ifdef TCSUPPORT_FW_UPGRADE_16M
void releaseMemory2(request *req)
{
	unsigned int mark;
	int marklen;

	tcapi_set("System_Entry", "upgrade_status", "true");

	marklen = sizeof(mark);
	if (getsockopt(req->fd, SOL_IP, IP_SKB_MARK, &mark, &marklen) == 0)
	{
		mark = (mark & 0xf0000000) >> 28;
	#if 1
		tcdbg_printf("makr = %d\n", mark);
	#endif
		if((SSID1_PORT_MASK == mark)||(SSID2_PORT_MASK == mark)||(SSID3_PORT_MASK == mark)||(SSID4_PORT_MASK == mark))
		{
			tcdbg_printf("\r\nRelease memory: WIFI upgrade!!\r\n");		
#if !defined(TCSUPPORT_CD_NEW_GUI) 
			system("sh /usr/script/before_web_download.sh 1");
#endif
		}
		else
		{
			tcdbg_printf("\r\nRelease memory: Not WIFI upgrade!!\r\n");

#if defined(TCSUPPORT_WLAN_RT6856)
			// this is for old board.
			/* Paul comment 2013/7/3, new hardware could reset iNIC while system boot */
			//tcdbg_printf("\r\nGoing to bring down WiFi!!\r\n");
			//system("ifconfig ra00_0 down");
			//system("ifconfig ra01_0 down");
#endif

#if !defined(TCSUPPORT_CD_NEW_GUI) 
			system("sh /usr/script/before_web_download_remove_wifi.sh 1");
#endif
		}
	}
	else
	{
		tcdbg_printf("\r\nRelease memory: Unknown upgrade!!\r\n");
		system("/usr/script/before_firmware_update.sh");
	}

	sleep(1);	
	sync();
}
#endif

/*
 * Name: savingFileForVoipRestart
 *
 * Description: saving flag to /etc/restart_voip file
 */
void savingFileForVoipRestart()
{
        FILE *fp = NULL;
        fp = fopen("/etc/restart_voip","w");
        if(fp != NULL)
        {
                fputc('s',fp);
                fclose(fp);
        }
        sleep(3);
        return;
}

#ifdef INET6
int check_ip_type(char* ip) {
	struct sockaddr_in cvtaddr;
	struct sockaddr_in6 ipv6addr;

	if(inet_aton(ip,&cvtaddr.sin_addr)) {
		//ip address is ipv4
              	//fprintf(stderr,"\n==>check_ip_type, IPv4\n");
		return IP_TYPE_V4;
	}
	else if( inet_pton(AF_INET6, ip,(struct sockaddr *) &ipv6addr.sin6_addr) >0) {	
		//ip address is ipv6
              	//fprintf(stderr,"\n==>check_ip_type, IPv6\n");
		return IP_TYPE_V6;
	}

	return -1;
}

#endif

// all user can access these pages, not only the login one.
int pass_login_check(char* url)
{
	if( strstr(url, "error_page.asp") ||
	strstr(url , ".js") ||
	strstr(url , ".gif") ||
	strstr(url , ".png") ||
	strstr(url , ".css"))
		return 1;
        else
		return 0;
}

//Ren.B
int handleRomfile( char *productName )
{
	char noEncrypt[32] = {0};
	char cmd[128] = {0};
	
	int ret = 0;
	
	ret = tcapi_get("WebCurSet_Entry", "NE", noEncrypt); //NE means NoEncrypt
	if( ( ret >= 0 ) && (!strcmp(noEncrypt, "1")) )
	{
		sprintf(cmd, "cp /tmp/var/romfile.cfg /tmp/var/romfile_encrypt.cfg" );
		system(cmd);
		return 0;
	}
	else
	{
		if((!productName)||( strlen(productName) <= 0 ))
		{
			encryptRomfile("/tmp/var/romfile.cfg", "/tmp/var/romfile_encrypt.cfg", "DSL-N66U");
		}
		else
		{
			encryptRomfile("/tmp/var/romfile.cfg", "/tmp/var/romfile_encrypt.cfg", productName);
		}
	}

	return 0;
}
//Ren.E

//javi
void
replace_seconddash(char *string){
	char *pt;
	char *pt2;
	pt=strstr(string, "-");
	if(pt==NULL){
		return;
	}

	pt2= strstr(pt+1, "-");
	if(pt2){
		*pt2='_';
	}
	return;
}

static int is_cgibin_exception(char *request)
{
	int i = 0;
	char *filename;
	
	if (request == NULL)
		return 0;

	filename = rindex(request, '/')+1;
	for (i=0; i< CGIBIN_EXCEPT_FILE_LIST_SIZE; i++)
	{
		if (!strcmp(filename, cgibin_exception_file_list[i].filename))
			return 1;
	}

	return 0;
}

void http_update_allowed_client(void) 
{
	char getbuf[128];
	char *nvp, *b;
	int i=0;

	memset(&restrict_client, 0, sizeof(restrict_client));
	memset(getbuf, 0, sizeof(getbuf));
	if (tcapi_get("SysInfo_Entry", "http_restrict_client", getbuf) == 0)
	{
		if (atoi(getbuf) == 1)
			restrict_client.enabled = 1;
		else
			restrict_client.enabled = 0;
	}

	memset(getbuf, 0, sizeof(getbuf));
	if (tcapi_get("SysInfo_Entry", "http_clientlist", getbuf) == 0)
	{
		nvp = getbuf;
		while ((b = strsep(&nvp, ":")) != NULL) {
			if (strlen(b)==0) continue;
			if (i >= MAX_CLIENT) {
				break;
			}

			sprintf(restrict_client.allowed_client_list[i].client_ip_str, "%s", b);
			restrict_client.allowed_client_list[i].client_ip = 
				inet_addr(restrict_client.allowed_client_list[i].client_ip_str);
			i++;
		}
	}
	restrict_client.client_cnt = i;
	cprintf("%s-restrict_client(%d): (%d)clients\n", __FUNCTION__, restrict_client.enabled, restrict_client.client_cnt);
	for (i=0; i<restrict_client.client_cnt; i++)
	{
		if(strlen(restrict_client.allowed_client_list[i].client_ip_str) != 0)
		{
			cprintf("-ip%d(%s)(0x%x)\n", i, restrict_client.allowed_client_list[i].client_ip_str,
				restrict_client.allowed_client_list[i].client_ip);
		}
	}
}

static int http_client_ip_check(request * req) 
{
	int i = 0;
	in_addr_t remote_ip;
	restrict_client_t *restrict_client = NULL;

	if (req == NULL)
		return 1;

	if (req->restrict_client == NULL)
		return 1;

	restrict_client = req->restrict_client;
	if (restrict_client->enabled) 
	{
		remote_ip = inet_addr(req->remote_ip_addr);
		for (i=0; i<restrict_client->client_cnt; i++)
		{
			if(remote_ip == restrict_client->allowed_client_list[i].client_ip)
			{
				return 1;
			}
		}
		return 0;
	}

	return 1;
}


#define APPLYAPPSTR 	"applyapp.cgi"
#define GETAPPSTR	"getapp"
#define GETWEBDAVINFO	"get_webdavInfo.asp"
/*
 * Name: process_header_end
 *
 * Description: takes a request and performs some final checking before
 * init_cgi or init_get
 * Returns 0 for error or NPH, or 1 for success
 */

int process_header_end(request * req)
{
		char hostUrl[64] = "";
		char voIPBusy[] = "0";
		char forbidden[] = "0";
#ifdef TCSUPPORT_MODEL_NAME_CUSKIT
		char productName[64] = {0};
		char strNodeName[32] = {0};
#endif
		char cfgFileName[128] = {0}; //Ren
		int ret = 0;

	//process for app
	char tmpuri[MAX_HEADER_LENGTH + 1] = {0};
	char *p = 0;
	int fromapp=0;
	if(p = strstr(req->request_uri, GETAPPSTR)) {
		//skip GETAPPSTR
		strcpy(tmpuri, p + strlen(GETAPPSTR));
		strcpy(p, tmpuri);
		fromapp=1;
	}
	else if(strstr(req->request_uri, APPLYAPPSTR)) {
		//change .cgi to .asp
		p = strstr(req->request_uri, ".cgi");
		strncpy(p, ".asp", 4);
		fromapp=1;
	}
	else if(strstr(req->request_uri, GETWEBDAVINFO)) {
		fromapp=1;
	}

	/* Check if request come from allowed client list */
	if (http_client_ip_check(req) == 0)
	{
		send_r_forbidden(req);
		return 0;
	}

		/* Paul add start 2013/1/3, only allow single user to be logged in, and unlock if not accessed again in 60 seconds. */
		char *uptime = file2str("/proc/uptime");
		char cur_temp_ip[32];
		memset(cur_temp_ip, 0, 32);
		ret = tcapi_get("WebCurSet_Entry", "login_ip_tmp", cur_temp_ip);

		if(ret < 0 || (!strcmp(cur_temp_ip, "none")) || fromapp){ //No one logged in. Ignore if request from app
			// tcapi_set("WebCurSet_Entry", "login_ip_tmp", req->remote_ip_addr);
			// if(uptime)
				// tcapi_set("WebCurSet_Entry", "login_uptime", uptime);
			;//update after authorization, Sam, 2013/1/15
		}
		else //Someone logged in
		{
			if(cur_temp_ip != "" && strcmp(cur_temp_ip, req->remote_ip_addr)){ //Different IP, check whether access is allowed
				if(!pass_login_check(req->request_uri)) {	//all can access, not only the login one
					char cur_temp_uptime[32];
					memset(cur_temp_uptime, 0, 32);
					ret = tcapi_get("WebCurSet_Entry", "login_uptime", cur_temp_uptime);

					if(!(ret < 0) && uptime && (atoi(uptime) - atoi(cur_temp_uptime)) < 60)
					{
						send_r_cannot_login(req); //return a 400 bad request
						free(uptime);
						return 0;
					}
					else //over 60 seconds, access granted
					{
						// tcapi_set("WebCurSet_Entry", "login_ip_tmp", req->remote_ip_addr);
						// if(uptime)
							// tcapi_set("WebCurSet_Entry", "login_uptime", uptime);
						;//update after authorization, Sam, 2013/1/15
					}
				}
			}
			else
			{
				if(uptime)
					tcapi_set("WebCurSet_Entry", "login_uptime", uptime);
			}
		}
		free(uptime);
		/* Paul add end */

	   int VoIPBusyFlag = 0; /* for ForbiddenWeb in VoIP */
        if (!req->logline) {
                send_r_error(req);
                return 0;
        }

	//redirect the requests with incorrect path.
	//process before unescape_uri, or query_string will miss. Sam 2013/7/3
	if(strncmp(req->request_uri, "/cgi-bin", 8)) {
		if(strstr(req->request_uri, ".cgi") || strstr(req->request_uri, ".asp")
			|| is_cgibin_exception(req->request_uri)) {
			memset(tmpuri, 0, sizeof(tmpuri));
			strcpy(tmpuri, req->request_uri);
			sprintf(req->request_uri, "/cgi-bin%s", tmpuri);
		}
	}

        /* Percent-decode request */
        if (unescape_uri(req->request_uri, &(req->query_string)) == 0) {
                log_error_doc(req);
                fputs("Problem unescaping uri\n", stderr);
                send_r_bad_request(req);
                return 0;
        }

        /* clean pathname */
        clean_pathname(req->request_uri);
        
        if (req->request_uri[0] != '/') {
                send_r_bad_request(req);
                return 0;
        }
		
	
        if (translate_uri(req) == 0) { /* unescape, parse uri */
                SQUASH_KA(req);
                return 0;               /* failure, close down */
        }
        
         /* get method function */
        if(req->method == M_GET)
        {
        	//Ren.B
        	if( strstr(req->request_uri, "Settings_") )
        	{
        	sprintf(strNodeName,"%s_%s",SYS_INFO_NODE_NAME,SUB_NODE_NAME);
		ret = tcapi_get(strNodeName, SYS_INFO_PRODUCT_NAME, productName);
		if(ret < 0)
		{
			fputs("[request.c][process_header_end()]Cannot get product name!\n", stderr);
		}
		else {
			sprintf(cfgFileName, "Settings_%s.cfg", productName ); //cfgFileName will like as "Settings_DSL-N66U.cfg"
			if( strstr(req->request_uri, cfgFileName) )
			{
				handleRomfile(productName);
			}
		}
        	}
		//Ren.E
			
                /* user login need add below this line */
                /* -------------------------------------------- */
#if !defined(TCSUPPORT_SELF_LOGIN_WINDOW) 
#if !defined(TCSUPPORT_CD_NEW_GUI) 
                 /* main trunk authorize function */
                //lee 2006-4-11:authorize
                if ( (0 != passURL(req->request_uri)) && (-1 == http_authorize(req)) )
                {
                        #ifdef BOA_DEBUG
                        fprintf(stderr,"send_r_unauthorized\n");
                        #endif
#ifdef TCSUPPORT_MODEL_NAME_CUSKIT
                       	sprintf(strNodeName,"%s_%s",SYS_INFO_NODE_NAME,SUB_NODE_NAME);
			ret = tcapi_get(strNodeName, SYS_INFO_PRODUCT_NAME, productName);
			replace_seconddash(productName);
			if(ret < 0)
			{
			       	send_r_unauthorized(req,"Member Login");
			}
			else {
				send_r_unauthorized(req,productName);
			}
#else
			send_r_unauthorized(req,"Member Login");
#endif
                        return 0;
                }
#endif
#endif
		if(!pass_login_check(req->request_uri) && !fromapp) {	//all can access, not only the login one
			// Sam, 2013/1/15, update time, ip, ...etc. after authrization.
			tcapi_set("WebCurSet_Entry", "login_ip_tmp", req->remote_ip_addr);
			uptime = file2str("/proc/uptime");
			if(uptime)
				tcapi_set("WebCurSet_Entry", "login_uptime", uptime);
			free(uptime);
		}
        }

#ifndef PURE_BRIDGE
#if !defined(TCSUPPORT_SELF_LOGIN_WINDOW) 
        if(0 != pageAccessCheck(req))
#endif
        {
                send_r_forbidden(req);
                // the page is blocked
                return 0;
        }
#endif

         /* post method function */
        if (req->method == M_POST) {
#ifdef TRENDCHIP
/*20110217 PTChen add for Forbidden Web functionality of VoIP */
/*if it match ForbiddenWeb & VoIPBusy all equal to 1, represent VoIP system is busy now. return a new web page to inform user VoIP is busy, and pass through following work*/
		if(!tcapi_get("VoIPBasic_Common","ForbiddenWeb",forbidden) && !tcapi_get("VoIPBasic_Common","VoIPBusy",voIPBusy)){
			if((0 == strcmp(voIPBusy, "1" )) && (0 == strcmp(forbidden, "1" ))){
				strcpy(req->pathname, "/boaroot/cgi-bin/voip_busy.asp");	
				VoIPBusyFlag = 1; /* if 1 pass through following work */
			}			
		}
/*20110217 PTChen add for Forbidden Web functionality of VoIP End*/




                // when upload the file to cpe
                if((1 == uploadDownLoadUrl(req)) && (VoIPBusyFlag != 1))
                {
#ifdef PURE_BRIDGE
                        releaseMemory();
#else
#ifdef TCSUPPORT_FW_UPGRADE_16M
#if 0
#if !defined(TCSUPPORT_CD_NEW_GUI)
			releaseMemory2();
#endif
#endif
			releaseMemory2(req);
#endif
#endif
#ifdef WITHVOIP
                        savingFileForVoipRestart();
#endif
                }               
#endif       
		req->post_data_fd = create_temporary_file(1, NULL, 0);
        if (req->post_data_fd == 0)
            return(0);
        return(1); /* success */
    }

    if (req->is_cgi) {
    	
        return init_cgi(req);
    }

    req->status = WRITE;
    return init_get(req);       /* get and head */
}

/*
 * Name: process_option_line
 *
 * Description: Parses the contents of req->header_line and takes
 * appropriate action.
 */

int process_option_line(request * req)
{
    char c, *value, *line = req->header_line;

    /* Start by aggressively hacking the in-place copy of the header line */

#ifdef FASCIST_LOGGING
    log_error_time();
    fprintf(stderr, "%s:%d - Parsing \"%s\"\n", __FILE__, __LINE__, line);
#endif
		
    value = strchr(line, ':');
    if (value == NULL)
        return 0;
    *value++ = '\0';            /* overwrite the : */
    to_upper(line);             /* header types are case-insensitive */
    while ((c = *value) && (c == ' ' || c == '\t'))
        value++;

    if (!memcmp(line, "IF_MODIFIED_SINCE", 18) && !req->if_modified_since)
        req->if_modified_since = value;

    else if (!memcmp(line, "CONTENT_TYPE", 13) && !req->content_type)
        req->content_type = value;

    else if (!memcmp(line, "CONTENT_LENGTH", 15) && !req->content_length)
        req->content_length = value;

    else if (!memcmp(line, "CONNECTION", 11) &&
             ka_max && req->keepalive != KA_STOPPED) {
        req->keepalive = (!strncasecmp(value, "Keep-Alive", 10) ?
                          KA_ACTIVE : KA_STOPPED);
    }
    /* #ifdef ACCEPT_ON */
    else if (!memcmp(line, "ACCEPT", 7))
        add_accept_header(req, value);
    /* #endif */

    /* Need agent and referer for logs */
    else if (!memcmp(line, "REFERER", 8)) {
        req->header_referer = value;
        if (!add_cgi_env(req, "REFERER", value, 1))
            return 0;
    } else if (!memcmp(line, "USER_AGENT", 11)) {
        req->header_user_agent = value;
        if (!add_cgi_env(req, "USER_AGENT", value, 1))
            return 0;
    }
    #if 0
    else if (!memcmp(line, "COOKIE", 6))//lee 2006-4-11
    {
    #endif
	/*
		krammer change for bug 1473.
		we use the SESSIONID in cookie to do the authentication.
		But opera brower will send cookie and cookie2 to our cpe,
		and the cookie2 will cover the SESSIONID. So opera can't 
		login our CPE because wrong SESSIONID.
		I do the following change to make cookie2 can't be stored into our CPE
	*/
    else if (!memcmp(line, "COOKIE", 6)&&memcmp(line, "COOKIE2", 7))//krammer 2008-12-4
    {
    		//fprintf(stderr,"line:%s\n",line);
    	int len = strlen(value)+1;
    	req->cookie = malloc(sizeof(char)*len);
    	memset(req->cookie,0,len);
    	memcpy(req->cookie,value,len-1);
    	if (!add_cgi_env(req, line, value, 1))
    		return 0;
    }
    else if (!memcmp(line, "AUTHORIZATION", 13))
    {
    	int len = strlen(value)+1;
    	req->authorize = malloc(sizeof(char)*len);
    	memset(req->authorize,0,len);
    	memcpy(req->authorize,value,len-1);
    	if (!add_cgi_env(req, line, value, 1))
    		return 0;
    }
    else {
        if (!add_cgi_env(req, line, value, 1))
            return 0;
    }
    return 1;
}

/*
 * Name: add_accept_header
 * Description: Adds a mime_type to a requests accept char buffer
 *   silently ignore any that don't fit -
 *   shouldn't happen because of relative buffer sizes
 */

void add_accept_header(request * req, char *mime_type)
{
#ifdef ACCEPT_ON
    int l = strlen(req->accept);
    int l2 = strlen(mime_type);

    if ((l + l2 + 2) >= MAX_HEADER_LENGTH)
        return;

    if (req->accept[0] == '\0')
        strcpy(req->accept, mime_type);
    else {
        req->accept[l] = ',';
        req->accept[l + 1] = ' ';
        memcpy(req->accept + l + 2, mime_type, l2 + 1);
        /* the +1 is for the '\0' */
        /*
           sprintf(req->accept + l, ", %s", mime_type);
         */
    }
#endif
}

void free_requests(void)
{
    request *ptr, *next;

    ptr = request_free;
    while (ptr != NULL) {
        next = ptr->next;
        free(ptr);
        ptr = next;
    }
    request_free = NULL;
}


#endif
