/*
 * $Source$
 * $Author$
 *
 * Copyright 1989,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * For copying and distribution information, please see the file
 * <krb5/copyright.h>.
 *
 * Usage:
 * sample_server servername
 *
 * Simple UDP-based server application.  For demonstration.
 * This program performs no useful function.
 */

#if !defined(lint) && !defined(SABER)
static char rcsid_sim_server_c [] =
"$Id$";
#endif	/* !lint & !SABER */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>

#include <krb5/krb5.h>
#include <krb5/ext-proto.h>
#include <krb5/libos-proto.h>
#include <com_err.h>

#include "simple.h"

#define PROGNAME argv[0]
#define SNAME argv[1]

/* this should become a library function */

krb5_error_code
krb5_rc_getrcache(rcptr, server)
krb5_rcache *rcptr;
krb5_principal server;
{
    krb5_rcache rcache;
    char *cachename;
    extern krb5_deltat krb5_clockskew;
    krb5_error_code retval;

    if (rcache = (krb5_rcache) malloc(sizeof(*rcache))) {
	if (!(retval = krb5_rc_resolve_type(&rcache, "dfl"))) {

	    if (cachename = malloc(server[1]->length+1+3)) {
		strcpy(cachename, "rc_");
		strncat(cachename, server[1]->data, server[1]->length);
		cachename[server[1]->length+3] = '\0';

		if (!(retval = krb5_rc_resolve(rcache, cachename))) {
		    if (!((retval = krb5_rc_recover(rcache)) &&
			  (retval = krb5_rc_initialize(rcache,
						       krb5_clockskew)))) {
			*rcptr = rcache;
			return 0;
		    }
		}
	    } else
		retval = ENOMEM;
	}
	xfree(rcache);
    } else
	retval = ENOMEM;
    return retval;
}

void
main(argc, argv)
int argc;
char *argv[];
{
    int sock, i;
    int flags = 0;			/* for recvfrom() */
    struct servent *serv;
    struct hostent *host;
    struct sockaddr_in s_sock;		/* server's address */
    struct sockaddr_in c_sock;		/* client's address */
    char full_hname[256];		/* XXX magic number; for hostname */
    char *cp;

    krb5_error_code retval;
    krb5_data packet, message;
    unsigned char pktbuf[BUFSIZ];
    krb5_address foreign_addr, *portforeign_addr;
    krb5_rcache rcache;
    krb5_principal sprinc;
    krb5_tkt_authent *ad;

    if (argc != 2) {
	fprintf(stderr, "usage: %s <hostname>\n",PROGNAME);
	exit(1);
    }

    if (retval = krb5_parse_name(SNAME, &sprinc)) {
	com_err(PROGNAME, retval, "while parsing server name %s", SNAME);
	exit(1);
    }

    /* Set up server address */
    memset((char *)&s_sock, 0, sizeof(s_sock));
    s_sock.sin_family = AF_INET;

    /* Look up service */
    if ((serv = getservbyname(SERVICE, "udp")) == NULL) {
	fprintf(stderr, "service unknown: %s/udp\n", SERVICE);
	exit(1);
    }
    s_sock.sin_port = serv->s_port;

    if (gethostname(full_hname, sizeof(full_hname)) < 0) {
	perror("gethostname");
	exit(1);
    }

    if ((host = gethostbyname(full_hname)) == (struct hostent *)0) {
	fprintf(stderr, "%s: host unknown\n", full_hname);
	exit(1);
    }
    memcpy((char *)&s_sock.sin_addr, host->h_addr, host->h_length);

    /* Open socket */
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("opening datagram socket");
	exit(1);
    }

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *)&s_sock, sizeof(s_sock))) {
	perror("binding datagram socket");
	exit(1);
    }

#ifdef DEBUG
    printf("socket has port # %d\n", ntohs(s_sock.sin_port));
#endif

    /* GET KRB_AP_REQ MESSAGE */

    i = read(sock, (char *)pktbuf, sizeof(pktbuf));
    if (i < 0) {
	perror("receiving datagram");
	exit(1);
    }
    printf("Received %d bytes\n", i);
    packet.length = i;
    packet.data = (krb5_pointer) pktbuf;

    /* Check authentication info */
    if (retval = krb5_rd_req_simple(&packet, sprinc,
				    0, /* ignore sender address for now */
				    &ad)) {
	com_err(PROGNAME, retval, "while reading request");
	exit(1);
    }
    if (retval = krb5_unparse_name(ad->ticket->enc_part2->client, &cp)) {
	com_err(PROGNAME, retval, "while unparsing client name");
	exit(1);
    }
    printf("Got authentication info from %s\n", cp);

    free(cp);

    /* GET KRB_MK_SAFE MESSAGE */


    /* use "recvfrom" so we know client's address */
    i = sizeof(c_sock);
    i = recvfrom(sock, (char *)pktbuf, sizeof(pktbuf), flags,
		 (struct sockaddr *)&c_sock, &i);
    if (i < 0) {
	perror("receiving datagram");
	exit(1);
    }
#ifdef DEBUG
    printf("&c_sock.sin_addr is %s\n", inet_ntoa(c_sock.sin_addr));
#endif
    printf("Received %d bytes\n", i);

    packet.length = i;
    packet.data = (krb5_pointer) pktbuf;

    foreign_addr.addrtype = c_sock.sin_family;
    foreign_addr.length = sizeof(c_sock.sin_addr);
    foreign_addr.contents = (krb5_octet *)&c_sock.sin_addr;

    if (retval = krb5_gen_portaddr(&foreign_addr,
				   (krb5_pointer) &c_sock.sin_port,
				   &portforeign_addr)) {
	com_err(PROGNAME, retval, "while generating port address");
	exit(1);
    }

    if (retval = krb5_rc_getrcache(&rcache, sprinc)) {
	com_err(PROGNAME, retval, "while opening replay cache");
	exit(1);
    }
    
    if (retval = krb5_rd_safe(&packet, ad->ticket->enc_part2->session,
			      portforeign_addr, 0, 0, 0,
			      rcache,
			      &message)) {
	com_err(PROGNAME, retval, "while verifying PRIV message");
	exit(1);
    }
    printf("Safe message is: '%.*s'\n", message.length, message.data);

    xfree(message.data);
    krb5_free_addr(portforeign_addr);

    /* NOW GET ENCRYPTED MESSAGE */

    /* use "recvfrom" so we know client's address */
    i = sizeof(c_sock);
    i = recvfrom(sock, (char *)pktbuf, sizeof(pktbuf), flags,
		 (struct sockaddr *)&c_sock, &i);
    if (i < 0) {
	perror("receiving datagram");
	exit(1);
    }
    printf("Received %d bytes\n", i);

    if (retval = krb5_gen_portaddr(&foreign_addr,
				   (krb5_pointer) &c_sock.sin_port,
				   &portforeign_addr)) {
	com_err(PROGNAME, retval, "while generating port address");
	exit(1);
    }

    if (retval = krb5_rd_priv(&packet, ad->ticket->enc_part2->session,
			      portforeign_addr, 0, 0, 0, 0,
			      rcache,
			      &message)) {
	com_err(PROGNAME, retval, "while verifying SAFE message");
	exit(1);
    }
    printf("Decrypted message is: '%.*s'\n", message.length, message.data);

    exit(0);
}