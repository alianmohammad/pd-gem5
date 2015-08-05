#!/usr/bin/python

#############################################################################
# This script launch pd-gem5
# usage: python pd-gem5.py <config file>
#
# Author:
# Mohammad Alian
# alian@wisc.edu
#############################################################################

import sys, os, thread, commands
import socket
import time
import subprocess
import threading
import signal
import fcntl
import struct
import re
import collections
configFile = sys.argv[1]
# A dictionary of parameters
params = {}
machines = {}
# dictionary that maps each simulated node to a physical node
sim_to_phy = {}
# dictionary that maps each subprocess to a simulated node
gem5_processes = {}
this = socket.gethostname()
run_dir = ""
ckpt_dir = ""
script_dir = ""
submit_script = ""
sync_ip = ''
sync_port = 5000
connected_gem5s = -1
sw_ip = ''
sw_port = ''
running = True
monitoring = True
num_nodes = 0

# kill all threads and gem5 processes
def cleanup():
    global running
    # kill barrier, monitor and main threads
    running = False
    time.sleep(1)
    # kill gem5 processes if they are still running
    for p in gem5_processes.keys():
        if type(p) == str:
            submission_line = subprocess.Popen("grep submitted %s/%s/pdgem5sim.log"\
                %(run_dir, gem5_processes[p]), stdout=subprocess.PIPE, stderr=subprocess.PIPE, \
                shell = True).communicate()[0].rstrip()
            if submission_line != '':
                job_id = re.split('\.|\(', submission_line)[1]
                os.system("condor_rm %s > /dev/null" %(job_id))
        else:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except Exception as ex:
                pass
    sys.exit(0)

# submit a job to HTCondor
# MODIFY THIS FUNCTION IF YOU ARE USING OTHER SIMULATION POOL MANAGEMENT SOFTWARE
def submit_to_HTCondor(job_path):
    submit_script =("executable = /bin/sh\n"
        "arguments = %s\n"
        "initialdir = %s\n"
        "output = %s\n"
        "error = %s\n"
        "log = %s\n"
        "Rank = TARGET.Mips\n"
        "Requirements =\n"
        "universe = vanilla\n"
        "getenv = true\n"
        "queue\n")\
        % (job_path + "/job.sh",
           job_path,
           job_path + "/pdgem5sim.out",
           job_path + "/pdgem5sim.err",
           job_path + "/pdgem5sim.log")
    f0 = open(job_path + '/condor.rcS', 'w')
    f0.write(submit_script)
    f0.close()
    command_line = 'condor_submit ' + job_path + '/condor.rcS'
    os.system(command_line)

# returns the ip address of a given network interface
def get_ip_address(ifname):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    return socket.inet_ntoa( fcntl.ioctl(s.fileno(), 0x8915,\
        struct.pack('256s', ifname[:15]))[20:24])

# get the ip address of this machines, we run barrier on this machine
sync_ip = get_ip_address('eth0')

def launch_gem5_process(cmd, sim):
    global gem5_processes
    job_path = run_dir + '/' + sim
    if sim_to_phy[sim] == 'HTCondor':
        f0 = open(job_path + '/job.sh', 'w')
        f0.write('#!/bin/sh\n')
        f0.write(cmd)
        f0.close()
        key = 'HTCondor_' + sim
        gem5_processes[key] = sim
        submit_to_HTCondor(job_path)
    else:
        host_ip = socket.gethostbyname(sim_to_phy[sim])
        command_line = ''
        # if the physical host is localhost then we don't need to ssh
        if (host_ip == '127.0.0.1') or (host_ip == '127.0.1.1') or\
                host_ip == sync_ip:
            command_line = 'cd ' + job_path + ';' + cmd +\
                ' >pdgem5sim.out 2>pdgem5sim.err'
        # we should ssh to machines and launch cmd on them
        else:
            command_line = 'ssh ' + sim_to_phy[sim] + ' \'cd ' + job_path + ';'\
                + cmd + ' >pdgem5sim.out 2>pdgem5sim.err\''
        p = subprocess.Popen(command_line, shell = True, preexec_fn=os.setsid)
        gem5_processes[p] = sim
    tap_connected(sim)

def perpare_dir():
    # clean up the old files
    os.system("rm -rf %s > /dev/null" % (run_dir))

    # make new directories
    os.system("mkdir -p %s > /dev/null 2>&1" %(run_dir))
    # make sure that checkpoint directory exists
    os.system("mkdir -p %s > /dev/null 2>&1" %(params['pd-gem5_dir'] +'/ckptdir/'))
    rel_path = params['ckpt_dir'].split('/')
    ckpt_dir_partial_path = params['pd-gem5_dir'] + '/ckptdir/'
    for dirs in rel_path:
        os.system("mkdir -p %s > /dev/null 2>&1" %(ckpt_dir_partial_path + dirs))
        ckpt_dir_partial_path += dirs + '/'
    # make run directory for each node, make sure that checkpoint dir exists for
    # each node
    for machine in machines:
        (phy, sim) = machine.split(':')
        os.system("mkdir -p %s/%s > /dev/null 2>&1" %(run_dir, sim))
        os.system("mkdir -p %s/%s > /dev/null 2>&1" %((params['pd-gem5_dir'] +\
                '/ckptdir/' + params['ckpt_dir'] + '/', sim)))

