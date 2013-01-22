/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2001-2012 Aymeric MOIZARD amoizard@antisip.com
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "eXosip2.h"
#include "eXtransport.h"

#ifdef _WIN32_WCE
#include "inet_ntop.h"
#elif WIN32
#include "inet_ntop.h"
#endif

#if defined(_WIN32_WCE)
#define strerror(X) "-1"
#endif

#if !defined(_WIN32_WCE)
#include <errno.h>
#endif

#ifdef __APPLE_CC__
#include "TargetConditionals.h"
#endif

void udp_tl_learn_port_from_via (struct eXosip_t *excontext, osip_message_t * sip);


struct eXtludp {
  int udp_socket;
  struct sockaddr_storage ai_addr;

  char udp_firewall_ip[64];
  char udp_firewall_port[10];
};

static int
udp_tl_init (struct eXosip_t *excontext)
{
  struct eXtludp *reserved = (struct eXtludp *) osip_malloc (sizeof (struct eXtludp));

  if (reserved == NULL)
    return OSIP_NOMEM;
  reserved->udp_socket = 0;
  memset (&reserved->ai_addr, 0, sizeof (struct sockaddr_storage));
  memset (reserved->udp_firewall_ip, 0, sizeof (reserved->udp_firewall_ip));
  memset (reserved->udp_firewall_port, 0, sizeof (reserved->udp_firewall_port));

  excontext->eXtludp_reserved = reserved;
  return OSIP_SUCCESS;
}

static int
udp_tl_free (struct eXosip_t *excontext)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  memset (reserved->udp_firewall_ip, 0, sizeof (reserved->udp_firewall_ip));
  memset (reserved->udp_firewall_port, 0, sizeof (reserved->udp_firewall_port));
  memset (&reserved->ai_addr, 0, sizeof (struct sockaddr_storage));
  if (reserved->udp_socket > 0)
    close (reserved->udp_socket);

  osip_free (reserved);
  excontext->eXtludp_reserved = NULL;
  return OSIP_SUCCESS;
}

#if !defined(WIN32) && !defined(_WIN32_WCE)
#define SOCKET_OPTION_VALUE	void *
#else
#define SOCKET_OPTION_VALUE char *
#endif

int
_eXosip_transport_set_dscp(struct eXosip_t *excontext, int family, int sock)
{
  int res;
  if (family == AF_INET) {
    int tos = (excontext->dscp << 2) & 0xFC;
    res = setsockopt (sock, IPPROTO_IP, IP_TOS, (SOCKET_OPTION_VALUE) & tos, sizeof (tos));
  }
  else {
    int tos = (excontext->dscp << 2) & 0xFC;
#ifdef IPV6_TCLASS
    res = setsockopt (sock, IPPROTO_IPV6, IPV6_TCLASS, (SOCKET_OPTION_VALUE) & tos, sizeof (tos));
#else
    res = setsockopt (sock, IPPROTO_IPV6, IP_TOS, (SOCKET_OPTION_VALUE) & tos, sizeof (tos));
#endif
  }
  return res;
}

static int
udp_tl_open (struct eXosip_t *excontext)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;
  int res;
  struct addrinfo *addrinfo = NULL;
  struct addrinfo *curinfo;
  int sock = -1;

  if (eXtl_udp.proto_port < 0)
    eXtl_udp.proto_port = 5060;


  res = _eXosip_get_addrinfo (excontext, &addrinfo, eXtl_udp.proto_ifs, eXtl_udp.proto_port, eXtl_udp.proto_num);
  if (res)
    return -1;

  for (curinfo = addrinfo; curinfo; curinfo = curinfo->ai_next) {
    socklen_t len;

    if (curinfo->ai_protocol && curinfo->ai_protocol != eXtl_udp.proto_num) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "eXosip: Skipping protocol %d\n", curinfo->ai_protocol));
      continue;
    }

    sock = (int) socket (curinfo->ai_family, curinfo->ai_socktype, curinfo->ai_protocol);
    if (sock < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot create socket %s!\n", strerror (errno)));
      continue;
    }

    if (curinfo->ai_family == AF_INET6) {
#ifdef IPV6_V6ONLY
      if (setsockopt_ipv6only (sock)) {
        close (sock);
        sock = -1;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot set socket option %s!\n", strerror (errno)));
        continue;
      }
#endif /* IPV6_V6ONLY */
    }

