#include "dragon_fly_plus_topology.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "string.h"
#include <sstream>
#include <iostream>
#include "main.h"
#include "queue.h"
#include "switch.h"
#include "compositequeue.h"
#include "prioqueue.h"
#include "queue_lossless.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "ecnqueue.h"

string ntoa(double n);
string itoa(uint64_t n);

class TerminalPacketSink : public PacketSink {
public:
    TerminalPacketSink(const string& name) : _name(name) {}
    
    virtual void receivePacket(Packet& pkt) {
        // Terminal sink - packet is consumed here
        pkt.free();
    }
    
    virtual void receivePacket(Packet& pkt, VirtualQueue* q) {
        // Terminal sink - packet is consumed here
        pkt.free();
    }
    
    virtual const string& nodename() { return _name; }

private:
    string _name;
};

DragonFlyPlusTopology::DragonFlyPlusTopology(uint32_t p, uint32_t h, uint32_t a, uint32_t h_plus,
                                           mem_b queuesize, Logfile* lg, EventList* ev,
                                           queue_type q, simtime_picosec rtt) {
    _queuesize = queuesize;
    logfile = lg;
    _eventlist = ev;
    qt = q;
    _rtt = rtt;
    
    _p = p;
    _a = a;
    _h = h;
    _h_plus = h_plus;
    
    _no_of_nodes = _a * _p * (_a * _h + 1);
    
    cout << "DragonFly Plus topology with " << _p << " hosts per router, "
         << _a << " routers per group, " << _h << " global links per router, "
         << _h_plus << " additional global links per router, and "
         << (_a * _h + 1) << " groups, total nodes " << _no_of_nodes << endl;
    cout << "Queue type " << qt << endl;
    
    set_params();
    init_network();
}

DragonFlyPlusTopology::DragonFlyPlusTopology(uint32_t no_of_nodes, mem_b queuesize,
                                           Logfile* lg, EventList* ev, queue_type q,
                                           simtime_picosec rtt) {
    _queuesize = queuesize;
    logfile = lg;
    _eventlist = ev;
    qt = q;
    _rtt = rtt;
    
    set_params(no_of_nodes);
    init_network();
}

void DragonFlyPlusTopology::set_params(uint32_t no_of_nodes) {
    cout << "Set params " << no_of_nodes << endl;
    cout << "QueueSize " << _queuesize << endl;
    _no_of_nodes = 0;
    _h = 0;
    
    // Find the smallest h that gives us enough nodes
    while (_no_of_nodes < no_of_nodes) {
        _h++;
        _p = _h;           // p = h for balanced configuration
        _a = 2 * _h;       // a = 2h for balanced configuration
        _h_plus = _h / 2;  // h' = h/2 for balanced configuration
        _no_of_nodes = _a * _p * (_a * _h + 1);
    }
    
    if (_no_of_nodes > no_of_nodes) {
        cerr << "Topology Error: can't have a DragonFly Plus with " << no_of_nodes
             << " nodes\n";
        exit(1);
    }
    
    set_params();
}

void DragonFlyPlusTopology::set_params() {
    _no_of_groups = _a * _h + 1;
    _no_of_switches = _no_of_groups * _a;
    
    cout << "DragonFly Plus topology with " << _p << " hosts per router, "
         << _a << " routers per group, " << _h << " global links per router, "
         << _h_plus << " additional global links per router, and "
         << _no_of_groups << " groups, total nodes " << _no_of_nodes << endl;
    cout << "Queue type " << qt << endl;
    
    // Initialize vectors
    switches.resize(_no_of_switches, NULL);
    
    pipes_host_switch.resize(_no_of_nodes, vector<Pipe*>(_no_of_switches));
    queues_host_switch.resize(_no_of_nodes, vector<Queue*>(_no_of_switches));
    
    pipes_switch_host.resize(_no_of_switches, vector<Pipe*>(_no_of_nodes));
    queues_switch_host.resize(_no_of_switches, vector<Queue*>(_no_of_nodes));
    
    pipes_switch_switch.resize(_no_of_switches, vector<Pipe*>(_no_of_switches));
    queues_switch_switch.resize(_no_of_switches, vector<Queue*>(_no_of_switches));
    
    pipes_global_plus.resize(_no_of_switches, vector<Pipe*>(_no_of_switches));
    queues_global_plus.resize(_no_of_switches, vector<Queue*>(_no_of_switches));
}

