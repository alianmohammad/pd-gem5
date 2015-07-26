/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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

/* @file
 * Interface to connect a simulated ethernet device to the real world
 */

#if defined(__OpenBSD__) || defined(__APPLE__)
#include <sys/param.h>
#endif
#include <netinet/in.h>
#include <unistd.h>

#include <deque>
#include <string>

#include "base/misc.hh"
#include "base/pollevent.hh"
#include "base/socket.hh"
#include "base/trace.hh"
#include "debug/Ethernet.hh"
#include "debug/EthernetData.hh"
#include "dev/etherdump.hh"
#include "dev/etherint.hh"
#include "dev/etherpkt.hh"
#include "dev/ethertap.hh"
#include <fcntl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <iostream>

using namespace std;

/**
 */
class TapListener
{
  protected:
    /**
     */
    class Event : public PollEvent
    {
      protected:
        TapListener *listener;

      public:
        Event(TapListener *l, int fd, int e)
            : PollEvent(fd, e), listener(l) {}

        virtual void process(int revent) { listener->accept(); }
    };

    friend class Event;
    Event *event;

  protected:
    ListenSocket listener;
    EtherTap *tap;
    int port;

  public:
    TapListener(EtherTap *t, int p)
        : event(NULL), tap(t), port(p) {}
    ~TapListener() { if (event) delete event; }

    void accept();
    void listen(bool flag);
};

void
TapListener::listen(bool flag)
{
    // if this is the first instance of switch tap then we should create
    // a socket, otherwise we don't need to create a new socket
    if (flag) {
        while (!listener.listenTap(port, true)) {
           DPRINTF(Ethernet, "TapListener(listen): Can't bind port %d\n"
                   , port);
           port++;
        }
        struct ifaddrs * ifAddrStruct=NULL;
        struct ifaddrs * ifa=NULL;
        void * tmpAddrPtr=NULL;
        getifaddrs(&ifAddrStruct);

        //print the port and ip information of EthrTap
        for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
           if (!ifa->ifa_addr)
              continue;
           //check if it is a valid IP4 Address
           if (ifa->ifa_addr->sa_family == AF_INET) {
             tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
             char addressBuffer[INET_ADDRSTRLEN];
             inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
             if (!strcmp(ifa->ifa_name, "eth0")) {
                 setbuf(stdout, NULL);
                 ccprintf(cerr, "Listening for tap connection on %s %s %d\n",
                 ifa->ifa_name, addressBuffer, port);
                 fflush(stdout);
             }
           }
        }
        if (ifAddrStruct!=NULL)
            freeifaddrs(ifAddrStruct);
     }
    event = new Event(this, listener.getfdStatic(), POLLIN|POLLERR);
    pollQueue.schedule(event);
}

void
TapListener::accept()
{
    if (tap->isattached()) {
        DPRINTF(Ethernet, "EtherTap already attached\n");
        return;
    }
     // As a consequence of being called from the PollQueue, we might
     // have been called from a different thread. Migrate to "our"
     // thread.
     EventQueue::ScopedMigration migrate(tap->eventQueue());

    if (!listener.anyislistening())
         panic("TapListener(accept): cannot accept if we're not listening!");

    int sfd = listener.acceptTap(true);
     if (sfd != -1)
         tap->attach(sfd);
}

/**
 */
class TapConnector
{
  protected:
    ConnectSocket connector;
    EtherTap *tap;
    int port;
    const char *ip;

  public:
    TapConnector(EtherTap *t, int p, const char *ip_)
        : tap(t), port(p), ip(ip_) {}
    ~TapConnector() { }

    void connect();
};

void
TapConnector::connect()
{
    // connect to the switch tap device which is listening for connections
    int sfd = connector.connect(port, ip, true);
    if (sfd != -1)
        tap->attach(sfd);
}
/**
 */
EtherTap::EtherTap(const Params *p)
     : EtherObject(p), socket(-1), buflen(p->bufsz), dump(p->dump),
      interface(NULL), pollRate(p->poll_rate), txEvent(this),
      tapInEvent(this), attached(false)
{
     if (ListenSocket::allDisabled())
         fatal("All listeners are disabled! EtherTap can't work!");

     buffer = new char[buflen];
    static bool flag = true;
    // if this is a tap connection in switch
    if (p->server) {
        listener = new TapListener(this, p->port);
        listener->listen(flag);
        flag = false;
    }
    // if this is a tap connection in nodes
    else {
        connector = new TapConnector(this, p->port, p->server_ip.c_str());
        connector->connect();
    }
     interface = new EtherTapInt(name() + ".interface", this);
}

EtherTap::~EtherTap()
{
    if (buffer)
        delete [] buffer;

    delete interface;
    delete listener;
}

