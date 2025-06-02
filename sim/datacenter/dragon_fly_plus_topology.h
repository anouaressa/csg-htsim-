#ifndef DRAGONFLY_PLUS
#define DRAGONFLY_PLUS

#include "main.h"
#include "randomqueue.h"
#include "queue_lossless.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "ecnqueue.h"
#include "pipe.h"
#include "config.h"
#include "loggers.h"
#include "network.h"
#include "topology.h"
#include "logfile.h"
#include "eventlist.h"
#include "switch.h"
#include <ostream>
#include <vector>
#include <map>

// DragonFly Plus parameters
// p = number of hosts per router
// a = number of routers per group
// h = number of global links per router
// h' = number of additional global links per router (Plus enhancement)
// k = router radix = p + h + h' + a - 1
// g = number of groups
//
// For balanced configuration:
// a = 2p = 2h
// h' = h/2 (additional links are half of original global links)
// Total nodes N = ap(ah + 1)

#define HOST_TOR(src) (src/_p)
#define HOST_GROUP(src) (src/(_a*_p))

#ifndef QT
#define QT
typedef enum {RANDOM, ECN, COMPOSITE, CTRL_PRIO, LOSSLESS, LOSSLESS_INPUT, LOSSLESS_INPUT_ECN, COMPOSITE_ECN} queue_type;
#endif

class DragonFlyPlusTopology : public Topology {
public:
    vector<Switch*> switches;
    
    // Network components
    vector<vector<Pipe*>> pipes_host_switch;      // Connections from hosts to switches
    vector<vector<Pipe*>> pipes_switch_switch;    // Local and global connections between switches
    vector<vector<Queue*>> queues_host_switch;    // Queues from hosts to switches
    vector<vector<Queue*>> queues_switch_switch;  // Queues between switches
    vector<vector<Pipe*>> pipes_switch_host;      // Connections from switches back to hosts
    vector<vector<Queue*>> queues_switch_host;    // Queues from switches back to hosts
    
    // Additional global links for Plus enhancement
    vector<vector<Pipe*>> pipes_global_plus;      // Additional global connections
    vector<vector<Queue*>> queues_global_plus;    // Queues for additional global connections
    
    Logfile* logfile;
    EventList* _eventlist;
    uint32_t failed_links;
    queue_type qt;

    // Constructors
    DragonFlyPlusTopology(uint32_t p, uint32_t h, uint32_t a, uint32_t h_plus,
                         mem_b queuesize, Logfile* log, EventList* ev,
                         queue_type q, simtime_picosec rtt);
    
    DragonFlyPlusTopology(uint32_t no_of_nodes, mem_b queuesize, Logfile* log,
                         EventList* ev, queue_type q, simtime_picosec rtt);

    // Core functions
    void init_network();
    virtual vector<const Route*>* get_bidir_paths(uint32_t src, uint32_t dest, bool reverse);
    
    // Queue management
    Queue* alloc_src_queue(QueueLogger* q);
    Queue* alloc_queue(QueueLogger* q, mem_b queuesize, bool tor);
    Queue* alloc_queue(QueueLogger* q, uint64_t speed, mem_b queuesize, bool tor);
    void count_queue(Queue*);
    
    // Utility functions
    void print_path(std::ofstream& paths, uint32_t src, const Route* route);
    vector<uint32_t>* get_neighbours(uint32_t src) { return NULL; }
    uint32_t no_of_nodes() const { return _no_of_nodes; }
    uint32_t get_mtu() const { return 1500; }  // Standard MTU size
    uint32_t get_group(uint32_t sw) const { return sw / _a; }  // Get group number for a switch

private:
    // Helper functions
    bool is_valid_route(Route* route);
    int64_t find_switch(Queue* queue);
    int64_t find_destination(Queue* queue);
    void set_params(uint32_t no_of_nodes);
    void set_params();
    bool is_global_plus_link(uint32_t src_switch, uint32_t dst_switch);
    
    // Topology parameters
    uint32_t _p;           // Hosts per router
    uint32_t _a;           // Routers per group
    uint32_t _h;           // Global links per router
    uint32_t _h_plus;      // Additional global links per router
    uint32_t _no_of_nodes; // Total number of nodes
    uint32_t _no_of_groups;// Total number of groups
    uint32_t _no_of_switches; // Total number of switches
    simtime_picosec _rtt;  // Round trip time
    mem_b _queuesize;      // Queue size
};

#endif 