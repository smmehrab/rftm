# RFTM: Recommendation Filtering Trust Model

An implementation of the trust model described in "[Trust Model Based on Recommendation Filtering in Internet of Vehicles](https://ieeexplore.ieee.org/abstract/document/10336646/)" using NS-3 network simulator.

Some aspects of the implementation deviates from the paper to align with a proposed solution, to perform a comparative analysis between the two solutions.

## Overview

### Concept

The RFTM model addresses the problem of inaccurate recommendation evaluation in vehicular networks by implementing a hierarchical trust management framework with three key innovations: 

1. Direct trust calculation using Bayesian inference with time decay functions, sliding window mechanisms, and penalty factors to handle temporal dynamics.

2. Recommendation filtering using Fuzzy C-Means clustering to identify and filter out malicious recommenders based on similarity, difference degree, and reliability metrics.

3. Adaptive weighting that dynamically adjusts the balance between direct trust and recommendation trust based on interaction history and environmental factors, improving the model's adaptability to changing network conditions.

### Components

- **Vehicle** <br> Ordinary vehicles (V) or vehicular service providers (VSP) with configurable attack behaviors
- **RSU** <br> Calculates global trust using recommendation filtering and FCM clustering
- **Trust Model** <br> Combines direct trust and filtered recommendations with adaptive weighting

### Flow

- Vehicles interact with each other, via requesting services and providing feedback on that service.
- Vehicles report interaction results (positive/negative feedback) to nearby RSUs
- RSUs collect trust evaluation data and apply FCM clustering to filter malicious recommenders
- RSUs calculate recommendation trust using similarity and reliability metrics from filtered recommenders
- RSUs compute global trust values using adaptive weighting between direct and recommendation trust
- Trust values are propagated back to vehicles for decision making in future interactions
- Various attack scenarios are simulated (Bad Mouthing, Ballot Stuffing, On-Off attacks etc)

### Cases

There are 25 simulation cases.

- Cases 1-4: Basic attack scenarios
- Cases 5-14: On-Off attack variations with different timing patterns
- Cases 15-21: Advanced attack combinations
- Cases 22-25: FCM timing analysis with varying vehicle counts

## Guide

### Prerequisites

- [Docker](https://www.docker.com/get-started/)
- [NS-3 network simulator](https://www.nsnam.org/) <br> (Included in Docker image)

### Setup

- Build and run the Docker container

    ```bash
    docker build -t rftm .
    docker run -it --name rftm-container -v rftm-build:/ns-allinone-3.33/ns-3.33/build rftm /bin/bash
    ```
- Configure and build NS-3

    ```bash
    cd ns-allinone-3.33/ns-3.33
    ./waf configure --disable-werror --enable-examples --enable-tests --build-profile=optimized
    ./waf build
    ```

### Usage

- Run specific simulation cases

    ```bash
    ./waf --run "rftm --case=1"
    ./waf --run "rftm --case=3" 
    # To save logs on file
    ./waf --run "rftm --case=4" > case4.log 2>&1 
    ```

- Copy log files from Docker container to host machine

    ```bash
    # Copy individual log files to logs directory
    docker cp <container_id>:/usr/ns-allinone-3.33/ns-3.33/case1.log ./logs/
    docker cp <container_id>:/usr/ns-allinone-3.33/ns-3.33/case22.log ./logs/
    
    # Copy multiple log files at once
    docker cp <container_id>:/usr/ns-allinone-3.33/ns-3.33/ ./logs/
    ```

- Update modified implementation to Docker container

    ```bash
    # Copy updated files from host machine to container
    docker cp . <container_id>:/usr/ns-allinone-3.33/ns-3.33/scratch/rftm/
    
    # Rebuild after updating files
    docker exec -it <container_id> /bin/bash
    cd ns-allinone-3.33/ns-3.33
    ./waf build
    ```