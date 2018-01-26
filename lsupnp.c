/*
 *  lsupnp.c
 *  Simple command-line program to discover all UPnP devices on a network.
 *
 *  Copyright (c) 2012, Chris Coffey <kpuc@sdf.org>
 *
 *  Permission to use, copy, modify, and/or distribute this software
 *  for any purpose with or without fee is hereby granted, provided
 *  that the above copyright notice and this permission notice appear
 *  in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL 
 *  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *  AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
 *  OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *  PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE     /* for strcasestr() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <errno.h>

#include "utils.h"

#define SSDP_MULTICAST_ADDRESS      "239.255.255.250"
#define SSDP_MULTICAST_PORT         1900

#define MAX_BUFFER_LEN              8192

static const char ssdp_discover_string[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 3\r\n"
    "ST: ssdp:all\r\n"
    "\r\n";

/* Globals - command-line option defaults */
int opt_source_port = 0;
int opt_verbose = false;
int opt_rdns_lookup = false;
int opt_timeout = 5;

/* Functions */
int discover_hosts(struct str_vector *vector);
int send_ssdp_request(int sock);
int get_ssdp_responses(int sock, struct str_vector *vector);
int rdns_lookup(char *ip_addr, char *hostname, int hostname_size);
int parse_cmd_opts(int argc, char *argv[]);


/*
 * *******************************************************************
 * Function: main()
 * Purpose: Discover all UPnP devices on a network and print them out.
 * Returns: 0 on success, non-zero if error occurred.
 * *******************************************************************
 */
int main(int argc, char *argv[])
{
    int ret = 0;
    struct str_vector my_vector;
    
    parse_cmd_opts(argc, argv);

    str_vector_init(&my_vector);

    ret = discover_hosts(&my_vector);

    str_vector_free(&my_vector);

    return(ret);
}

/*
 * *******************************************************************
 * Function: discover_hosts()
 * Purpose: Open a socket, send the SSDP request, and listen for any 
 *          responses back.
 * Returns: 0 on success, non-zero if error occurred.
 * *******************************************************************
 */
int discover_hosts(struct str_vector *vector)
{
    int ret = 0, sock;
    struct sockaddr_in src_sock;
    
    /* Get a socket */
    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) 
    {
        ret = errno;
        perror("socket()");
        return(ret);
    }

    /* Bind client-side (source) port to socket */
    memset(&src_sock, 0, sizeof(src_sock));
    src_sock.sin_family = AF_INET;
    src_sock.sin_addr.s_addr = htonl(INADDR_ANY);
    src_sock.sin_port = htons(opt_source_port);

    if ((ret = bind(sock, (struct sockaddr *)&src_sock, 
                            sizeof(src_sock))) != 0 ) 
    {
        ret = errno;
        perror("bind()");
        goto close_socket;
    }
    else if ( (opt_verbose == true) && (opt_source_port != 0) )
        printf("[Client bound to port %d]\n\n", opt_source_port);

    /* Send SSDP request */
    if ((ret = send_ssdp_request(sock)) != 0)
    {
        goto close_socket;
    }

    /* Get SSDP responses */
    if ((ret = get_ssdp_responses(sock, vector)) != 0)
    {
        goto close_socket;
    }

close_socket:
    if ( close(sock) != 0 )
    {
        ret = errno;
        perror("close()");
    }

    return(ret);
}

/*
 * *******************************************************************
 * Function: send_ssdp_request()
 * Purpose: Send the SSDP request string.
 * Returns: 0 on success, non-zero if error occurred.
 * *******************************************************************
 */
int send_ssdp_request(int sock)
{
    int ret = 0, len, bytes_out;
    struct sockaddr_in dest_sock;

    /* Prepare destination info */
    memset(&dest_sock, 0, sizeof(dest_sock));
    dest_sock.sin_family = AF_INET;
    dest_sock.sin_port = htons(SSDP_MULTICAST_PORT);
    inet_pton(AF_INET, SSDP_MULTICAST_ADDRESS, &dest_sock.sin_addr);

    /* Send SSDP request */
    len = strlen(ssdp_discover_string);

    if ((bytes_out = sendto(sock, ssdp_discover_string, 
                             len, 0, 
                             (struct sockaddr*) &dest_sock, 
                             sizeof(dest_sock))) < 0) 
    {
        ret = errno;
        perror("sendto()");
        return(ret);
    }
    else if (bytes_out != len) 
    {
        fprintf(stderr, "sendto(): only sent %d of %d bytes\n", bytes_out, len);
        return(-1);
    }
    else if ( opt_verbose == true )
        printf("%s\n", ssdp_discover_string);

    return ret;
}

/*
 * *******************************************************************
 * Function: get_ssdp_responses()
 * Purpose: Process all incoming SSDP responses.
 * Returns: 0 on success, non-zero if error occurred.
 * *******************************************************************
 */
