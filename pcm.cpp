#include <stdio.h>
#include <vector>
#include <algorithm>

using std::vector;

#define DEBUG   1

struct HOST {
    double flops;
    double storage_outer;
    double storage_inner;
    int id;
    HOST(double f, double so, double si, int i) {
        flops = f;
        storage_outer = so;
        storage_inner = si;
        id = i;
    }
};

struct JOB_PARAMS {
    int max_hosts_per_group;
    int max_groups;
    double max_var_hosts;
        // max fastest/slowest ratio within a group
    double max_var_groups;
        // max fastest/slowest ratio of total flops between groups
    double size_outer;
        // data to be divided among groups
    double size_inner;
        // data to be divided within a group
    bool aggressive;
        // if true, allow a group to exceed the max_var_groups limit
        // rather than skipping a host.
        // This optimized the job possibly at the expense of later jobs
};

// describes a group of (similar) hosts
//
struct GROUP {
    vector<HOST*> hosts;
    double max_flops;       // speed of fastest host in group
    double total_flops;     // sum of host flops

    GROUP() {
        max_flops = 0;
        total_flops = 0;
    }
    void update() {
        max_flops = 0;
        total_flops = 0;
        for (HOST* h: hosts) {
            total_flops += h->flops;
            if (h->flops > max_flops) {
                max_flops = h->flops;
            }
        }
    }
    void clear(){
        hosts.clear();
        max_flops = 0;
        total_flops = 0;
    }
    void add_host(HOST *h) {
        hosts.push_back(h);
        total_flops += h->flops;
        if (h->flops > max_flops) {
            max_flops = h->flops;
        }
    }
    double mean_flops() {
        return total_flops/hosts.size();
    }
    void print() {
        printf("   fastest: %f\n", max_flops);
        printf("   total: %f\n", total_flops);
        for (unsigned int i=0; i<hosts.size(); i++) {
            printf("   %d: %f\n", hosts[i]->id, hosts[i]->flops);
        }
    }
    // Calculate the inner storage needed, based on group size.
    // Remove hosts that don't have that much.
    // Return true if removed any.
    //
    bool prune_inner_storage(double size_inner) {
        if (hosts.empty()) return false;
        double needed = size_inner/hosts.size();
        vector<HOST*> new_hosts;
        bool found = false;
        for (HOST* h: hosts) {
            if (h->storage_inner < needed) {
                found = true;
            } else {
                new_hosts.push_back(h);
            }
        }
        hosts = new_hosts;
        update();
        return found;
    }

    void remove(HOST* h) {
        if (hosts.back() == h) {
            hosts.pop_back();
            update();
        }
    }

    void remove_first() {
        vector<HOST*>h2;
        if (hosts.empty()) {
            printf("remove_first: empty\n");
            exit(1);
        }
        for (unsigned int i=1; i<hosts.size(); i++) {
            h2.push_back(hosts[i]);
        }
        hosts = h2;
    }
};

// describes a set of groups
//
struct GROUP_SET {
    vector<GROUP*> groups;
    double total_flops;
    double flops0;
    GROUP_SET() {
        total_flops = 0;
    }
    void add_group(GROUP g) {
        if (groups.empty()) {
            flops0 = g.total_flops;
        }
        GROUP *g2 = new GROUP;
        *g2 = g;
        groups.push_back(g2);
        total_flops += g.total_flops;
    }
    void print() {
        for (unsigned int i=0; i<groups.size(); i++) {
            GROUP &g = *groups[i];
            printf("group %d:\n", i);
            g.print();
        }
    }
    void clear() {
        groups.clear();
        total_flops = 0;
        flops0 = 0;
    }
};

bool compare(HOST* p1, HOST* p2) {
    return p1->flops > p2->flops;
}

