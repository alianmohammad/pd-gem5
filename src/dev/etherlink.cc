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
 *          Ron Dreslinski
 */

/* @file
 * Device module for modelling a fixed bandwidth full duplex ethernet link
 */

#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Ethernet.hh"
#include "debug/EthernetData.hh"
#include "dev/etherdump.hh"
#include "dev/etherint.hh"
#include "dev/etherlink.hh"
#include "dev/etherpkt.hh"
#include "params/EtherLink.hh"
#include "sim/core.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

using namespace std;

EtherLink::EtherLink(const Params *p)
    : EtherObject(p)
{
    link[0] = new Link(name() + ".link0", this, 0, p->speed,
                       p->delay, p->delay_var, p->dump,
                       p->mode, p->ni_speed, p->ni_delay);
    link[1] = new Link(name() + ".link1", this, 1, p->speed,
                       p->delay, p->delay_var, p->dump,
                       p->mode, p->ni_speed, p->ni_delay);

    interface[0] = new Interface(name() + ".int0", link[0], link[1]);
    interface[1] = new Interface(name() + ".int1", link[1], link[0]);
}


EtherLink::~EtherLink()
{
    delete link[0];
    delete link[1];

    delete interface[0];
    delete interface[1];
}

EtherInt*
EtherLink::getEthPort(const std::string &if_name, int idx)
{
    Interface *i;
    if (if_name == "int0")
        i = interface[0];
    else if (if_name == "int1")
        i = interface[1];
    else
        return NULL;
    fatal_if(i->getPeer(), "interface already connected to\n");

    return i;
}


EtherLink::Interface::Interface(const string &name, Link *tx, Link *rx)
    : EtherInt(name), txlink(tx)
{
    tx->setTxInt(this);
    rx->setRxInt(this);
}

EtherLink::Link::Link(const string &name, EtherObject *p, int num,
                      double rate, Tick delay, Tick delay_var, EtherDump *d,
                      int mode_, double ni_rate, Tick ni_delay)
    : objName(name), parent(p), number(num), txint(NULL), rxint(NULL),
      ticksPerByte(rate), linkDelay(delay), delayVar(delay_var), dump(d),
      mode(mode_), releaseTick(0), niTicksPerByte(ni_rate),
      niDelay(ni_delay), doneEvent(this)
{
    regStats();
}

void
EtherLink::serialize(ostream &os)
{
    link[0]->serialize("link0", os);
    link[1]->serialize("link1", os);
}

void
EtherLink::unserialize(Checkpoint *cp, const string &section)
{
    link[0]->unserialize("link0", cp, section);
    link[1]->unserialize("link1", cp, section);
}

void
EtherLink::Link::txComplete(EthPacketPtr packet)
{
    DPRINTF(Ethernet, "packet received: len=%d\n", packet->length);
    DDUMP(EthernetData, packet->data, packet->length);
    rxint->sendPacket(packet);
    //dump packet here, if packets are comming from outside world!
    if (dump && (mode == 1 && name().find(".link0") == std::string::npos))
        dump->dump(packet);
}

class LinkDelayEvent : public Event
{
  protected:
    EtherLink::Link *link;
    EthPacketPtr packet;

  public:
    // non-scheduling version for createForUnserialize()
    LinkDelayEvent();
    LinkDelayEvent(EtherLink::Link *link, EthPacketPtr pkt);

    void process();

    virtual void serialize(ostream &os);
    void unserialize(Checkpoint *cp, const string &section) {}
    void unserialize(Checkpoint *cp, const string &section,
                     EventQueue *eventq);
    static Serializable *createForUnserialize(Checkpoint *cp,
                                              const string &section);
};

