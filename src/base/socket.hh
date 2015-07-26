/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Nathan Binkert
 */

#ifndef __SOCKET_HH__
#define __SOCKET_HH__

class ListenSocket
{
  protected:
    static bool listeningDisabled;
    static bool anyListening;

  public:
    static void disableAll();
    static bool allDisabled();

  protected:
    bool listening;
    int fd;
    /**
     * Define an static fd variable. It will be used for all EtherTap devices
     * instantiated in switch, then you don't need to occupy several ports in
     * your system by creating so one socket per port
     */
    static int fdStatic;

  public:
    ListenSocket();
    virtual ~ListenSocket();

    virtual int accept(bool nodelay = false);
    /**
     * acceptTap accept incoming connection on fdStatic.
     */
    virtual int acceptTap(bool nodelay = false);
    virtual bool listen(int port, bool reuse = true);
    /**
     * listenTap is called by the first instant of EtherTap.
     * It initializes "fdStatic"
     */
    virtual bool listenTap(int port, bool reuse = true);

    int getfd() const { return fd; }
    int getfdStatic() const { return fdStatic; }
    bool islistening() const { return listening; }
    bool anyislistening() const { return anyListening; }
};

/**
 * Client socket class. EtherTap of pd-gem5 nodes should act as a client and
 * connect to a corresponding EtherTap port of the switch.
 * ConnectSocket vs. ListenSocket:
 * ListenSocket is like a server connection
 * ConnectSocket is like a client connection
 */
class ConnectSocket
{
  protected:
    int fd;

  public:
    ConnectSocket();
    virtual ~ConnectSocket();

    virtual int connect(int port, const char* ip, bool nodelay = false);

    int getfd() const { return fd; }
};


#endif //__SOCKET_HH__