Queue* DragonFlyPlusTopology::alloc_src_queue(QueueLogger* queueLogger) {
    if (qt == LOSSLESS || qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
        return new LosslessQueue(speedFromMbps((uint64_t)HOST_NIC), memFromPkt(50),
                               *_eventlist, queueLogger, switches[0]);
    } else {
        return new FairPriorityQueue(speedFromMbps((uint64_t)HOST_NIC),
                                   memFromPkt(FEEDER_BUFFER),
                                   *_eventlist, queueLogger);
    }
}

Queue* DragonFlyPlusTopology::alloc_queue(QueueLogger* queueLogger, mem_b queuesize, bool tor) {
    return alloc_queue(queueLogger, HOST_NIC, queuesize, tor);
}

Queue* DragonFlyPlusTopology::alloc_queue(QueueLogger* queueLogger, uint64_t speed, mem_b queuesize, bool tor) {
    if (qt == RANDOM)
        return new RandomQueue(speedFromMbps(speed), queuesize, *_eventlist,
                             queueLogger, memFromPkt(RANDOM_BUFFER));
    else if (qt == COMPOSITE)
        return new CompositeQueue(speedFromMbps(speed), queuesize,
                                *_eventlist, queueLogger);
    else if (qt == CTRL_PRIO)
        return new PriorityQueue(speedFromMbps(speed), queuesize,
                               *_eventlist, queueLogger);
    else if (qt == ECN)
        return new ECNQueue(speedFromMbps(speed), queuesize, *_eventlist,
                          queueLogger, memFromPkt(15));
    else if (qt == LOSSLESS)
        return new LosslessQueue(speedFromMbps(speed), memFromPkt(50),
                               *_eventlist, queueLogger, switches[0]);
    else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN)
        return new LosslessOutputQueue(speedFromMbps(speed), memFromPkt(200),
                                     *_eventlist, queueLogger);
    else if (qt == COMPOSITE_ECN) {
        if (tor)
            return new CompositeQueue(speedFromMbps(speed), queuesize,
                                    *_eventlist, queueLogger);
        else
            return new ECNQueue(speedFromMbps(speed), memFromPkt(2*SWITCH_BUFFER),
                              *_eventlist, queueLogger, memFromPkt(15));
    }
    
    assert(0);
    return NULL;
}

void DragonFlyPlusTopology::count_queue(Queue* queue) {
    if (queue == NULL)
        return;
    
    if (qt == LOSSLESS || qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
        queue->setName("LosslessQueue_" + to_string(rand()));
    }
    
    QueueLoggerSampling* queueLogger = new QueueLoggerSampling(timeFromMs(1000),
                                                              *_eventlist);
    queue->setLogger(queueLogger);
    logfile->addLogger(*queueLogger);
}

