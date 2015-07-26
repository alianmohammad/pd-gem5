# This is an example of an n port network switch (star topology) to work in
# pd-gem5. Users can extend this to have different different topologies
#
# Author: Mohammad Alian
import optparse
import sys

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal

addToPath('../common')

import Simulation
import Options

def build_switch(options):
    # instantiate an EtherSwitch with "num_node" ports. Also pass along
    # timing parameters
    switch = EtherSwitch(port_count = options.num_nodes,
                         delay = options.sw_delay)
    # instantiate etherlinks to connect switch box ports to ethertap objects
    switch.portlink = [EtherLink(mode = 3,
                                 delay = options.etherlink_delay,
                                 speed = options.etherlink_speed,
                                 ni_speed = options.sw_speed)
                       for i in xrange(options.num_nodes)]

    # instantiate ethertap objects in server mode
    switch.porttap = [EtherTap(server = True,
                               poll_rate = int(options.sync.split(',')[0])/2)
                      for i in xrange(options.num_nodes)]
    for (i, link) in enumerate(switch.portlink):
        link.int1 = switch.porttap[i].tap
        link.int0 = switch.interface[i]

    return switch
# Add options
parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addFSOptions(parser)
(options, args) = parser.parse_args()

system = build_switch(options)
root = Root(full_system = True, system = system)
Simulation.run(options, root, None, None)