#if SO_NOSIGPIPE
    {
      int val;

      val = 1;
      setsockopt (sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &val, sizeof (int));
    }
#endif

    res = bind (sock, curinfo->ai_addr, curinfo->ai_addrlen);
    if (res < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind socket node:%s family:%d %s\n", eXtl_udp.proto_ifs, curinfo->ai_family, strerror (errno)));
      close (sock);
      sock = -1;
      continue;
    }
    len = sizeof (reserved->ai_addr);
    res = getsockname (sock, (struct sockaddr *) &reserved->ai_addr, &len);
    if (res != 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot get socket name (%s)\n", strerror (errno)));
      memcpy (&reserved->ai_addr, curinfo->ai_addr, curinfo->ai_addrlen);
    }

    if (eXtl_udp.proto_num != IPPROTO_UDP) {
      res = listen (sock, SOMAXCONN);
      if (res < 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind socket node:%s family:%d %s\n", eXtl_udp.proto_ifs, curinfo->ai_family, strerror (errno)));
        close (sock);
        sock = -1;
        continue;
      }
    }

    break;
  }

  _eXosip_freeaddrinfo (addrinfo);

  if (sock < 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind on port: %i\n", eXtl_udp.proto_port));
    return -1;
  }

  reserved->udp_socket = sock;

  _eXosip_transport_set_dscp(excontext, eXtl_udp.proto_family, sock);

  if (eXtl_udp.proto_port == 0) {
    /* get port number from socket */
    if (eXtl_udp.proto_family == AF_INET)
      eXtl_udp.proto_port = ntohs (((struct sockaddr_in *) &reserved->ai_addr)->sin_port);
    else
      eXtl_udp.proto_port = ntohs (((struct sockaddr_in6 *) &reserved->ai_addr)->sin6_port);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "eXosip: Binding on port %i!\n", eXtl_udp.proto_port));
  }
  snprintf (reserved->udp_firewall_port, sizeof (reserved->udp_firewall_port), "%i", eXtl_udp.proto_port);
  return OSIP_SUCCESS;
}

static int
_udp_tl_reset (struct eXosip_t *excontext)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  if (reserved->udp_socket > 0)
    close (reserved->udp_socket);
  return udp_tl_open (excontext);
}

static int
udp_tl_set_fdset (struct eXosip_t *excontext, fd_set * osip_fdset, fd_set * osip_wrset, int *fd_max)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  if (reserved->udp_socket <= 0)
    return -1;

  eXFD_SET (reserved->udp_socket, osip_fdset);

  if (reserved->udp_socket > *fd_max)
    *fd_max = reserved->udp_socket;

  return OSIP_SUCCESS;
}

void
udp_tl_learn_port_from_via (struct eXosip_t *excontext, osip_message_t * sip)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  /* EXOSIP_OPT_UDP_LEARN_PORT option set */
  if (excontext->learn_port > 0) {
    osip_via_t *via = NULL;
    osip_generic_param_t *br;
    int i;

    i = osip_message_get_via (sip, 0, &via);
    if (i >= 0 && via != NULL && via->protocol != NULL && (osip_strcasecmp (via->protocol, "udp") == 0 || osip_strcasecmp (via->protocol, "dtls-udp") == 0)) {
      osip_via_param_get_byname (via, "rport", &br);
      if (br != NULL && br->gvalue != NULL) {
        struct eXosip_account_info ainfo;

        memset (&ainfo, 0, sizeof (struct eXosip_account_info));
        snprintf (reserved->udp_firewall_port, sizeof (reserved->udp_firewall_port), "%s", br->gvalue);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "SIP port modified from rport in SIP answer\r\n"));

        osip_via_param_get_byname (via, "received", &br);
        if (br != NULL && br->gvalue != NULL && sip->from != NULL && sip->from->url != NULL && sip->from->url->host != NULL) {
          snprintf (ainfo.proxy, sizeof (ainfo.proxy), "%s", sip->from->url->host);
          ainfo.nat_port = atoi (reserved->udp_firewall_port);
          snprintf (ainfo.nat_ip, sizeof (ainfo.nat_ip), "%s", br->gvalue);
          eXosip_set_option (excontext, EXOSIP_OPT_ADD_ACCOUNT_INFO, &ainfo);
        }
      }
    }
  }
  return;
}