# this function prepare gem5 commandline for each simualted node
def prepare_cmd(sims):
    cmd = ''
    cmd_debug = params['pd-gem5_dir'] + '/' + params['gem5_binary'] + ' ' + \
        '--debug-flags=' + params['debug_flags'] + ' '
    if params['debug_start'] != '0':
        cmd_debug += '--debug-start=' + params['debug_start'] + ' '
    cmd_nodebug = params['pd-gem5_dir'] + '/' +  params['gem5_binary'] + ' '

    debug_flag = False
    for sim in sims.split(','):
        if sim in params['trace_on']:
            debug_flag = True
            break
    if debug_flag:
        cmd = cmd_debug + params['pd-gem5_dir'] + '/configs/example/'
    else:
        cmd = cmd_nodebug + params['pd-gem5_dir'] + '/configs/example/'

    # add switch specific options
    if sims.split(',')[0] == 'sw':
        cmd += 'sw.py '
    # add node specific options
    else:
        cmd += params['fs_script'] + ' '\
             + '--switch-tap-ip=' + sw_ip + ' '\
             + '--switch-tap-port=' + sw_port + ' '
        if len(sims.split(',')) == 1:
            cmd += '--eth '
        # right now we just support quad or mono
        else:
            cmd += '--quad '
        if params['script_dir'] != '0':
            if len(sims.split(',')) == 4:
                if sims.split(',')[0] != 'tux0':
                    cmd += '--script=' + script_dir + '/' + sims.split(',')[0] + '.sh '\
                        + '--script1=' + script_dir + '/' + sims.split(',')[1] + '.sh '\
                        + '--script2=' + script_dir + '/' + sims.split(',')[2] + '.sh '\
                        + '--script3=' + script_dir + '/' + sims.split(',')[3] + '.sh '
                else:
                    cmd += '--script=' + params['script_tux0'] + ' '\
                        + '--script1=' + script_dir + '/' + sims.split(',')[1] + '.sh '\
                        + '--script2=' + script_dir + '/' + sims.split(',')[2] + '.sh '\
                        + '--script3=' + script_dir + '/' + sims.split(',')[3] + '.sh '
            else:
                if sims.split(',')[0] != 'tux0':
                    cmd += '--script=' + script_dir + '/' + sims.split(',')[0] + '.sh '
                else:
                    cmd += '--script=' + params['script_tux0'] + '.sh '
    # add common options for both switch and nodes
    cmd += '--checkpoint-dir=' + ckpt_dir + '/' + sims + ' '\
           + params['other_command_line_options'] + ' '\
           '--sync=' + params['sync_period'] + '000000' + ','\
           + str(sync_port) + ',' + sync_ip + ' '\
           + '--num-nodes=' + str(num_nodes - 1) + ' '\
           + '--nic-delay=' + params['nic_delay'] + ' '\
           + '--nic-speed=' + params['nic_speed'] + ' '\
           + '--etherlink-delay=' + params['link_delay'] + ' '\
           + '--etherlink-speed=' + params['link_speed'] + ' '\
           + '--sw-delay=' + params['sw_delay'] + ' '\
           + '--sw-speed=' + params['sw_speed'] + ' '
    if 'etherdump' in params.keys():
        cmd += '--etherdump=' + run_dir + '/' + sims + '/etherdump.pcap '
    return cmd

# barrier thread
# employ fixed quantum synchronization mechanism
def barrier():

    global sync_port
    global monitoring
    global connected_gem5s
    global num_nodes
    c_list=[]
    BUFFER_SIZE = 1

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    while running:
        try:
            s.bind((sync_ip, sync_port))
            break
        except Exception as ex:
            if "Address already in use" in ex.args:
                print "%d already in use, try %d" %(sync_port, sync_port+1)
                sync_port += 1
                continue
            print "<ERROR> cannot bind to socket"
            print "exception %s occured." %(ex.args)
            cleanup()
            return

    s.listen(10)
    try:
        for i in range(0, num_nodes):
            conn, addr = s.accept()
            c_list.append(conn)
            connected_gem5s = i
            print "gem5 " + str(i) + "connected."

        print "pd-gem5 started ..."

        # kill monitor thread as we don't need it anymore
        monitoring = False
        checkpoint = 0
        # this is the main synchronization loop which send/receive sync messages
        while running:
            checkpoint = 0
            for i in range (0, num_nodes):
                if(c_list[i].recv(BUFFER_SIZE) == "C"):
                    checkpoint = 1
            for i in range (0, num_nodes):
                if checkpoint == 1:
                    c_list[i].send('C')
                else:
                    c_list[i].send('R')
    except Exception as ex:
        conn.close()
        time.sleep(2)
        # print nodes that went down and caused the exception
        check_nodes_status()
        cleanup()
        return