void DragonFlyPlusTopology::init_network() {
    QueueLoggerSampling* queueLogger;
    
    // Initialize all arrays to NULL
    for (uint32_t j = 0; j < _no_of_switches; j++) {
        for (uint32_t k = 0; k < _no_of_switches; k++) {
            queues_switch_switch[j][k] = NULL;
            pipes_switch_switch[j][k] = NULL;
            queues_global_plus[j][k] = NULL;
            pipes_global_plus[j][k] = NULL;
        }
        
        for (uint32_t k = 0; k < _no_of_nodes; k++) {
            queues_switch_host[j][k] = NULL;
            pipes_switch_host[j][k] = NULL;
            queues_host_switch[k][j] = NULL;
            pipes_host_switch[k][j] = NULL;
        }
    }
    
    // Create switches if we have lossless operation
    if (qt == LOSSLESS || qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
        for (uint32_t j = 0; j < _no_of_switches; j++) {
            switches[j] = new Switch(*_eventlist, "Switch_" + ntoa(j));
        }
    }
    
    // Create host-switch connections
    for (uint32_t j = 0; j < _no_of_switches; j++) {
        for (uint32_t l = 0; l < _p; l++) {
            uint32_t k = j * _p + l;  // Host ID
            
            // Downlink (Switch -> Host)
            queueLogger = new QueueLoggerSampling(timeFromUs((uint32_t)10), *_eventlist);
            logfile->addLogger(*queueLogger);
            
            queues_switch_host[j][k] = alloc_queue(queueLogger, _queuesize, true);
            queues_switch_host[j][k]->setName("SW" + ntoa(j) + "->DST" + ntoa(k));
            logfile->writeName(*(queues_switch_host[j][k]));
            
            pipes_switch_host[j][k] = new Pipe(_rtt, *_eventlist);
            pipes_switch_host[j][k]->setName("Pipe-SW" + ntoa(j) + "->DST" + ntoa(k));
            logfile->writeName(*(pipes_switch_host[j][k]));
            
            // Uplink (Host -> Switch)
            queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
            logfile->addLogger(*queueLogger);
            
            queues_host_switch[k][j] = alloc_src_queue(queueLogger);
            queues_host_switch[k][j]->setName("SRC" + ntoa(k) + "->SW" + ntoa(j));
            logfile->writeName(*(queues_host_switch[k][j]));
            
            if (qt == LOSSLESS) {
                switches[j]->addPort(queues_switch_host[j][k]);
                ((LosslessQueue*)queues_switch_host[j][k])->setRemoteEndpoint(queues_host_switch[k][j]);
            } else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                new LosslessInputQueue(*_eventlist, queues_host_switch[k][j]);
            }
            
            pipes_host_switch[k][j] = new Pipe(_rtt, *_eventlist);
            pipes_host_switch[k][j]->setName("Pipe-SRC" + ntoa(k) + "->SW" + ntoa(j));
            logfile->writeName(*(pipes_host_switch[k][j]));
        }
    }
    
    // Create intra-group connections (full mesh within group)
    for (uint32_t j = 0; j < _no_of_switches; j++) {
        uint32_t groupid = j / _a;
        
        for (uint32_t k = j + 1; k < (groupid + 1) * _a; k++) {
            // Bidirectional links within group
            queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
            logfile->addLogger(*queueLogger);
            
            queues_switch_switch[j][k] = alloc_queue(queueLogger, _queuesize, false);
            queues_switch_switch[j][k]->setName("SW" + ntoa(j) + "->SW" + ntoa(k));
            logfile->writeName(*(queues_switch_switch[j][k]));
            
            queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
            logfile->addLogger(*queueLogger);
            
            queues_switch_switch[k][j] = alloc_queue(queueLogger, _queuesize, false);
            queues_switch_switch[k][j]->setName("SW" + ntoa(k) + "->SW" + ntoa(j));
            logfile->writeName(*(queues_switch_switch[k][j]));
            
            if (qt == LOSSLESS) {
                switches[j]->addPort(queues_switch_switch[j][k]);
                switches[k]->addPort(queues_switch_switch[k][j]);
                ((LosslessQueue*)queues_switch_switch[j][k])->setRemoteEndpoint(queues_switch_switch[k][j]);
                ((LosslessQueue*)queues_switch_switch[k][j])->setRemoteEndpoint(queues_switch_switch[j][k]);
            } else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                new LosslessInputQueue(*_eventlist, queues_switch_switch[j][k]);
                new LosslessInputQueue(*_eventlist, queues_switch_switch[k][j]);
            }
            
            pipes_switch_switch[j][k] = new Pipe(_rtt, *_eventlist);
            pipes_switch_switch[j][k]->setName("Pipe-SW" + ntoa(j) + "->SW" + ntoa(k));
            logfile->writeName(*(pipes_switch_switch[j][k]));
            
            pipes_switch_switch[k][j] = new Pipe(_rtt, *_eventlist);
            pipes_switch_switch[k][j]->setName("Pipe-SW" + ntoa(k) + "->SW" + ntoa(j));
            logfile->writeName(*(pipes_switch_switch[k][j]));
        }
    }
    
    // Create global connections between groups
    for (uint32_t g1 = 0; g1 < _no_of_groups; g1++) {
        for (uint32_t g2 = g1 + 1; g2 < _no_of_groups; g2++) {
            for (uint32_t i = 0; i < _h; i++) {
                uint32_t src = g1 * _a + i;
                uint32_t dst = g2 * _a + i;
                
                // Bidirectional global links
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
                logfile->addLogger(*queueLogger);
                
                queues_switch_switch[src][dst] = alloc_queue(queueLogger, _queuesize, false);
                queues_switch_switch[src][dst]->setName("SW" + ntoa(src) + "->SW" + ntoa(dst) + "(Global)");
                logfile->writeName(*(queues_switch_switch[src][dst]));
                
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
                logfile->addLogger(*queueLogger);
                
                queues_switch_switch[dst][src] = alloc_queue(queueLogger, _queuesize, false);
                queues_switch_switch[dst][src]->setName("SW" + ntoa(dst) + "->SW" + ntoa(src) + "(Global)");
                logfile->writeName(*(queues_switch_switch[dst][src]));
                
                if (qt == LOSSLESS) {
                    switches[src]->addPort(queues_switch_switch[src][dst]);
                    switches[dst]->addPort(queues_switch_switch[dst][src]);
                    ((LosslessQueue*)queues_switch_switch[src][dst])->setRemoteEndpoint(queues_switch_switch[dst][src]);
                    ((LosslessQueue*)queues_switch_switch[dst][src])->setRemoteEndpoint(queues_switch_switch[src][dst]);
                } else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    new LosslessInputQueue(*_eventlist, queues_switch_switch[src][dst]);
                    new LosslessInputQueue(*_eventlist, queues_switch_switch[dst][src]);
                }
                
                pipes_switch_switch[src][dst] = new Pipe(_rtt, *_eventlist);
                pipes_switch_switch[src][dst]->setName("Pipe-SW" + ntoa(src) + "->SW" + ntoa(dst) + "(Global)");
                logfile->writeName(*(pipes_switch_switch[src][dst]));
                
                pipes_switch_switch[dst][src] = new Pipe(_rtt, *_eventlist);
                pipes_switch_switch[dst][src]->setName("Pipe-SW" + ntoa(dst) + "->SW" + ntoa(src) + "(Global)");
                logfile->writeName(*(pipes_switch_switch[dst][src]));
            }
        }
    }
    
    // Create additional global links (Plus enhancement)
    for (uint32_t g1 = 0; g1 < _no_of_groups; g1++) {
        for (uint32_t g2 = g1 + 1; g2 < _no_of_groups; g2++) {
            for (uint32_t i = 0; i < _h_plus; i++) {
                uint32_t src = g1 * _a + i;
                uint32_t dst = g2 * _a + (_a - i - 1);  // Connect to complementary switch
                
                // Bidirectional additional global links
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
                logfile->addLogger(*queueLogger);
                
                queues_global_plus[src][dst] = alloc_queue(queueLogger, _queuesize, false);
                queues_global_plus[src][dst]->setName("SW" + ntoa(src) + "->SW" + ntoa(dst) + "(Plus)");
                logfile->writeName(*(queues_global_plus[src][dst]));
                
                queueLogger = new QueueLoggerSampling(timeFromMs(1000), *_eventlist);
                logfile->addLogger(*queueLogger);
                
                queues_global_plus[dst][src] = alloc_queue(queueLogger, _queuesize, false);
                queues_global_plus[dst][src]->setName("SW" + ntoa(dst) + "->SW" + ntoa(src) + "(Plus)");
                logfile->writeName(*(queues_global_plus[dst][src]));
                
                if (qt == LOSSLESS) {
                    switches[src]->addPort(queues_global_plus[src][dst]);
                    switches[dst]->addPort(queues_global_plus[dst][src]);
                    ((LosslessQueue*)queues_global_plus[src][dst])->setRemoteEndpoint(queues_global_plus[dst][src]);
                    ((LosslessQueue*)queues_global_plus[dst][src])->setRemoteEndpoint(queues_global_plus[src][dst]);
                } else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    new LosslessInputQueue(*_eventlist, queues_global_plus[src][dst]);
                    new LosslessInputQueue(*_eventlist, queues_global_plus[dst][src]);
                }
                
                pipes_global_plus[src][dst] = new Pipe(_rtt, *_eventlist);
                pipes_global_plus[src][dst]->setName("Pipe-SW" + ntoa(src) + "->SW" + ntoa(dst) + "(Plus)");
                logfile->writeName(*(pipes_global_plus[src][dst]));
                
                pipes_global_plus[dst][src] = new Pipe(_rtt, *_eventlist);
                pipes_global_plus[dst][src]->setName("Pipe-SW" + ntoa(dst) + "->SW" + ntoa(src) + "(Plus)");
                logfile->writeName(*(pipes_global_plus[dst][src]));
            }
        }
    }
}

