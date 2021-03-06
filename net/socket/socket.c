/****************************************************************************
 * net/socket/socket.c
 *
 *   Copyright (C) 2007-2009, 2012, 2014-2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/socket.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include "usrsock/usrsock.h"
#include "devspecsock/devspecsock.h"
#include "socket/socket.h"

#ifdef CONFIG_NET

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: psock_socket
 *
 * Description:
 *   socket() creates an endpoint for communication and returns a socket
 *   structure.
 *
 * Parameters:
 *   domain   (see sys/socket.h)
 *   type     (see sys/socket.h)
 *   protocol (see sys/socket.h)
 *   psock    A pointer to a user allocated socket structure to be initialized.
 *
 * Returned Value:
 *   0 on success; -1 on error with errno set appropriately
 *
 *   EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 *   EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 *   EINVAL
 *     Unknown protocol, or protocol family not available.
 *   EMFILE
 *     Process file table overflow.
 *   ENFILE
 *     The system limit on the total number of open files has been reached.
 *   ENOBUFS or ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 *   EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 *
 * Assumptions:
 *
 ****************************************************************************/

int psock_socket(int domain, int type, int protocol, FAR struct socket *psock)
{
  FAR const struct sock_intf_s *sockif = NULL;
  int errcode;
  int ret;

  /* Initialize the socket structure */

  psock->s_domain = domain;
  psock->s_type   = type;
  psock->s_conn   = NULL;
#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
  psock->s_sndcb  = NULL;
#endif

#ifdef CONFIG_NET_USRSOCK
  if (domain != PF_LOCAL && domain != PF_UNSPEC)
    {
      /* Handle special setup for USRSOCK sockets (user-space networking
       * stack).
       */

      ret = g_usrsock_sockif.si_setup(psock, protocol);
      if (ret == -ENETDOWN)
        {
          /* -ENETDOWN means that USRSOCK daemon is not running.  Attempt to
           * open socket with kernel networking stack.
           */
        }
      else
        {
          psock->s_sockif = &g_usrsock_sockif;

          if (ret < 0)
            {
              errcode = -ret;
              goto errout;
            }

          return ret;
        }
    }
#endif /* CONFIG_NET_USRSOCK */

#ifdef CONFIG_NET_DEV_SPEC_SOCK
  if (domain != PF_LOCAL && domain != PF_UNSPEC)
    {
      /* Handle special setup for device-specific sockets
       * (device-specific networking stack).
       */

      ret = g_devspecsock_sockif.si_setup(psock, protocol);
      if (ret < 0)
        {
          errcode = -ret;
          goto errout;
        }

      psock->s_sockif = &g_devspecsock_sockif;

      return ret;
    }
#endif /* CONFIG_NET_DEV_SPEC_SOCK */

  /* Get the socket interface */

  sockif = net_sockif(domain);
  if (sockif == NULL)
    {
      nerr("ERROR: socket address family unsupported: %d\n", domain);
      errcode = EAFNOSUPPORT;
      goto errout;
    }

  /* The remaining of the socket initialization depends on the address
   * family.
   */

  DEBUGASSERT(sockif->si_setup != NULL);
  psock->s_sockif = sockif;

  ret = sockif->si_setup(psock, protocol);
  if (ret < 0)
    {
      nerr("ERROR: socket si_setup() failed: %d\n", ret);
      errcode = -ret;
      goto errout;
    }

  return OK;

errout:
  set_errno(errcode);
  return ERROR;
}

/****************************************************************************
 * Name: socket
 *
 * Description:
 *   socket() creates an endpoint for communication and returns a descriptor.
 *
 * Parameters:
 *   domain   (see sys/socket.h)
 *   type     (see sys/socket.h)
 *   protocol (see sys/socket.h)
 *
 * Returned Value:
 *   A non-negative socket descriptor on success; -1 on error with errno set
 *   appropriately.
 *
 *   EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 *   EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 *   EINVAL
 *     Unknown protocol, or protocol family not available.
 *   EMFILE
 *     Process file table overflow.
 *   ENFILE
 *     The system limit on the total number of open files has been reached.
 *   ENOBUFS or ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 *   EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 *
 * Assumptions:
 *
 ****************************************************************************/

int socket(int domain, int type, int protocol)
{
  FAR struct socket *psock;
  int sockfd;
  int ret;

  /* Allocate a socket descriptor */

  sockfd = sockfd_allocate(0);
  if (sockfd < 0)
    {
      nerr("ERROR: Failed to allodate a socket descriptor\n");
      set_errno(ENFILE);
      return ERROR;
    }

  /* Get the underlying socket structure */

  psock = sockfd_socket(sockfd);
  if (!psock)
    {
      set_errno(ENOSYS); /* should not happen */
      goto errout;
    }

  /* Initialize the socket structure */

  ret = psock_socket(domain, type, protocol, psock);
  if (ret < 0)
    {
      /* errno already set by psock_socket() */

      nerr("ERROR: psock_socket() failed: %d\n", ret);
      goto errout;
    }

  return sockfd;

errout:
  sockfd_release(sockfd);
  return ERROR;
}

#endif /* CONFIG_NET */