def get_sw_ip_port():
    tap_line = ''
    # wait till sw gem5 process updates the content of it's log file
    while tap_line == '':
        tap_line = subprocess.Popen("grep tap %s/sw/pdgem5sim.err"\
                    %(run_dir), stdout=subprocess.PIPE,\
                    shell = True).communicate()[0].rstrip()
    sw_ip = tap_line.split(' ')[6]
    sw_port = tap_line.split(' ')[7].rstrip()
    return sw_ip, sw_port

def tap_connected(sim):
    line = ''
    while line == '' and running:
        line = subprocess.Popen("grep Listening %s/%s/pdgem5sim.err"\
                    %(run_dir, sim), stdout=subprocess.PIPE,\
                    shell = True).communicate()[0].rstrip()

def check_nodes_status():
    flag = False
    for p in gem5_processes.keys():
        # type of p is "strting" if gem5 process is launched using HTCondor
        if type(p) == str:
            submission_line = ''
            # wait till gem5 process updates the content of it's log file
            while submission_line == '':
                submission_line = subprocess.Popen("grep submitted %s/%s/pdgem5sim.log"\
                    %(run_dir, gem5_processes[p]), stdout=subprocess.PIPE, stderr=subprocess.PIPE, \
                    shell = True).communicate()[0].rstrip()
                time.sleep(1)

            job_id = re.split('\.|\(', submission_line)[1]
            job_status = subprocess.Popen("condor_q %s | grep %s" %(job_id, job_id),
                stdout=subprocess.PIPE, shell = True).communicate()[0].rstrip()
            if job_status == '':
                print ("gem5 process simulating %s is finished/killed. Please"
                        " check %s/%s/pdgem5sim.err(out) for more information")\
                        %(gem5_processes[p], run_dir, gem5_processes[p])
                flag = True
        else:
            # poll output is None if process is still running
            if p.poll() != None:
                print ("gem5 process simulating %s is finished/killed. Please"
                        " check %s/%s/pdgem5sim.err(out) for more information")\
                        %(gem5_processes[p], run_dir, gem5_processes[p])
                flag = True
    return flag

def monitor_cluster():
    while monitoring:
        time.sleep(1)
        if check_nodes_status():
            cleanup()
            return

# read confing file and extract the parmeters
configCont = open(configFile,'r').readlines()

for i in configCont:
    if len(i) > 1 and not i.strip()[0] == '#':
        iSplit = map(lambda x:x.strip(), i.split('=')[1:])
        params[i.split('=')[0]] = '='.join(iSplit)

print "preparing to start pd-gem5"
print "configuration file = " + params['run_name']

run_dir = params['run_dir'] + '/' + params['run_name']
ckpt_dir = params['pd-gem5_dir'] + '/ckptdir/' + params['ckpt_dir']
script_dir = params['pd-gem5_dir'] + '/scriptdir/' + params['script_dir']
machines = params['machine_names'].split(' ')
num_nodes = len(machines)

for machine in machines:
    (phy, sim) = machine.split(':')
    #num_nodes += len(sim.split(','))
    # right now we just support quad or mono
    if (len(sim.split(',')) != 1) and (len(sim.split(',')) != 4):
        print ("right now we just support quad or mono simulation!"\
        "please modify machine_names config\n")
        running = False
        monitoring = False
        sys.exit(0)
    sim_to_phy[sim] = phy

# clean up previous directories, create new ones
perpare_dir()

# take a copy of "config file" to run_dir
os.system("cp %s %s"%(configFile,run_dir))

# start barrier
# create the barrier socket and listen for connection form gem5 processes
threads = []
t_barrier = threading.Thread(name='barrier_thread', target = barrier)
t_barrier.daemon = True
threads.append(t_barrier)
t_barrier.start()

# start monitoring the progress of launched gem5 processes and kill simulation
# if any of them encountered any problem
t_monitor = threading.Thread(name='monitor_thread', target = monitor_cluster)
t_monitor.daemon = True
threads.append(t_monitor)
t_monitor.start()

cmds = {}
# prepare cmd for switch
cmds['sw'] = prepare_cmd('sw')

launch_gem5_process(cmds['sw'], 'sw')

# wait for the first gem5 process (switch) to connect to barrier
while connected_gem5s == -1:
    if running:
        time.sleep(1)
    else:
        sys.exit(0)

# get ip address of the physical machine that is simulating switch + the port
# number of the switch tap device
sw_ip, sw_port = get_sw_ip_port()

sim_to_phy_ordered = collections.OrderedDict(sorted(sim_to_phy.items()))
# prepare cmd for nodes and start them
for sim in sim_to_phy_ordered.keys():
    if sim != 'sw':
        cmds[sim] = prepare_cmd(sim)
        launch_gem5_process(cmds[sim], sim)

# busy loop till simulation finish OR keyboard interrupt receive
while running:
    try:
        time.sleep(1)
    except KeyboardInterrupt:
        print "CTRL+C pressed.\nLet's cleanup and exit pd-gem5 ..."
        cleanup()