bool DragonFlyPlusTopology::is_valid_route(Route* route) {
    if (!route || route->size() == 0) {
        return false;
    }
    
    // Check that each component in the route is valid
    for (size_t i = 0; i < route->size(); i++) {
        if (!route->at(i)) {
            return false;
        }
        
        // For queues, verify they have valid remote endpoints if required
        if (i % 2 == 0 && dynamic_cast<Queue*>(route->at(i))) {
            Queue* q = dynamic_cast<Queue*>(route->at(i));
            if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                if (!q->getRemoteEndpoint()) {
                    return false;
                }
            }
        }
    }
    
    // Verify route structure: [Queue, Pipe, (Remote), Queue, Pipe, ...]
    for (size_t i = 0; i < route->size(); i++) {
        if (i % 2 == 0) {
            if (!dynamic_cast<Queue*>(route->at(i)) && 
                !dynamic_cast<PacketSink*>(route->at(i))) {
                return false;
            }
        } else {
            if (!dynamic_cast<Pipe*>(route->at(i))) {
                return false;
            }
        }
    }
    
    // Verify bidirectional route setup
    if (!route->reverse()) {
        return false;
    }
    
    // Verify the reverse route points back to this route
    if (route->reverse()->reverse() != route) {
        return false;
    }
    
    // Verify path IDs are valid
    if (route->path_id() >= route->no_of_paths() || 
        route->reverse()->path_id() >= route->reverse()->no_of_paths()) {
        return false;
    }
    
    // Verify path IDs match between forward and reverse routes
    if (route->path_id() != route->reverse()->path_id() || 
        route->no_of_paths() != route->reverse()->no_of_paths()) {
        return false;
    }
    
    return true;
}

