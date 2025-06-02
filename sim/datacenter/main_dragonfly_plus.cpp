#include "config.h"
#include <sstream>
#include <iostream>
#include <string.h>
#include <math.h>
#include "network.h"
#include "randomqueue.h"
#include "subflow_control.h"
#include "shortflows.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "clock.h"
#include "tcp.h"
#include "tcp_transfer.h"
#include "dragon_fly_plus_topology.h"
#include "firstfit.h"
#include <list>

#define PRINT_PATHS 1

uint32_t RTT = 1; // 1 microsecond
uint32_t DEFAULT_NODES = 16;

FirstFit* ff = NULL;
uint32_t subflow_count = 1;

EventList eventlist;

void exit_error(char* progr) {
    cout << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] [epsilon][COUPLED_SCALABLE_TCP]" << endl;
    exit(1);
}

void print_path(std::ofstream &paths, const Route* rt) {
    for (uint32_t i = 1; i < rt->size() - 1; i += 2) {
        RandomQueue* q = (RandomQueue*)rt->at(i);
        if (q != NULL)
            paths << q->str() << " ";
        else 
            paths << "NULL ";
    }
    paths << endl;
}

int main(int argc, char **argv) {
    eventlist.setEndtime(timeFromSec(4));
    Clock c(timeFromSec(50 / 100.), eventlist);
    
    uint32_t no_of_nodes = DEFAULT_NODES;
    uint32_t no_of_conns = DEFAULT_NODES;
    
    int algo = COUPLED_EPSILON;
    double epsilon = 1;
    
    string filename = "logout.dat";
    uint32_t cwnd = 15;
    
    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-o")) {
            filename = argv[i+1];
            i++;
        } else if (!strcmp(argv[i], "-nodes")) {
            no_of_nodes = atoi(argv[i+1]);
            cout << "no_of_nodes " << no_of_nodes << endl;
            i++;
        } else if (!strcmp(argv[i], "-conns")) {
            no_of_conns = atoi(argv[i+1]);
            cout << "no_of_conns " << no_of_conns << endl;
            i++;
        } else if (!strcmp(argv[i], "-cwnd")) {
            cwnd = atoi(argv[i+1]);
            cout << "cwnd " << cwnd << endl;
            i++;
        } else if (!strcmp(argv[i], "UNCOUPLED"))
            algo = UNCOUPLED;
        else if (!strcmp(argv[i], "COUPLED_INC"))
            algo = COUPLED_INC;
        else if (!strcmp(argv[i], "FULLY_COUPLED"))
            algo = FULLY_COUPLED;
        else if (!strcmp(argv[i], "COUPLED_TCP"))
            algo = COUPLED_TCP;
        else if (!strcmp(argv[i], "COUPLED_SCALABLE_TCP"))
            algo = COUPLED_SCALABLE_TCP;
        else if (!strcmp(argv[i], "COUPLED_EPSILON")) {
            algo = COUPLED_EPSILON;
            if (argc > i+1) {
                epsilon = atof(argv[i+1]);
                i++;
            }
            printf("Using epsilon %f\n", epsilon);
        } else
            exit_error(argv[0]);
        
        i++;
    }
    srand(13);
    
    cout << "Using algo=" << algo << " epsilon=" << epsilon << endl;
    cout << "Logging to " << filename << endl;
    
    // Initialize logfile
    Logfile logfile(filename, eventlist);
    logfile.setStartTime(timeFromSec(0));
    
    // Add TCP sink logger
    TcpSinkLoggerSampling sinkLogger = TcpSinkLoggerSampling(timeFromMs(1000), eventlist);
    logfile.addLogger(sinkLogger);
    
    // Create DragonFly Plus topology
    // Using balanced configuration: p = h, a = 2h, h_plus = h/2
    uint32_t p = (uint32_t)ceil(sqrt(no_of_nodes/4));  // Hosts per router
    uint32_t h = p;                                     // Global links per router
    uint32_t a = 2 * p;                                // Routers per group
    uint32_t h_plus = h/2;                             // Additional global links
    
    // Create DragonFly Plus topology with large queue size (2000 packets)
    DragonFlyPlusTopology* top = new DragonFlyPlusTopology(p, h, a, h_plus, memFromPkt(2000), &logfile, &eventlist, RANDOM, timeFromUs(RTT));
    
    // Initialize TCP endpoints
    TcpRtxTimerScanner tcpRtxScanner(timeFromMs(10), eventlist);
    
    // Create TCP connections
    for (uint32_t i = 0; i < no_of_conns; i++) {
        uint32_t src = rand() % no_of_nodes;
        uint32_t dst = rand() % no_of_nodes;
        
        while (src == dst) {
            dst = rand() % no_of_nodes;
        }
        
        // Create TCP source and sink
        TcpSrc* tcpSrc = new TcpSrc(NULL, NULL, eventlist);
        TcpSink* tcpSnk = new TcpSink();
        
        tcpSrc->setName("tcp_" + std::to_string(i));
        logfile.writeName(*tcpSrc);
        
        tcpSnk->setName("tcp_sink_" + std::to_string(i));
        logfile.writeName(*tcpSnk);
        
        // Get routes from source to destination
        vector<const Route*>* routes = top->get_bidir_paths(src, dst, false);
        
        if (routes->size() == 0) {
            cout << "No route from node " << src << " to node " << dst << endl;
            exit(1);
        }
        
        cout << "Found " << routes->size() << " route(s) from node " << src << " to node " << dst << endl;
        
        // Print paths if enabled
        if (PRINT_PATHS) {
            cout << "Path from node " << src << " to node " << dst << ":" << endl;
            for (uint32_t j = 0; j < routes->size(); j++) {
                cout << "Route " << j << " (size=" << routes->at(j)->size() << "): ";
                for (uint32_t k = 0; k < routes->at(j)->size(); k++) {
                    if (routes->at(j)->at(k)) {
                        cout << routes->at(j)->at(k)->nodename() << " ";
                    } else {
                        cout << "NULL ";
                    }
                }
                cout << endl;
                
                if (routes->at(j)->reverse()) {
                    cout << "Reverse route " << j << " (size=" << routes->at(j)->reverse()->size() << "): ";
                    for (uint32_t k = 0; k < routes->at(j)->reverse()->size(); k++) {
                        if (routes->at(j)->reverse()->at(k)) {
                            cout << routes->at(j)->reverse()->at(k)->nodename() << " ";
                        } else {
                            cout << "NULL ";
                        }
                    }
                    cout << endl;
                }
            }
        }
        
        // Setup TCP connection using the first available route
        const Route* routeout = routes->at(0);
        const Route* routeback = routes->at(0)->reverse();
        
        if (!routeout || !routeback) {
            cout << "Invalid route from node " << src << " to node " << dst << endl;
            exit(1);
        }
        
        // Verify route sizes and components
        if (routeout->size() == 0 || routeback->size() == 0) {
            cout << "Empty route from node " << src << " to node " << dst << endl;
            exit(1);
        }
        
        // Verify route consistency
        if (routeout->size() != routeback->size()) {
            cout << "Mismatched route sizes from node " << src << " to node " << dst << endl;
            exit(1);
        }
        
        // Verify route components
        for (uint32_t k = 0; k < routeout->size(); k++) {
            if (!routeout->at(k)) {
                cout << "Invalid component in forward route at position " << k << endl;
                exit(1);
            }
            if (!routeback->at(k)) {
                cout << "Invalid component in reverse route at position " << k << endl;
                exit(1);
            }
        }
        
        // Verify path IDs
        if (routeout->path_id() >= routeout->no_of_paths() || 
            routeback->path_id() >= routeback->no_of_paths()) {
            cout << "Invalid path ID for route from node " << src << " to node " << dst << endl;
            exit(1);
        }
        
        // Verify path ID consistency
        if (routeout->path_id() != routeback->path_id() || 
            routeout->no_of_paths() != routeback->no_of_paths()) {
            cout << "Inconsistent path IDs between forward and reverse routes" << endl;
            exit(1);
        }
        
        // Connect TCP endpoints with proper route validation
        tcpSrc->connect(*routeout, *routeback, *tcpSnk, timeFromMs(0));
        
        // Set initial congestion window
        tcpSrc->set_cwnd(cwnd*Packet::data_packet_size());
        
        // Add to TCP RTX scanner
        tcpRtxScanner.registerTcp(*tcpSrc);
        
        // Log connection
        sinkLogger.monitorSink(tcpSnk);
        
        // Set source and destination IDs
        tcpSrc->set_dst(dst);
        tcpSnk->set_dst(src);
        
#ifdef PACKET_SCATTER
        tcpSrc->set_paths(routes);
#endif
    }
    
    // Run simulation
    while (eventlist.doNextEvent()) {
    }
    
    return 0;
} 