void
EtherLink::Link::txDone()
{
    if (dump && !mode)
        dump->dump(packet);
    DPRINTF(Ethernet, "receive packet in txDone: mode=%d\n",mode);
    // mode = 1 is 'pd-gem5 connector' mode
    // We use etherlink in this mode to connect nic or switch port to ethertap
    if (mode == 1) {
        // We assume that int0 is connected to nic or switch
        // We should timestamp packets that we recieve form int0 before
        // sending them to peer (ethertap)
        if (name().find(".link0") != std::string::npos) {
            DPRINTF(Ethernet, "receive packet from interface0, "
                    "start to add time stamp, len=%d, releaseTick=%lu\n",
                    packet->length, releaseTick);
            uint64_t time_stamp;

            // network interface timing params.
            // We have already applied ni transmission delay at transmit
            // function, add ni propagation delay here
            time_stamp = curTick() + niDelay;

            // link timing params.
            // Add link transmission and propagation delay to time_stamp
            // To make sure that we don't reorder packets after adding
            // transmission latency (size/bw), we utilize "releaseTick"
            // variable to serialize outgoing packets.
            // We assume that link bw is not network bottleneck
            Tick link_trans_delay = (Tick)ceil(((double)packet->length
                                               * ticksPerByte) + 1.0);
            if (releaseTick > time_stamp) {
                time_stamp = releaseTick + link_trans_delay + linkDelay;
                releaseTick += link_trans_delay;
            } else {
                releaseTick = time_stamp + link_trans_delay;
                time_stamp += link_trans_delay + linkDelay;
            }
            DPRINTF(Ethernet, "time_stamp=%lu, link_trans_delay=%lu\n",
                    time_stamp, link_trans_delay);
            // attach time_stamp as well as curTick at the end of the packet
            attachTimeStamp(time_stamp);
            // send packet immediately to peer (ethertap)
            txComplete(packet);
            packet = 0;
            assert(!busy());

            txint->sendDone();
            return;
        } else {
            // Otherwise, we should extract time stamp form the incoming
            // packet and send it to peer (nic or switch) at "time_stamp"
            DPRINTF(Ethernet, "receive packet from interface1, "
                    "startng to remove time stamp, len=%d\n", packet->length);
            uint64_t time_stamp;

            time_stamp = getTimeStamp();
            detachTimeStamp();

            if (time_stamp < curTick()) {
                // print a warning message as data packet has received late!
                // you may abort simulation instead of printing warning
                warn("VIOLATION, packet arrived late %lu\n", curTick() -
                     time_stamp);
                // deliver packet instantly to peer
                time_stamp = curTick();
            }

            // Record packet RTT/2 latency for distribution.
            latencyDist.sample(time_stamp - senderTick);

            // Record packet RTT/2 latency for average.
            latencyAverage = time_stamp - senderTick;

            // deliver packet at time_stamp to peer (NIC or Switch)
            Event *event = new LinkDelayEvent(this, packet);
            parent->schedule(event, time_stamp);
            packet = 0;
            assert(!busy());

            txint->sendDone();
            return;
        }
    }
    // We use etherlink in this mode to connect sw to ethertap
    if (mode == 3) {
        // We assume that int0 is connected to sw
        // We should timestamp packets that we recieve form int0 before
        // sending them to peer (EtherTap)
        if (name().find(".link0") != std::string::npos) {
            DPRINTF(Ethernet, "receive packet from interface0, "
                    "start to add time stamp, len=%d, releaseTick=%lu\n",
                    packet->length, releaseTick);
            uint64_t time_stamp;

            // Link Timing params.
            // We have already applied link transmission delay at transmit
            // function, add link propagation delay here
            time_stamp = curTick() + linkDelay;

            // attach time stamp to the packet
            attachTimeStamp(time_stamp, false);

            // send packet immediately to peer (EtherTap)
            txComplete(packet);
            packet = 0;
            assert(!busy());

            txint->sendDone();
            return;
        }
        // Otherwise, we should extract time stamp form the incoming packet
        // and send it to peer (switch) at "time_stamp"
        else {
            DPRINTF(Ethernet, "receive packet from interface1, "
                    "startng to remove time stamp, len=%d\n", packet->length);
            uint64_t time_stamp;

            time_stamp = getTimeStamp();
            // just detach time stamp, not sender tick (cur_tick)
            detachTimeStamp(false);

            if (time_stamp < curTick()) {
                // print a warning message as data packet has received late!
                warn("VIOLATION, packet arrived late %lu\n", curTick() -
                     time_stamp);
                time_stamp = curTick();
            }
            // deliver packet at time_stamp to peer (Switch)
            Event *event = new LinkDelayEvent(this, packet);
            parent->schedule(event, time_stamp);
            packet = 0;
            assert(!busy());

            txint->sendDone();
            return;
        }
    }
    // etherlink defualt functionality
    if (linkDelay > 0) {
        DPRINTF(Ethernet, "packet delayed: delay=%d\n", linkDelay);
        Event *event = new LinkDelayEvent(this, packet);
        parent->schedule(event, curTick() + linkDelay);
    } else {
        txComplete(packet);
    }

    packet = 0;
    assert(!busy());

    txint->sendDone();
}