// Find a group set given the constraints that
// - the total FLOPS of a group can't exceed a given value.
// - the storage_outer of hosts must be at least a given value
// ... as well as the job params constraints.
//
// Enforce the inner storage constraint as follows:
// when about to accept a group, compute the inner storage req (Y/N).
// If any hosts don't satisfy it,
// remove them from the group ('inner storage pruning') and continue scanning
//
void find_group_set3(
    vector<HOST*> &hosts, JOB_PARAMS &params, GROUP_SET &s,
    double max_group_flops,
    double min_storage_outer
) {
    GROUP c;        // in-progress candidate group

#ifdef DEBUG
    printf(
        "find_group_set3() start: max_group_flops %f, min_storage_outer %f\n",
        max_group_flops, min_storage_outer
    );
#endif
    s.clear();
    for (unsigned int i=0; i<hosts.size(); i++) {
        if (s.groups.size() == params.max_groups) {
#ifdef DEBUG
            printf("reached max groups\n");
#endif
            break;
        }
        HOST* h = hosts[i];
#ifdef DEBUG
        printf("scan host %d: flops %f\n", h->id, h->flops);
#endif
        if (h->storage_outer < min_storage_outer) {
#ifdef DEBUG
            printf("insufficient outer storage %f\n", h->storage_outer);
#endif
            continue;
        }
        if (s.groups.empty()) {
            // no accepted groups so far.
            // Try to add H to the candidate group C
            // This is the first group, so we need to enforce max_group_flops
            //
            // Loop because of inner storage pruning
            while (1) {
                // If C is nonempty, see if H is too slow for it.
                // If so, try to accept C
                if (c.total_flops && h->flops < c.max_flops/params.max_var_hosts) {
                    if (c.prune_inner_storage(params.size_inner)) {
                        continue;
                    }
#ifdef DEBUG
                    printf("add group 1\n");
                    c.print();
#endif
                    s.add_group(c);
                    c.clear();
                    c.add_host(h);
                    goto next_host;
                }
                // see if adding H would make C too fast
                //
                if (c.total_flops + h->flops > max_group_flops) {
                    // If so, and C is nonempty, try to accept it
                    //
                    if (c.total_flops) {
                        if (c.prune_inner_storage(params.size_inner)) {
                            continue;
                        }
#ifdef DEBUG
                        printf("add group 2\n");
                        c.print();
#endif
                        s.add_group(c);
                        c.clear();
                    }
                    // New candidate group is {H}
                    c.add_host(h);
                    goto next_host;
                } else {
                    // Otherwise, add H to C.
                    // If C is now full, try to accept it
                    //
                    c.add_host(h);
                    if (c.hosts.size() == params.max_hosts_per_group) {
                        if (c.prune_inner_storage(params.size_inner)) {
                            c.remove(h);
                            continue;
                        }
#ifdef DEBUG
                        printf("add group 3\n");
                        c.print();
#endif
                        s.add_group(c);
                        c.clear();
                        goto next_host;
                    } else {
                        // C is not full; go on
                        goto next_host;
                    }
                }
            }
        } else {
            // There is at least 1 accepted group,
            // so we have to enforce params.max_var_groups
            //
            // C.total_flops is < group0.flops,
            // else we would have already accepted it
            //
            while (1) {
                double x = c.total_flops + h->flops;
                if (x > max_group_flops
                    || (!params.aggressive && x > s.flops0*params.max_var_groups)
                ) {
                    // adding H could make C too fast;
                    // see if we can accept C as is
                    //
                    if (c.prune_inner_storage(params.size_inner)) {
                        continue;
                    }
                    if (c.total_flops < s.flops0/params.max_var_groups) {
                        // C is too slow to accept, but adding H
                        // would make it too fast.
                        // Remove the first host from C and see if this
                        // changes things.
                        //
                        c.remove_first();
                        continue;
                    }
                    // accept C; the new candidate group is {H}
                    //
#ifdef DEBUG
                    printf("add group 4\n");
                    c.print();
#endif
                    s.add_group(c);
                    c.clear();
                    c.add_host(h);
                    goto next_host;
                } else {
                    // adding H to C would not make it too fast
                    // See if H is too slow for C
                    //
                    if (h->flops < c.max_flops/params.max_var_hosts) {
                        // Yes; try to accept C
                        if (c.prune_inner_storage(params.size_inner)) {
                            continue;
                        }

                        // c could be empty at this point
                        //
                        if (c.hosts.empty()) {
                            c.add_host(h);
                            goto next_host;
                        }
                        if (c.total_flops < s.flops0/params.max_var_groups) {
                            c.remove_first();
                            continue;
                        }
#ifdef DEBUG
                        printf("add group 5\n");
                        c.print();
#endif
                        s.add_group(c);
                        c.clear();
                        c.add_host(h);
                        goto next_host;
                    }

                    c.add_host(h);
                    if (c.prune_inner_storage(params.size_inner)) {
                        goto next_host;
                    }

                    // if C is at least s.flops0, accept it
                    //
                    if (c.total_flops >= s.flops0) {
#ifdef DEBUG
                        printf("add group 6\n");
                        c.print();
#endif
                        s.add_group(c);
                        c.clear();
                        goto next_host;
                    }

                    if (c.hosts.size() == params.max_hosts_per_group) {
                        // C is now full.  See if it's fast enough to accept
                        //
                        if (c.total_flops > s.flops0/params.max_var_groups) {
                            // yes - accept C
                            //
#ifdef DEBUG
                            printf("add group 7\n");
                            c.print();
#endif
                            s.add_group(c);
                            c.clear();
                            goto next_host;
                        } else {
                            // C is max size and too slow to accept.
                            // We're done
                            goto done;
                        }
                    }
                    goto next_host;
                }
            }
        }
next_host: ;
    }
done:
    // see if the candidate group is acceptable
    c.prune_inner_storage(params.size_inner);
    if (c.total_flops > s.flops0/params.max_var_groups) {
#ifdef DEBUG
        printf("add group 8\n");
        c.print();
#endif
        s.add_group(c);
    }
#ifdef DEBUG
    printf("find_group_set3() done:\n");
    s.print();
#endif
}