static int
udp_tl_read_message (struct eXosip_t *excontext, fd_set * osip_fdset, fd_set * osip_wrset)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;
  char *buf;
  int i;

  if (reserved->udp_socket <= 0)
    return -1;

  if (FD_ISSET (reserved->udp_socket, osip_fdset)) {
    struct sockaddr_storage sa;
    socklen_t slen;

    if (eXtl_udp.proto_family == AF_INET)
      slen = sizeof (struct sockaddr_in);
    else
      slen = sizeof (struct sockaddr_in6);

    buf = (char *) osip_malloc (SIP_MESSAGE_MAX_LENGTH * sizeof (char) + 1);
    if (buf == NULL)
      return OSIP_NOMEM;

    i = (int) recvfrom (reserved->udp_socket, buf, SIP_MESSAGE_MAX_LENGTH, 0, (struct sockaddr *) &sa, &slen);

    if (i > 5) {
      char src6host[NI_MAXHOST];
      int recvport = 0;
      int err;

      buf[i] = '\0';

      memset (src6host, 0, sizeof (src6host));

      if (eXtl_udp.proto_family == AF_INET)
        recvport = ntohs (((struct sockaddr_in *) &sa)->sin_port);
      else
        recvport = ntohs (((struct sockaddr_in6 *) &sa)->sin6_port);

#if defined(__arc__)
      {
        struct sockaddr_in *fromsa = (struct sockaddr_in *) &sa;
        char *tmp;

        tmp = inet_ntoa (fromsa->sin_addr);
        if (tmp == NULL) {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Message received from: NULL:%i inet_ntoa failure\n", recvport));
        }
        else {
          snprintf (src6host, sizeof (src6host), "%s", tmp);
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Message received from: %s:%i\n", src6host, recvport));
        }
      }
#else
      err = getnameinfo ((struct sockaddr *) &sa, slen, src6host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

      if (err != 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Message received from: NULL:%i getnameinfo failure\n", recvport));
        snprintf (src6host, sizeof (src6host), "127.0.0.1");
      }
      else {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Message received from: %s:%i\n", src6host, recvport));
      }
#endif

      _eXosip_handle_incoming_message (excontext, buf, i, reserved->udp_socket, src6host, recvport, NULL, NULL);

    }
    else if (i < 0) {
#ifdef _WIN32_WCE
      int my_errno = 0;
#else
      int my_errno = errno;
#endif
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Could not read socket (%i) (%i) (%s)\n", i, my_errno, strerror (my_errno)));
      if (my_errno == 57) {
        _udp_tl_reset (excontext);
      }
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Dummy SIP message received\n"));
    }

    osip_free (buf);
  }

  return OSIP_SUCCESS;
}

static int
eXtl_update_local_target (struct eXosip_t *excontext, osip_message_t * req)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;
  int pos = 0;

  struct eXosip_account_info *ainfo = NULL;
  char *proxy = NULL;
  int i;

  if (MSG_IS_REQUEST (req)) {
    if (req->from != NULL && req->from->url != NULL && req->from->url->host != NULL)
      proxy = req->from->url->host;
  }
  else {
    if (req->to != NULL && req->to->url != NULL && req->to->url->host != NULL)
      proxy = req->to->url->host;
  }

  if (proxy != NULL) {
    for (i = 0; i < MAX_EXOSIP_ACCOUNT_INFO; i++) {
      if (excontext->account_entries[i].proxy[0] != '\0') {
        if (strstr (excontext->account_entries[i].proxy, proxy) != NULL || strstr (proxy, excontext->account_entries[i].proxy) != NULL) {
          /* use ainfo */
          if (excontext->account_entries[i].nat_ip[0] != '\0') {
            ainfo = &excontext->account_entries[i];
            break;
          }
        }
      }
    }
  }

  if (reserved->udp_firewall_ip[0] != '\0') {

    while (!osip_list_eol (&req->contacts, pos)) {
      osip_contact_t *co;

      co = (osip_contact_t *) osip_list_get (&req->contacts, pos);
      pos++;
      if (co != NULL && co->url != NULL && co->url->host != NULL
#if 0
          && 0 == osip_strcasecmp (co->url->host, udp_firewall_ip)
#endif
        ) {
        if (ainfo == NULL) {
          if (co->url->port == NULL && 0 != osip_strcasecmp (reserved->udp_firewall_port, "5060")) {
            co->url->port = osip_strdup (reserved->udp_firewall_port);
          }
          else if (co->url->port != NULL && 0 != osip_strcasecmp (reserved->udp_firewall_port, co->url->port)) {
            osip_free (co->url->port);
            co->url->port = osip_strdup (reserved->udp_firewall_port);
          }
        }
        else {
          if (co->url->port == NULL && ainfo->nat_port != 5060) {
            co->url->port = osip_malloc (10);
            if (co->url->port == NULL)
              return OSIP_NOMEM;
            snprintf (co->url->port, 9, "%i", ainfo->nat_port);
          }
          else if (co->url->port != NULL && ainfo->nat_port != atoi (co->url->port)) {
            osip_free (co->url->port);
            co->url->port = osip_malloc (10);
            if (co->url->port == NULL)
              return OSIP_NOMEM;
            snprintf (co->url->port, 9, "%i", ainfo->nat_port);
          }
#if 1
          if (ainfo->nat_ip[0] != '\0') {
            osip_free (co->url->host);
            co->url->host = osip_strdup (ainfo->nat_ip);
          }
        }
      }
#endif
    }
  }

  return OSIP_SUCCESS;
}

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