bool
EtherLink::Link::transmit(EthPacketPtr pkt)
{
    if (busy()) {
        DPRINTF(Ethernet, "packet not sent, link busy\n");
        return false;
    }

    DPRINTF(Ethernet, "packet sent: len=%d, mode=%d\n", pkt->length, mode);
    DDUMP(EthernetData, pkt->data, pkt->length);

    packet = pkt;
    // mode = 1 is 'pd-gem5 connector' mode
    // We use etherlink in this mode to connect nic or switch port to ethertap
    if (mode == 1) {
        // We assume that int0 is connected to nic/switch and int1 to ethertap
        // If we recieve packet form int1 (ethertap), then we should call
        // txDone instantly
        if (name().find(".link1") != std::string::npos) {
            txDone();
            return true;
        }
        // dump packets before applying network interface (ni) timing
        if (dump)
            dump->dump(packet);

        // If we recieve packets form int0, we should apply ni's
        // transmission delay, and keep link busy during that period
        Tick trans_delay = (Tick)ceil(((double)pkt->length * niTicksPerByte)
                                      + 1.0);
        if (delayVar != 0)
            trans_delay += random_mt.random<Tick>(0, delayVar);
        DPRINTF(Ethernet, "scheduling packet: ni_delay=%d, (ni_rate=%f)\n",
                trans_delay, niTicksPerByte);
        parent->schedule(doneEvent, curTick() + trans_delay);
        return true;
    }
    // mode = 3 is 'switch connector' mode
    // We use etherlink in this mode to connect switch ports to EtherTap
    if (mode == 3) {
        // We assume that int0 is connected to switch and int1 to EtherTap
        // If we recieve packet form int1 (EtherTap), then we should call
        // txDone instantly
        if (name().find(".link1") != std::string::npos) {
            txDone();
            return true;
        }

        // If we recieve packets form int0, we should apply link transmission
        // delay, and keep link busy during that period
        Tick trans_delay = (Tick)ceil(((double)pkt->length * ticksPerByte)
                                      + 1.0);
        if (delayVar != 0)
            trans_delay += random_mt.random<Tick>(0, delayVar);
        DPRINTF(Ethernet, "scheduling packet: ni_delay=%d, (ni_rate=%f)\n",
                trans_delay, niTicksPerByte);
        parent->schedule(doneEvent, curTick() + trans_delay);
        return true;
    }
    // etherlink defualt functionality
    Tick delay = (Tick)ceil(((double)pkt->length * ticksPerByte) + 1.0);
    if (delayVar != 0)
        delay += random_mt.random<Tick>(0, delayVar);

    DPRINTF(Ethernet, "scheduling packet: delay=%d, (rate=%f)\n",
            delay, ticksPerByte);
    parent->schedule(doneEvent, curTick() + delay);

    return true;
}

void
EtherLink::Link::serialize(const string &base, ostream &os)
{
    bool packet_exists = packet != nullptr;
    paramOut(os, base + ".packet_exists", packet_exists);
    if (packet_exists)
        packet->serialize(base + ".packet", os);

    bool event_scheduled = doneEvent.scheduled();
    paramOut(os, base + ".event_scheduled", event_scheduled);
    if (event_scheduled) {
        Tick event_time = doneEvent.when();
        paramOut(os, base + ".event_time", event_time);
    }

}

void
EtherLink::Link::unserialize(const string &base, Checkpoint *cp,
                             const string &section)
{
    bool packet_exists;
    paramIn(cp, section, base + ".packet_exists", packet_exists);
    if (packet_exists) {
        packet = make_shared<EthPacketData>(16384);
        packet->unserialize(base + ".packet", cp, section);
    }

    bool event_scheduled;
    paramIn(cp, section, base + ".event_scheduled", event_scheduled);
    if (event_scheduled) {
        Tick event_time;
        paramIn(cp, section, base + ".event_time", event_time);
        parent->schedule(doneEvent, event_time);
    }
}

// this function attaches curTick and a time_stamp to the packet
void
EtherLink::Link::attachTimeStamp(uint64_t time_stamp, bool attach_cur_tick)
{
    char buff[10000];
    int data_len = packet->length;

    // copy packet data into buff
    memcpy(buff, packet->data, data_len);
    // attach both curTick and time_stamp if attach_curTick == True
    if (attach_cur_tick) {
        Tick cur_tick = curTick();

        // add curTick and time_stamp at the end of buff
        memmove(buff + data_len, &cur_tick, sizeof(uint64_t));
        memmove(buff + data_len + sizeof(uint64_t), &time_stamp,
                sizeof(uint64_t));
        // create a new time stamped packet
        packet = make_shared<EthPacketData>(data_len + 2 * sizeof(uint64_t));
        packet->length = data_len + 2 * sizeof(uint64_t);
        // set packet->data
        memcpy(packet->data, buff, data_len + 2 * sizeof(uint64_t));
    } else {
        // add time_stamp at the end of buff
        memmove(buff + data_len, &time_stamp, sizeof(uint64_t));
        // create a new time stamped packet
        packet = make_shared<EthPacketData>(data_len + sizeof(uint64_t));
        packet->length = data_len + sizeof(uint64_t);
        // set packet->data
        memcpy(packet->data, buff, data_len + sizeof(uint64_t));
    }

    DPRINTF(Ethernet, "added %lu as time stamp, len=%d\n",
            time_stamp, packet->length);
}