void
EtherTap::attach(int fd)
{
    if (socket != -1)
        close(fd);

    buffer_offset = 0;
    data_len = 0;
    socket = fd;
    DPRINTF(Ethernet, "EtherTap attached\n");
    attached = true;
    int nonBlocking = 1;
    fcntl(socket, F_SETFL, O_NONBLOCK, nonBlocking);
    if (!tapInEvent.scheduled())
        schedule(tapInEvent, curTick() + pollRate);
}

void
EtherTap::detach()
{
    DPRINTF(Ethernet, "EtherTap detached\n");
    close(socket);
    socket = -1;
}

bool
EtherTap::recvPacket(EthPacketPtr packet)
{
    if (dump)
        dump->dump(packet);

    DPRINTF(Ethernet, "EtherTap output len=%d\n", packet->length);
    DDUMP(EthernetData, packet->data, packet->length);
    uint32_t len = htonl(packet->length);
    ssize_t ret = write(socket, &len, sizeof(len));
    if (ret != sizeof(len))
        return false;
    ret = write(socket, packet->data, packet->length);
    if (ret != packet->length)
        return false;

    interface->recvDone();

    return true;
}

void
EtherTap::sendDone()
{}

void
EtherTap::process()
{
    char *data = buffer + sizeof(uint32_t);

    if (buffer_offset < data_len + sizeof(uint32_t)) {
        int len = read(socket, buffer + buffer_offset, buflen - buffer_offset);
        if (len <= 0) {
            if (!tapInEvent.scheduled())
                schedule(tapInEvent, curTick() + pollRate);
            return;
        }

        buffer_offset += len;

        if (data_len == 0)
            data_len = ntohl(*(uint32_t *)buffer);

        DPRINTF(Ethernet, "Received data from peer: len=%d buffer_offset=%d "
                "data_len=%d\n", len, buffer_offset, data_len);
    }

    while (data_len != 0 && buffer_offset >= data_len + sizeof(uint32_t)) {
        EthPacketPtr packet;
        packet = make_shared<EthPacketData>(data_len);
        packet->length = data_len;
        memcpy(packet->data, data, data_len);

        buffer_offset -= data_len + sizeof(uint32_t);
        assert(buffer_offset >= 0);
        if (buffer_offset > 0) {
            memmove(buffer, data + data_len, buffer_offset);
            data_len = ntohl(*(uint32_t *)buffer);
        } else
            data_len = 0;

        DPRINTF(Ethernet, "EtherTap input len=%d\n", packet->length);
        DDUMP(EthernetData, packet->data, packet->length);
        if (!interface->sendPacket(packet)) {
            DPRINTF(Ethernet, "bus busy...buffer for retransmission\n");
            packetBuffer.push(packet);
            if (!txEvent.scheduled())
                schedule(txEvent, curTick() + retryTime);
        } else if (dump) {
            dump->dump(packet);
        }
    }
    if (!tapInEvent.scheduled())
        schedule(tapInEvent, curTick() + pollRate);
}

void
EtherTap::retransmit()
{
    if (packetBuffer.empty())
        return;

    EthPacketPtr packet = packetBuffer.front();
    if (interface->sendPacket(packet)) {
        if (dump)
            dump->dump(packet);
        DPRINTF(Ethernet, "EtherTap retransmit\n");
        packetBuffer.front() = NULL;
        packetBuffer.pop();
    }

    if (!packetBuffer.empty() && !txEvent.scheduled())
        schedule(txEvent, curTick() + retryTime);
}

EtherInt*
EtherTap::getEthPort(const std::string &if_name, int idx)
{
    if (if_name == "tap") {
        if (interface->getPeer())
            panic("Interface already connected to\n");
        return interface;
    }
    return NULL;
}


//=====================================================================

void
EtherTap::serialize(ostream &os)
{
    // empty socket before serialization
    process();
    SERIALIZE_SCALAR(socket);
    SERIALIZE_SCALAR(buflen);
    uint8_t *buffer = (uint8_t *)this->buffer;
    SERIALIZE_ARRAY(buffer, buflen);
    SERIALIZE_SCALAR(buffer_offset);
    SERIALIZE_SCALAR(data_len);

}

void
EtherTap::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_SCALAR(buflen);
    uint8_t *buffer = (uint8_t *)this->buffer;
    UNSERIALIZE_ARRAY(buffer, buflen);
    UNSERIALIZE_SCALAR(buffer_offset);
    UNSERIALIZE_SCALAR(data_len);
    if (tapInEvent.scheduled())
        deschedule(tapInEvent);
    schedule(tapInEvent, curTick() + 1000);
}

//=====================================================================

EtherTap *
EtherTapParams::create()
{
    return new EtherTap(this);
}