static int
udp_tl_send_message (struct eXosip_t *excontext, osip_transaction_t * tr, osip_message_t * sip, char *host, int port, int out_socket)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;
  int len = 0;
  size_t length = 0;
  struct addrinfo *addrinfo;
  struct __eXosip_sockaddr addr;
  char *message = NULL;

  char ipbuf[INET6_ADDRSTRLEN];
  int i;
  osip_naptr_t *naptr_record = NULL;

  if (reserved->udp_socket <= 0)
    return -1;

  if (host == NULL) {
    host = sip->req_uri->host;
    if (sip->req_uri->port != NULL)
      port = osip_atoi (sip->req_uri->port);
    else
      port = 5060;
  }

  eXtl_update_local_target (excontext, sip);

  i = -1;
#ifndef MINISIZE
  if (tr == NULL) {
    _eXosip_srv_lookup (excontext, sip, &naptr_record);

    if (naptr_record != NULL) {
      eXosip_dnsutils_dns_process (naptr_record, 1);
      if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE || naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS)
        eXosip_dnsutils_dns_process (naptr_record, 1);
    }

    if (naptr_record != NULL && naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVDONE) {
      /* 4: check if we have the one we want... */
      if (naptr_record->sipudp_record.name[0] != '\0' && naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].srv[0] != '\0') {
        /* always choose the first here.
           if a network error occur, remove first entry and
           replace with next entries.
         */
        osip_srv_entry_t *srv;
        int n = 0;

        for (srv = &naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index];
             n < 10 && naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].srv[0]; srv = &naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index]) {
          if (srv->ipaddress[0])
            i = _eXosip_get_addrinfo (excontext, &addrinfo, srv->ipaddress, srv->port, IPPROTO_UDP);
          else
            i = _eXosip_get_addrinfo (excontext, &addrinfo, srv->srv, srv->port, IPPROTO_UDP);
          if (i == 0) {
            host = srv->srv;
            port = srv->port;
            break;
          }

          i = eXosip_dnsutils_rotate_srv (&naptr_record->sipudp_record);
          if (i <= 0) {
            return -1;
          }
          if (i >= n) {
            return -1;
          }
          i = -1;
          /* copy next element */
          n++;
        }
      }
    }

    if (naptr_record != NULL && naptr_record->keep_in_cache == 0)
      osip_free (naptr_record);
    naptr_record = NULL;
  }
  else {
    naptr_record = tr->naptr_record;
  }

  if (naptr_record != NULL) {
    /* 1: make sure there is no pending DNS */
    eXosip_dnsutils_dns_process (naptr_record, 0);
    if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE || naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS)
      eXosip_dnsutils_dns_process (naptr_record, 0);

    if (naptr_record->naptr_state == OSIP_NAPTR_STATE_UNKNOWN) {
      /* fallback to DNS A */
      if (naptr_record->keep_in_cache == 0)
        osip_free (naptr_record);
      naptr_record = NULL;
      if (tr != NULL)
        tr->naptr_record = NULL;
      /* must never happen? */
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_INPROGRESS) {
      /* 2: keep waiting (naptr answer not received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE) {
      /* 3: keep waiting (naptr answer received/no srv answer received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS) {
      /* 3: keep waiting (naptr answer received/no srv answer received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVDONE) {
      /* 4: check if we have the one we want... */
      if (naptr_record->sipudp_record.name[0] != '\0' && naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].srv[0] != '\0') {
        /* always choose the first here.
           if a network error occur, remove first entry and
           replace with next entries.
         */
        osip_srv_entry_t *srv;
        int n = 0;

        for (srv = &naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index];
             n < 10 && naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].srv[0]; srv = &naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index]) {
          if (srv->ipaddress[0])
            i = _eXosip_get_addrinfo (excontext, &addrinfo, srv->ipaddress, srv->port, IPPROTO_UDP);
          else
            i = _eXosip_get_addrinfo (excontext, &addrinfo, srv->srv, srv->port, IPPROTO_UDP);
          if (i == 0) {
            host = srv->srv;
            port = srv->port;
            break;
          }

          i = eXosip_dnsutils_rotate_srv (&naptr_record->sipudp_record);
          if (i <= 0) {
            return -1;
          }
          if (i >= n) {
            return -1;
          }
          i = -1;
          /* copy next element */
          n++;
        }
      }
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NOTSUPPORTED || naptr_record->naptr_state == OSIP_NAPTR_STATE_RETRYLATER) {
      /* 5: fallback to DNS A */
      if (naptr_record->keep_in_cache == 0)
        osip_free (naptr_record);
      naptr_record = NULL;
      if (tr != NULL)
        tr->naptr_record = NULL;
    }
  }