// This function detaches the time stamp of the packet in the link
void
EtherLink::Link::detachTimeStamp(bool detach_cur_tick)
{
    char buff[10000];
    int data_len = packet->length;

    // get a backup from packet data
    memcpy(buff, packet->data, data_len);
    if (detach_cur_tick) {
        // generate a new packet without timestamp
        packet = make_shared<EthPacketData>(data_len - 2 * sizeof(uint64_t));
        packet->length = data_len - 2 * sizeof(uint64_t);
        memcpy(packet->data, buff, data_len - 2 * sizeof(uint64_t));
    } else {
        // generate a new packet without timestamp
        packet = make_shared<EthPacketData>(data_len - sizeof(uint64_t));
        packet->length = data_len - sizeof(uint64_t);
        memcpy(packet->data, buff, data_len - sizeof(uint64_t));
    }
}

void
EtherLink::Link::setTimeStamp(uint64_t time_stamp)
{
    int data_len = packet->length;
    DPRINTF(Ethernet, "set packet time stamp to %lu\n", time_stamp);
    memmove(packet->data + data_len - sizeof(int64_t), &time_stamp,
           sizeof(uint64_t));
}

// This function returns thetime stamp of the packet in the link
// it also sets senderTick member variable
uint64_t
EtherLink::Link::getTimeStamp()
{
    Tick time_stamp;
    int data_len = packet->length;
    memmove(&senderTick, packet->data + data_len - 2 * sizeof(uint64_t),
            sizeof(uint64_t));
    memmove(&time_stamp, packet->data + data_len - sizeof(uint64_t),
            sizeof(uint64_t));
    DPRINTF(Ethernet, "removed time stamp %lu, senderTick %lu, len=%d\n",
            time_stamp, senderTick, packet->length);

    return time_stamp;
}

void
EtherLink::Link::regStats()
{
    latencyAverage
        .name(name() + ".avglatency")
        .desc("Average latency")
        .precision(1);

    latencyDist
        .init(/* base value */ 0,
              /* last value */ 10000000000, // 10ms
              /* bucket size */ 100000000 )       // 100us
        .name(name() + ".latencyDist")
        .desc("Packet latency (Total)")
        .flags(Stats::pdf);
}

LinkDelayEvent::LinkDelayEvent()
    : Event(Default_Pri, AutoSerialize | AutoDelete), link(NULL)
{
}

LinkDelayEvent::LinkDelayEvent(EtherLink::Link *l, EthPacketPtr p)
    : Event(Default_Pri, AutoSerialize | AutoDelete), link(l), packet(p)
{
}

void
LinkDelayEvent::process()
{
    link->txComplete(packet);
}

void
LinkDelayEvent::serialize(ostream &os)
{
    paramOut(os, "type", string("LinkDelayEvent"));
    Event::serialize(os);

    EtherLink *parent = static_cast<EtherLink*>(link->parent);
    bool number = link->number;
    SERIALIZE_OBJPTR(parent);
    SERIALIZE_SCALAR(number);

    packet->serialize("packet", os);
}


void
LinkDelayEvent::unserialize(Checkpoint *cp, const string &section,
                            EventQueue *eventq)
{
    Event::unserialize(cp, section, eventq);

    EtherLink *parent;
    bool number;
    UNSERIALIZE_OBJPTR(parent);
    UNSERIALIZE_SCALAR(number);

    link = static_cast<EtherLink*>(parent)->link[number];

    packet = make_shared<EthPacketData>(16384);
    packet->unserialize("packet", cp, section);
}


Serializable *
LinkDelayEvent::createForUnserialize(Checkpoint *cp, const string &section)
{
    return new LinkDelayEvent();
}

REGISTER_SERIALIZEABLE("LinkDelayEvent", LinkDelayEvent)

EtherLink *
EtherLinkParams::create()
{
    return new EtherLink(this);
}