vector<const Route*>* DragonFlyPlusTopology::get_bidir_paths(uint32_t src, uint32_t dest, bool reverse) {
    vector<const Route*>* paths = new vector<const Route*>();
    
    if (src >= _no_of_nodes || dest >= _no_of_nodes) {
        return paths;
    }
    
    uint32_t src_sw = src / _p;
    uint32_t dst_sw = dest / _p;
    
    // Count total possible paths first
    uint32_t total_paths = 0;
    
    // Direct path
    if (src_sw == dst_sw || 
        (queues_switch_switch[src_sw][dst_sw] && pipes_switch_switch[src_sw][dst_sw]) ||
        (queues_global_plus[src_sw][dst_sw] && pipes_global_plus[src_sw][dst_sw])) {
        total_paths++;
    }
    
    // Indirect paths through other switches
    for (uint32_t i = 0; i < _no_of_switches; i++) {
        if (i != src_sw && i != dst_sw) {
            if ((queues_switch_switch[src_sw][i] || queues_global_plus[src_sw][i]) &&
                (queues_switch_switch[i][dst_sw] || queues_global_plus[i][dst_sw])) {
                total_paths++;
            }
        }
    }
    
    if (total_paths == 0) {
        return paths;  // No valid paths found
    }
    
    uint32_t current_path_id = 0;
    
    // Try direct path first
    if (src_sw == dst_sw || 
        (queues_switch_switch[src_sw][dst_sw] && pipes_switch_switch[src_sw][dst_sw]) ||
        (queues_global_plus[src_sw][dst_sw] && pipes_global_plus[src_sw][dst_sw])) {
        
        Route* routeout = new Route();
        Route* routeback = new Route();
        bool valid_path = true;
        
        // Forward path
        if (queues_host_switch[src][src_sw] && pipes_host_switch[src][src_sw]) {
            routeout->push_back(queues_host_switch[src][src_sw]);
            routeout->push_back(pipes_host_switch[src][src_sw]);
            
            if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                PacketSink* remote = queues_host_switch[src][src_sw]->getRemoteEndpoint();
                if (remote) routeout->push_back(remote);
            }
        } else {
            valid_path = false;
        }
        
        if (src_sw != dst_sw) {
            bool found_link = false;
            
            // Try regular global link first
            if (queues_switch_switch[src_sw][dst_sw] && pipes_switch_switch[src_sw][dst_sw]) {
                routeout->push_back(queues_switch_switch[src_sw][dst_sw]);
                routeout->push_back(pipes_switch_switch[src_sw][dst_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[src_sw][dst_sw]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_link = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[src_sw][dst_sw] && pipes_global_plus[src_sw][dst_sw]) {
                routeout->push_back(queues_global_plus[src_sw][dst_sw]);
                routeout->push_back(pipes_global_plus[src_sw][dst_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[src_sw][dst_sw]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_link = true;
            }
            
            if (!found_link) {
                valid_path = false;
            }
        }
        
        if (queues_switch_host[dst_sw][dest] && pipes_switch_host[dst_sw][dest]) {
            routeout->push_back(queues_switch_host[dst_sw][dest]);
            routeout->push_back(pipes_switch_host[dst_sw][dest]);
        } else {
            valid_path = false;
        }
        
        // Reverse path
        if (queues_host_switch[dest][dst_sw] && pipes_host_switch[dest][dst_sw]) {
            routeback->push_back(queues_host_switch[dest][dst_sw]);
            routeback->push_back(pipes_host_switch[dest][dst_sw]);
            
            if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                PacketSink* remote = queues_host_switch[dest][dst_sw]->getRemoteEndpoint();
                if (remote) routeback->push_back(remote);
            }
        } else {
            valid_path = false;
        }
        
        if (src_sw != dst_sw) {
            bool found_link = false;
            
            // Try regular global link first
            if (queues_switch_switch[dst_sw][src_sw] && pipes_switch_switch[dst_sw][src_sw]) {
                routeback->push_back(queues_switch_switch[dst_sw][src_sw]);
                routeback->push_back(pipes_switch_switch[dst_sw][src_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[dst_sw][src_sw]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_link = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[dst_sw][src_sw] && pipes_global_plus[dst_sw][src_sw]) {
                routeback->push_back(queues_global_plus[dst_sw][src_sw]);
                routeback->push_back(pipes_global_plus[dst_sw][src_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[dst_sw][src_sw]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_link = true;
            }
            
            if (!found_link) {
                valid_path = false;
            }
        }
        
        if (queues_switch_host[src_sw][src] && pipes_switch_host[src_sw][src]) {
            routeback->push_back(queues_switch_host[src_sw][src]);
            routeback->push_back(pipes_switch_host[src_sw][src]);
        } else {
            valid_path = false;
        }
        
        // Link the routes
        routeout->set_reverse(routeback);
        routeback->set_reverse(routeout);
        
        // Add path if valid
        if (valid_path) {
            // Add final sink to both routes to properly terminate them
            PacketSink* finalSinkOut = new TerminalPacketSink("SinkOut_" + to_string(current_path_id));
            PacketSink* finalSinkBack = new TerminalPacketSink("SinkBack_" + to_string(current_path_id));
            
            routeout->push_back(finalSinkOut);
            routeback->push_back(finalSinkBack);
            
            routeout->set_path_id(current_path_id, total_paths);  // Set path ID and total paths
            routeback->set_path_id(current_path_id, total_paths); // Set same values for reverse path
            
            // Link the routes
            routeout->set_reverse(routeback);
            routeback->set_reverse(routeout);
            
            paths->push_back(routeout);
            current_path_id++;
        } else {
            delete routeout;
            delete routeback;
        }
    }
    
    // Try indirect paths through other switches
    for (uint32_t i = 0; i < _no_of_switches; i++) {
        if (i != src_sw && i != dst_sw) {
            Route* routeout = new Route();
            Route* routeback = new Route();
            bool valid_path = true;
            
            // Forward path
            if (queues_host_switch[src][src_sw] && pipes_host_switch[src][src_sw]) {
                routeout->push_back(queues_host_switch[src][src_sw]);
                routeout->push_back(pipes_host_switch[src][src_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_host_switch[src][src_sw]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
            } else {
                valid_path = false;
            }
            
            // First hop
            bool found_first_hop = false;
            if (queues_switch_switch[src_sw][i] && pipes_switch_switch[src_sw][i]) {
                routeout->push_back(queues_switch_switch[src_sw][i]);
                routeout->push_back(pipes_switch_switch[src_sw][i]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[src_sw][i]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_first_hop = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[src_sw][i] && pipes_global_plus[src_sw][i]) {
                routeout->push_back(queues_global_plus[src_sw][i]);
                routeout->push_back(pipes_global_plus[src_sw][i]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[src_sw][i]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_first_hop = true;
            }
            
            if (!found_first_hop) {
                valid_path = false;
            }
            
            // Second hop
            bool found_second_hop = false;
            if (queues_switch_switch[i][dst_sw] && pipes_switch_switch[i][dst_sw]) {
                routeout->push_back(queues_switch_switch[i][dst_sw]);
                routeout->push_back(pipes_switch_switch[i][dst_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[i][dst_sw]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_second_hop = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[i][dst_sw] && pipes_global_plus[i][dst_sw]) {
                routeout->push_back(queues_global_plus[i][dst_sw]);
                routeout->push_back(pipes_global_plus[i][dst_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[i][dst_sw]->getRemoteEndpoint();
                    if (remote) routeout->push_back(remote);
                }
                found_second_hop = true;
            }
            
            if (!found_second_hop) {
                valid_path = false;
            }
            
            if (queues_switch_host[dst_sw][dest] && pipes_switch_host[dst_sw][dest]) {
                routeout->push_back(queues_switch_host[dst_sw][dest]);
                routeout->push_back(pipes_switch_host[dst_sw][dest]);
            } else {
                valid_path = false;
            }
            
            // Reverse path
            if (queues_host_switch[dest][dst_sw] && pipes_host_switch[dest][dst_sw]) {
                routeback->push_back(queues_host_switch[dest][dst_sw]);
                routeback->push_back(pipes_host_switch[dest][dst_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_host_switch[dest][dst_sw]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
            } else {
                valid_path = false;
            }
            
            // First hop of reverse path
            found_first_hop = false;
            if (queues_switch_switch[dst_sw][i] && pipes_switch_switch[dst_sw][i]) {
                routeback->push_back(queues_switch_switch[dst_sw][i]);
                routeback->push_back(pipes_switch_switch[dst_sw][i]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[dst_sw][i]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_first_hop = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[dst_sw][i] && pipes_global_plus[dst_sw][i]) {
                routeback->push_back(queues_global_plus[dst_sw][i]);
                routeback->push_back(pipes_global_plus[dst_sw][i]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[dst_sw][i]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_first_hop = true;
            }
            
            if (!found_first_hop) {
                valid_path = false;
            }
            
            // Second hop of reverse path
            found_second_hop = false;
            if (queues_switch_switch[i][src_sw] && pipes_switch_switch[i][src_sw]) {
                routeback->push_back(queues_switch_switch[i][src_sw]);
                routeback->push_back(pipes_switch_switch[i][src_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_switch_switch[i][src_sw]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_second_hop = true;
            }
            // Try Plus link if available
            else if (queues_global_plus[i][src_sw] && pipes_global_plus[i][src_sw]) {
                routeback->push_back(queues_global_plus[i][src_sw]);
                routeback->push_back(pipes_global_plus[i][src_sw]);
                
                if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN) {
                    PacketSink* remote = queues_global_plus[i][src_sw]->getRemoteEndpoint();
                    if (remote) routeback->push_back(remote);
                }
                found_second_hop = true;
            }
            
            if (!found_second_hop) {
                valid_path = false;
            }
            
            if (queues_switch_host[src_sw][src] && pipes_switch_host[src_sw][src]) {
                routeback->push_back(queues_switch_host[src_sw][src]);
                routeback->push_back(pipes_switch_host[src_sw][src]);
            } else {
                valid_path = false;
            }
            
            // Link the routes
            routeout->set_reverse(routeback);
            routeback->set_reverse(routeout);
            
            // Add path if valid
            if (valid_path) {
                // Add final sink to both routes to properly terminate them
                PacketSink* finalSinkOut = new TerminalPacketSink("SinkOut_" + to_string(current_path_id));
                PacketSink* finalSinkBack = new TerminalPacketSink("SinkBack_" + to_string(current_path_id));
                
                routeout->push_back(finalSinkOut);
                routeback->push_back(finalSinkBack);
                
                routeout->set_path_id(current_path_id, total_paths);  // Set path ID and total paths
                routeback->set_path_id(current_path_id, total_paths); // Set same values for reverse path
                
                // Link the routes
                routeout->set_reverse(routeback);
                routeback->set_reverse(routeout);
                
                paths->push_back(routeout);
                current_path_id++;
            } else {
                delete routeout;
                delete routeback;
            }
        }
    }
    
    return paths;
}

bool DragonFlyPlusTopology::is_global_plus_link(uint32_t src_switch, uint32_t dst_switch) {
    uint32_t src_group = src_switch / _a;
    uint32_t dst_group = dst_switch / _a;
    uint32_t src_index = src_switch % _a;
    uint32_t dst_index = dst_switch % _a;
    
    // Check if there's a Plus link between these switches
    // Plus links connect switches in different groups with complementary indices
    if (src_group != dst_group && src_index < _h_plus) {
        return dst_index == (_a - src_index - 1);
    }
    return false;
}

int64_t DragonFlyPlusTopology::find_switch(Queue* queue) {
    // Find which switch this queue belongs to
    for (uint32_t i = 0; i < _no_of_switches; i++) {
        for (uint32_t j = 0; j < queues_switch_switch[i].size(); j++) {
            if (queues_switch_switch[i][j] == queue)
                return i;
        }
        for (uint32_t j = 0; j < queues_global_plus[i].size(); j++) {
            if (queues_global_plus[i][j] == queue)
                return i;
        }
    }
    return -1;
}

int64_t DragonFlyPlusTopology::find_destination(Queue* queue) {
    // Find the destination of this queue
    for (uint32_t i = 0; i < _no_of_nodes; i++) {
        if (queues_host_switch[i][0] == queue)
            return i;
    }
    return -1;
} 