#endif

  /* if SRV was used, destination may be already found */
  if (i != 0) {
    i = _eXosip_get_addrinfo (excontext, &addrinfo, host, port, IPPROTO_UDP);
  }

  if (i != 0) {
    return -1;
  }

  memcpy (&addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
  len = addrinfo->ai_addrlen;

  _eXosip_freeaddrinfo (addrinfo);

  /* remove preloaded route if there is no tag in the To header
   */
  {
    osip_route_t *route = NULL;
    osip_generic_param_t *tag = NULL;

    osip_message_get_route (sip, 0, &route);

    osip_to_get_tag (sip->to, &tag);
    if (tag == NULL && route != NULL && route->url != NULL) {
      osip_list_remove (&sip->routes, 0);
    }
    i = osip_message_to_str (sip, &message, &length);
    if (tag == NULL && route != NULL && route->url != NULL) {
      osip_list_add (&sip->routes, route, 0);
    }
  }

  if (i != 0 || length <= 0) {
    osip_free (message);
    return -1;
  }

  switch (((struct sockaddr *) &addr)->sa_family) {
  case AF_INET:
    inet_ntop (((struct sockaddr *) &addr)->sa_family, &(((struct sockaddr_in *) &addr)->sin_addr), ipbuf, sizeof (ipbuf));
    break;
  case AF_INET6:
    inet_ntop (((struct sockaddr *) &addr)->sa_family, &(((struct sockaddr_in6 *) &addr)->sin6_addr), ipbuf, sizeof (ipbuf));
    break;
  default:
    strncpy (ipbuf, "(unknown)", sizeof (ipbuf));
    break;
  }

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Message sent: (to dest=%s:%i)\n%s\n", ipbuf, port, message));

  if (excontext->enable_dns_cache==1 && osip_strcasecmp (host, ipbuf) != 0 && MSG_IS_REQUEST (sip)) {
    if (MSG_IS_REGISTER (sip)) {
      struct eXosip_dns_cache entry;

      memset (&entry, 0, sizeof (struct eXosip_dns_cache));
      snprintf (entry.host, sizeof (entry.host), "%s", host);
      snprintf (entry.ip, sizeof (entry.ip), "%s", ipbuf);
      eXosip_set_option (excontext, EXOSIP_OPT_ADD_DNS_CACHE, (void *) &entry);
    }
  }

  if (tr != NULL) {
    if (tr->ict_context != NULL)
      osip_ict_set_destination (tr->ict_context, osip_strdup (ipbuf), port);
    if (tr->nict_context != NULL)
      osip_nict_set_destination (tr->nict_context, osip_strdup (ipbuf), port);
  }

  if (0 > sendto (reserved->udp_socket, (const void *) message, length, 0, (struct sockaddr *) &addr, len)) {
#ifndef MINISIZE
    if (naptr_record != NULL) {
      /* rotate on failure! */
      if (eXosip_dnsutils_rotate_srv (&naptr_record->sipudp_record) > 0) {
        osip_free (message);
        return OSIP_SUCCESS + 1;        /* retry for next retransmission! */
      }
    }
#endif
    /* SIP_NETWORK_ERROR; */
    osip_free (message);
    return -1;
  }

  if (excontext->keep_alive > 0) {
    if (MSG_IS_REGISTER (sip)) {
      eXosip_reg_t *reg = NULL;

      if (_eXosip_reg_find (excontext, &reg, tr) == 0) {
        memcpy (&(reg->addr), &addr, len);
        reg->len = len;
      }
    }
  }

#ifndef MINISIZE
  if (naptr_record != NULL) {
    if (tr != NULL && (MSG_IS_REGISTER (sip) || MSG_IS_OPTIONS (sip)) && tr->last_response == NULL) {
      /* failover for outgoing transaction */
      time_t now;

      now = osip_getsystemtime (NULL);
      if (tr != NULL && now - tr->birth_time > 10 && now - tr->birth_time < 13) {
        /* avoid doing this twice... */
        if (eXosip_dnsutils_rotate_srv (&naptr_record->sipudp_record) > 0) {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL,
                                  "Doing failover: %s:%i->%s:%i\n", host, port, naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].srv, naptr_record->sipudp_record.srventry[naptr_record->sipudp_record.index].port));
          osip_free (message);
          return OSIP_SUCCESS + 1;      /* retry for next retransmission! */
        }
      }
    }
  }
