# htsim Network Simulator

####  ORIGINAL REPO    https://github.com/Broadcom/csg-htsim

htsim is a high performance discrete event simulator, inspired by ns2, but much faster, primarily intended to examine congestion control algorithm behaviour.  It was originally written by [Mark Handley](http://www0.cs.ucl.ac.uk/staff/M.Handley/) to allow [Damon Wishik](https://www.cl.cam.ac.uk/~djw1005/) to examine TCP stability issues when large numbers of flows are multiplexed.  It was extended by [Costin Raiciu](http://nets.cs.pub.ro/~costin/) to examine [Multipath TCP performance](http://nets.cs.pub.ro/~costin/files/mptcp-nsdi.pdf) during the MPTCP standardization process, and models of datacentre networks were added to [examine multipath transport](http://nets.cs.pub.ro/~costin/files/mptcp_dc_sigcomm.pdf) in a variety of datacentre topologies.  [NDP](http://nets.cs.pub.ro/~costin/files/ndp.pdf) was developed using htsim, and simple models of DCTCP, DCQCN were added for comparison.  Later htsim was adopted by Correct Networks (now part of Broadcom) to develop [EQDS](http://nets.cs.pub.ro/~costin/files/eqds.pdf), and switch models were improved to allow a variety of forwarding methods.  Support for a simple RoCE model, PFC, Swift and HPCC were added.

## Getting Started

There are some limited instructions in the [wiki](https://github.com/Broadcom/csg-htsim/wiki).  

htsim is written in C++, and has no dependencies.  It should compile and run with g++ or clang on MacOS or Linux.  To compile htsim, cd into the sim directory and run make.

To get started with running experiments, take a look in the experiments directory where there are some examples.  These examples generally require bash, python3 and gnuplot.




# HTSIM Network Simulator 

This repository add implementations DragonFly Plus topologies for the HTSIM network simulator.

## Building the Project

1. Clone the repository:
```bash
git clone https://github.com/anouaressa/csg-htsim-.git
cd csg-htsim
```

2. Build the simulator:
```bash
cd sim/datacenter
make
```

This will build both DragonFly and DragonFly Plus executables.

## Running DragonFly Tests

The DragonFly topology can be tested using TCP or NDP (Network Discovery Protocol):

### TCP Test
```bash
./htsim_tcp_dragonfly_permutation -nodes 16 -conns 8 -cwnd 15 COUPLED_EPSILON 1.0
```

### NDP Test
```bash
./htsim_ndp_dragonfly_permutation -nodes 16 -conns 8 -cwnd 15
```

Parameters:
- `-nodes`: Number of nodes in the network
- `-conns`: Number of concurrent connections
- `-cwnd`: TCP congestion window size
- `COUPLED_EPSILON`: Congestion control algorithm (for TCP only)

## Running DragonFly Plus Tests

DragonFly Plus includes enhanced routing and additional global links:

```bash
./htsim_dragonfly_plus -nodes 16 -conns 8 -cwnd 15 COUPLED_EPSILON 1.0
```

Additional Parameters for DragonFly Plus:
- The topology automatically configures additional global links (h') based on the number of original global links (h)
- Adaptive routing is enabled by default

## Topology Parameters

### DragonFly
- p: Number of hosts per router
- a: Number of routers per group
- h: Number of global links per router
- k: Router radix = p + h + a - 1

### DragonFly Plus
- p: Number of hosts per router
- a: Number of routers per group
- h: Number of global links per router
- h': Additional global links per router (Plus enhancement)
- k: Router radix = p + h + h' + a - 1

## Example Configurations

1. Small Test Network:
```bash
./htsim_dragonfly_plus -nodes 16 -conns 8 -cwnd 15
```

2. Medium Network:
```bash
./htsim_dragonfly_plus -nodes 64 -conns 32 -cwnd 20
```

3. Large Network:
```bash
./htsim_dragonfly_plus -nodes 256 -conns 128 -cwnd 25
```

## Output and Analysis

The simulator generates several output files:
- `logout.dat`: Contains detailed simulation logs
- Path information is printed to stdout during execution
- Queue statistics and congestion information

## Key Differences Between DragonFly and DragonFly Plus

1. Network Architecture:
   - DragonFly Plus adds extra global links (h')
   - Improved path diversity
   - Better load balancing

2. Routing:
   - DragonFly: Basic minimal and non-minimal routing
   - DragonFly Plus: Adaptive routing with congestion awareness

3. Performance:
   - DragonFly Plus offers better congestion management
   - Improved fault tolerance
   - Enhanced scalability

## Debugging and Monitoring

To enable detailed logging:
```bash
export HTSIM_DEBUG=1
```

To monitor specific aspects:
1. Queue occupancy:
```bash
./htsim_dragonfly_plus -nodes 16 -conns 8 -cwnd 15 -queue_log 1
```

2. Path selection:
```bash
./htsim_dragonfly_plus -nodes 16 -conns 8 -cwnd 15 -path_log 1
```

## Common Issues and Solutions

1. If you see "No route found":
   - Check if the number of nodes is compatible with the topology
   - Verify that p, a, and h parameters are properly balanced

2. For performance issues:
   - Adjust the congestion window size (cwnd)
   - Try different numbers of connections
   - Monitor queue occupancy

## References

- DragonFly topology: Kim et al., ISCA 2008
- DragonFly Plus enhancements: Based on modern data center requirements 