int get_ssdp_responses(int sock, struct str_vector *vector)
{
    int ret = 0, bytes_in, done = false;
    unsigned int host_sock_len;
    struct sockaddr_in host_sock;
    char buffer[MAX_BUFFER_LEN];
    char host[NI_MAXHOST];
    char *url_start, *host_start, *host_end;
    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    timeout.tv_sec = opt_timeout;
    timeout.tv_usec = 0;

    /* Loop through SSDP discovery request responses */
    do
    {
        if ((ret = select(sock+1, &read_fds, NULL, NULL, &timeout)) < 0) 
        {
            ret = errno;
            perror("select()");
            return(ret);
        }

        if (FD_ISSET(sock, &read_fds))
        {
            host_sock_len = sizeof(host_sock);
            if ((bytes_in = recvfrom(sock, buffer, sizeof(buffer), 0, 
                                    &host_sock, &host_sock_len)) < 0)
            {
                ret = errno;
                perror("recvfrom()");
                return(ret);
            }
            buffer[bytes_in] = '\0';

            /* Parse valid responses */
            if (strncmp(buffer, "HTTP/1.1 200 OK", 12) == 0)
            {
                if ( opt_verbose == true ) 
                {
                    printf("\n%s", buffer);
                }

                /* Skip ahead to url in SSDP response */
                if ( (url_start = strcasestr(buffer, "LOCATION:")) != NULL )
                {
                    /* Get hostname/IP address in SSDP response */
                    if ( (host_start = strstr(url_start, "http://")) != NULL )
                    {
                        host_start += 7;    /* Move past "http://" */

                        if ( (host_end = strstr(host_start, ":")) != NULL )
                        {
                            strncpy(host, host_start, host_end - host_start);
                            host[host_end - host_start] = '\0';

                            /* Add host to vector if we haven't done so already */
                            if ( str_vector_search(vector, host) == false )
                            {
                                str_vector_add(vector, host);
                                printf("%s", host);

                                /* Are we doing reverse lookups? */
                                if ( opt_rdns_lookup == true )
                                {
                                    char name[NI_MAXHOST];
                                    name[0] = '\0';

                                    if ( (rdns_lookup(host, name, NI_MAXHOST)) == 0 )
                                    {
                                        printf("\t%s", name);
                                    }
                                }

                                printf("\n");
                            }
                        }
                    }
                }
            }
            else
            {
                fprintf(stderr, "[Unexpected SSDP response]\n");
                if ( opt_verbose == true ) 
                {
                    printf("%s\n\n", buffer);
                }
            }
        } 
        else 
        {
            /* select() timed out, so we're done */
            done = true;
        }

    } while ( done == false );

    return ret;
}

/*
 * *******************************************************************
 * Function: rdns_lookup()
 * Purpose: Given an IP address in *ip_addr, return its hostname in 
 *           *hostname, not to exeed hostname_size
 * Returns: 0 on success
 * *******************************************************************
 */
int rdns_lookup(char *ip_addr, char *hostname, int hostname_size)
{
    int ret;
    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    inet_pton(AF_INET, ip_addr, &sa.sin_addr);

    if ( (ret = getnameinfo((struct sockaddr *)&sa, sizeof(sa), 
                             hostname, hostname_size, NULL, 0, 0)) != 0 )
    {
        fprintf(stderr, "getnameinfo(): %s\n", gai_strerror(ret));
    }

    return(ret);
}

/*
 * *******************************************************************
 * Function: parse_cmd_opts()
 * Purpose: Given argc and argv[], parse the command line options.
 * Returns: 0 on success, exits on failure
 * *******************************************************************
 */
int parse_cmd_opts(int argc, char *argv[])
{
    int cmdopt;

    while ( (cmdopt = getopt(argc, argv, "p:rt:v")) != -1 ) 
    {
        switch (cmdopt) 
        {
            case 'p':
                opt_source_port = atoi(optarg);
                break;
            case 'r':
                opt_rdns_lookup = true;
                break;
            case 't':
                opt_timeout = abs(atoi(optarg));
                break;
            case 'v':
                opt_verbose = true;
                break;
            default:
                printf("\nUsage: %s [OPTION]...", argv[0]);
                printf("\nDiscover and list UPnP devices on the network.\n\n");
                printf("Available options:\n\n");
                printf("  -p [port]\tSpecify client-side (source) UDP port to bind to\n");
                printf("  -r\t\tDo reverse DNS lookups\n");
                printf("  -t [interval]\tSpecify timeout interval in seconds (default is %d)\n", opt_timeout);
                printf("  -v\t\tProvide verbose information\n");
                printf("\n");
                exit(0);
        }
    }

    return(0);
}

/* EOF */