#endif

  osip_free (message);
  return OSIP_SUCCESS;
}

static int
udp_tl_keepalive (struct eXosip_t *excontext)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;
  char buf[4] = "jaK";
  eXosip_reg_t *jr;

  if (excontext->keep_alive <= 0) {
    return 0;
  }

  if (reserved->udp_socket <= 0)
    return OSIP_UNDEFINED_ERROR;

  for (jr = excontext->j_reg; jr != NULL; jr = jr->next) {
    if (jr->len > 0) {
      if (sendto (reserved->udp_socket, (const void *) buf, 4, 0, (struct sockaddr *) &(jr->addr), jr->len) > 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "eXosip: Keep Alive sent on UDP!\n"));
      }
    }
  }
  return OSIP_SUCCESS;
}

static int
udp_tl_set_socket (struct eXosip_t *excontext, int socket)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  reserved->udp_socket = socket;

  return OSIP_SUCCESS;
}

static int
udp_tl_masquerade_contact (struct eXosip_t *excontext, const char *public_address, int port)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  if (public_address == NULL || public_address[0] == '\0') {
    memset (reserved->udp_firewall_ip, '\0', sizeof (reserved->udp_firewall_ip));
    memset (reserved->udp_firewall_port, '\0', sizeof (reserved->udp_firewall_port));
    if (eXtl_udp.proto_port > 0)
      snprintf (reserved->udp_firewall_port, sizeof (reserved->udp_firewall_port), "%i", eXtl_udp.proto_port);
    return OSIP_SUCCESS;
  }
  snprintf (reserved->udp_firewall_ip, sizeof (reserved->udp_firewall_ip), "%s", public_address);
  if (port > 0) {
    snprintf (reserved->udp_firewall_port, sizeof (reserved->udp_firewall_port), "%i", port);
  }
  return OSIP_SUCCESS;
}

static int
udp_tl_get_masquerade_contact (struct eXosip_t *excontext, char *ip, int ip_size, char *port, int port_size)
{
  struct eXtludp *reserved = (struct eXtludp *) excontext->eXtludp_reserved;

  memset (ip, 0, ip_size);
  memset (port, 0, port_size);

  if (reserved->udp_firewall_ip[0] != '\0')
    snprintf (ip, ip_size, "%s", reserved->udp_firewall_ip);

  if (reserved->udp_firewall_port[0] != '\0')
    snprintf (port, port_size, "%s", reserved->udp_firewall_port);
  return OSIP_SUCCESS;
}

struct eXtl_protocol eXtl_udp = {
  1,
  5060,
  "UDP",
  "0.0.0.0",
  IPPROTO_UDP,
  AF_INET,
  0,
  0,

  &udp_tl_init,
  &udp_tl_free,
  &udp_tl_open,
  &udp_tl_set_fdset,
  &udp_tl_read_message,
  &udp_tl_send_message,
  &udp_tl_keepalive,
  &udp_tl_set_socket,
  &udp_tl_masquerade_contact,
  &udp_tl_get_masquerade_contact,
  NULL
};
