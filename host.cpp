#include <stdlib.h>
#include <math.h>

#include "host.h"

double drand() {
    return (double)rand()/(double)RAND_MAX;
}

#define PI2 (2*3.1415926)

// generate normal random numbers using Box-Muller.
// this generates 2 at a time, so cache the other one
//
double rand_normal() {
    static bool cached;
    static double cached_value;
    if (cached) {
        cached = false;
        return cached_value;
    }
    double u1 = drand();
    double u2 = drand();
    double z = sqrt(-2*log(u1));
    cached_value = z*sin(PI2*u2);
    cached = true;
    return z*cos(PI2*u2);
}

int id=1;

// add hosts with normally distributed flops
//
void add_normal(double mean, double stddev, int n, vector<HOST*> &hosts) {
    double x;
    for (int i=0; i<n; i++) {
        while (1) {
            x = rand_normal();
            if (x<4 && x>-4) break;
        }
        double f = mean + x*stddev;
        HOST* h = new HOST(f, 10, 10, id++);
        hosts.push_back(h);
    }
}

void make_hosts(vector<HOST*> &hosts, bool cpu, bool gpu, bool npu) {
    if (cpu) {
        add_normal(1e10, 1e9, 100, hosts);
    }
    if (gpu) {
        add_normal(1e11, 1e10, 50, hosts);
    }
    if (npu) {
        add_normal(1e12, 1e11, 20, hosts);
    }
}

void make_hosts_test(vector<HOST*> &hosts) {
    hosts.push_back(new HOST(1.0, 10, 10, id++));
    hosts.push_back(new HOST(.9, 10, 10, id++));
    hosts.push_back(new HOST(.6, 10, 10, id++));
    for (int i=0; i<20; i++) {
        hosts.push_back(new HOST(.2, 10, 10, id++));
    }
}