// Find a group set with the constraint that
// group average can't exceed a given value.
//
void find_group_set2(
    vector<HOST*> &hosts, JOB_PARAMS &params, GROUP_SET &gs,
    double max_group_flops
) {
#ifdef DEBUG
    printf("find_group_set2() start: max_group_flops %f\n", max_group_flops);
#endif
    for (int i=params.max_groups; i>0; i--) {
        double min_outer_storage = params.size_outer/i;
        find_group_set3(
            hosts, params, gs, max_group_flops, min_outer_storage
        );
        if (gs.groups.size() >= i) {
#ifdef DEBUG
            printf("find_group_set2(): success\n");
            gs.print();
#endif
            return;
        }
    }
    // nothing worked
#ifdef DEBUG
    printf("find_group_set2(): fail\n");
#endif
    gs.clear();
}

// Find a group set.
// Explore a range of values for max group flops
// Start at the largest possible value (sum of top N hosts)
// and go down by powers of two.
// Keep going until total FLOPs decreases
//
void find_group_set(
    vector<HOST*> &hosts, JOB_PARAMS &params, GROUP_SET &gs
) {
    sort(hosts.begin(), hosts.end(), compare);
    double max_group_flops = hosts[0]->flops*params.max_hosts_per_group;
    double best = 0;
    while (1) {
        GROUP_SET gs2;
        find_group_set2(
            hosts, params, gs2, max_group_flops
        );
#ifdef DEBUG
        printf("find_group_set: solution %f best %f\n", gs2.total_flops, best);
#endif
        if (gs2.total_flops <= best) {
            break;
        }
        best = gs2.total_flops;
        max_group_flops /= 2;
        gs = gs2;
    }
}

int main(int, char**) {
    vector<HOST*> hosts;
    JOB_PARAMS params;

    int id=1;
    hosts.push_back(new HOST(1.0, 10, .5, id++));
    hosts.push_back(new HOST(.9, 10, 10, id++));
    hosts.push_back(new HOST(.6, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));
    hosts.push_back(new HOST(.2, 10, 10, id++));

    params.max_hosts_per_group = 20;
    params.max_groups = 4;
    params.max_var_hosts = 1.5;
    params.max_var_groups = 1.4;
    params.size_outer = 10;
    params.size_inner = 10;
    params.aggressive = false;

    GROUP_SET gs;
    //find_group_set(hosts, params, gs);
    find_group_set3(hosts, params, gs, 1.5, 0);
    gs.print();

}
