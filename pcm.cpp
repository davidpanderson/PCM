// part of a 'parallel computing manager'

#include <stdio.h>
#include <vector>
#include <algorithm>

using std::vector;

#define DEBUG   1

struct HOST {
    double flops;
    double outer_storage;
    double inner_storage;
    int id;
    HOST(double f, double so, double si, int i) {
        flops = f;
        outer_storage = so;
        inner_storage = si;
        id = i;
    }
};

struct JOB_PARAMS {
    int max_hosts_per_team;
    int max_teams;
    double max_var_hosts;
        // max fastest/slowest ratio within a team
    double max_var_teams;
        // max fastest/slowest ratio of total flops between teams
    double size_outer;
        // data to be divided among teams
    double size_inner;
        // data to be divided within a team
    bool aggressive;
        // if true, allow a team to exceed the max_var_teams limit
        // rather than skipping a host.
        // This optimized the job possibly at the expense of later jobs
};

// describes a set of (similar) hosts
//
struct TEAM {
    vector<HOST*> hosts;
    double max_flops;       // speed of fastest host in team
    double total_flops;     // sum of host flops

    TEAM() {
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
    void add_host(HOST &h) {
        hosts.push_back(&h);
        total_flops += h.flops;
        if (h.flops > max_flops) {
            max_flops = h.flops;
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
    // Calculate the inner storage needed, based on team size.
    // Remove hosts that don't have that much.
    // Return true if removed any.
    //
    bool prune_inner_storage(double size_inner) {
        if (hosts.empty()) return false;
        double needed = size_inner/hosts.size();
        vector<HOST*> new_hosts;
        bool found = false;
        for (HOST* h: hosts) {
            if (h->inner_storage < needed) {
                found = true;
            } else {
                new_hosts.push_back(h);
            }
        }
        hosts = new_hosts;
        update();
        return found;
    }

    void remove(HOST& h) {
        if (hosts.back() == &h) {
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

// describes a set of teams
//
struct LEAGUE {
    vector<TEAM*> teams;
    double total_flops;
    double flops0;
    LEAGUE() {
        total_flops = 0;
    }
    void add_team(TEAM t) {
        if (teams.empty()) {
            flops0 = t.total_flops;
        }
        TEAM *t2 = new TEAM;
        *t2 = t;
        teams.push_back(t2);
        total_flops += t.total_flops;
    }
    void print() {
        for (unsigned int i=0; i<teams.size(); i++) {
            TEAM &t = *teams[i];
            printf("team %d:\n", i);
            t.print();
        }
    }
    void clear() {
        teams.clear();
        total_flops = 0;
        flops0 = 0;
    }
};

bool compare(HOST* h1, HOST* h2) {
    return h1->flops > h2->flops;
}

// Find a league given the constraints that
// - the total FLOPS of a team can't exceed a given value.
// - the outer_storage of hosts must be at least a given value
// ... as well as the job params constraints.
//
// Enforce the inner storage constraint as follows:
// when about to accept a team, compute the inner storage req (Y/N).
// If any hosts don't satisfy it,
// remove them from the team ('inner storage pruning') and continue scanning
//
void find_league3(
    vector<HOST*> &hosts, JOB_PARAMS &params, LEAGUE &league,
    double max_team_flops,
    double min_outer_storage
) {
    TEAM c;        // in-progress candidate team

#ifdef DEBUG
    printf(
        "find_league3() start: max_team_flops %f, min_outer_storage %f\n",
        max_team_flops, min_outer_storage
    );
#endif
    league.clear();
    for (unsigned int i=0; i<hosts.size(); i++) {
        if (league.teams.size() == params.max_teams) {
#ifdef DEBUG
            printf("reached max teams\n");
#endif
            break;
        }
        HOST& h = *hosts[i];
#ifdef DEBUG
        printf("scan host %d: flops %f\n", h.id, h.flops);
#endif
        if (h.outer_storage < min_outer_storage) {
#ifdef DEBUG
            printf("insufficient outer storage %f\n", h.outer_storage);
#endif
            continue;
        }
        if (league.teams.empty()) {
            // no accepted teams so far.
            // Try to add H to the candidate team C
            // This is the first team, so we need to enforce max_team_flops
            //
            // Loop because of inner storage pruning
            while (1) {
                // If C is nonempty, see if H is too slow for it.
                // If so, try to accept C
                if (c.total_flops && h.flops < c.max_flops/params.max_var_hosts) {
                    if (c.prune_inner_storage(params.size_inner)) {
                        continue;
                    }
#ifdef DEBUG
                    printf("add team 1\n");
                    c.print();
#endif
                    league.add_team(c);
                    c.clear();
                    c.add_host(h);
                    goto next_host;
                }
                // see if adding H would make C too fast
                //
                if (c.total_flops + h.flops > max_team_flops) {
                    // If so, and C is nonempty, try to accept it
                    //
                    if (c.total_flops) {
                        if (c.prune_inner_storage(params.size_inner)) {
                            continue;
                        }
#ifdef DEBUG
                        printf("add team 2\n");
                        c.print();
#endif
                        league.add_team(c);
                        c.clear();
                    }
                    // New candidate team is {H}
                    c.add_host(h);
                    goto next_host;
                } else {
                    // Otherwise, add H to C.
                    // If C is now full, try to accept it
                    //
                    c.add_host(h);
                    if (c.hosts.size() == params.max_hosts_per_team) {
                        if (c.prune_inner_storage(params.size_inner)) {
                            c.remove(h);
                            continue;
                        }
#ifdef DEBUG
                        printf("add team 3\n");
                        c.print();
#endif
                        league.add_team(c);
                        c.clear();
                        goto next_host;
                    } else {
                        // C is not full; go on
                        goto next_host;
                    }
                }
            }
        } else {
            // There is at least 1 accepted team,
            // so we have to enforce params.max_var_teams
            //
            // C.total_flops is < team0.flops,
            // else we would have already accepted it
            //
            while (1) {
                double x = c.total_flops + h.flops;
                if (x > max_team_flops
                    || (!params.aggressive && x > league.flops0*params.max_var_teams)
                ) {
                    // adding H could make C too fast;
                    // see if we can accept C as is
                    //
                    if (c.prune_inner_storage(params.size_inner)) {
                        continue;
                    }
                    if (c.total_flops < league.flops0/params.max_var_teams) {
                        // C is too slow to accept, but adding H
                        // would make it too fast.
                        // Remove the first host from C and see if this
                        // changes things.
                        //
                        c.remove_first();
                        continue;
                    }
                    // accept C; the new candidate team is {H}
                    //
#ifdef DEBUG
                    printf("add team 4\n");
                    c.print();
#endif
                    league.add_team(c);
                    c.clear();
                    c.add_host(h);
                    goto next_host;
                } else {
                    // adding H to C would not make it too fast
                    // See if H is too slow for C
                    //
                    if (h.flops < c.max_flops/params.max_var_hosts) {
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
                        if (c.total_flops < league.flops0/params.max_var_teams) {
                            c.remove_first();
                            continue;
                        }
#ifdef DEBUG
                        printf("add team 5\n");
                        c.print();
#endif
                        league.add_team(c);
                        c.clear();
                        c.add_host(h);
                        goto next_host;
                    }

                    c.add_host(h);
                    if (c.prune_inner_storage(params.size_inner)) {
                        goto next_host;
                    }

                    // if C is at least league.flops0, accept it
                    //
                    if (c.total_flops >= league.flops0) {
#ifdef DEBUG
                        printf("add team 6\n");
                        c.print();
#endif
                        league.add_team(c);
                        c.clear();
                        goto next_host;
                    }

                    if (c.hosts.size() == params.max_hosts_per_team) {
                        // C is now full.  See if it's fast enough to accept
                        //
                        if (c.total_flops > league.flops0/params.max_var_teams) {
                            // yes - accept C
                            //
#ifdef DEBUG
                            printf("add team 7\n");
                            c.print();
#endif
                            league.add_team(c);
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
    // see if the candidate team is acceptable
    c.prune_inner_storage(params.size_inner);
    if (c.total_flops > league.flops0/params.max_var_teams) {
#ifdef DEBUG
        printf("add team 8\n");
        c.print();
#endif
        league.add_team(c);
    }
#ifdef DEBUG
    printf("find_league3() done:\n");
    league.print();
#endif
}

// Find a league with the constraint that
// team average can't exceed a given value.
//
void find_league2(
    vector<HOST*> &hosts, JOB_PARAMS &params, LEAGUE &league,
    double max_team_flops
) {
#ifdef DEBUG
    printf("find_league2() start: max_team_flops %f\n", max_team_flops);
#endif
    for (int i=params.max_teams; i>0; i--) {
        double min_outer_storage = params.size_outer/i;
        find_league3(
            hosts, params, league, max_team_flops, min_outer_storage
        );
        if (league.teams.size() >= i) {
#ifdef DEBUG
            printf("find_league2(): success\n");
            league.print();
#endif
            return;
        }
    }
    // nothing worked
#ifdef DEBUG
    printf("find_league2(): fail\n");
#endif
    league.clear();
}

// Find a league.
// Explore a range of values for max team flops
// Start at the largest possible value (sum of top N hosts)
// and go down by powers of two.
// Keep going until total FLOPs decreases
//
void find_league(
    vector<HOST*> &hosts, JOB_PARAMS &params, LEAGUE &league
) {
    sort(hosts.begin(), hosts.end(), compare);
    double max_team_flops = hosts[0]->flops*params.max_hosts_per_team;
    double best = 0;
    while (1) {
        LEAGUE lg2;
        find_league2(hosts, params, lg2, max_team_flops);
#ifdef DEBUG
        printf("find_league: solution %f best %f\n", lg2.total_flops, best);
#endif
        if (lg2.total_flops <= best) {
            break;
        }
        best = lg2.total_flops;
        max_team_flops /= 2;
        league = lg2;
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

    params.max_hosts_per_team = 20;
    params.max_teams = 4;
    params.max_var_hosts = 1.5;
    params.max_var_teams = 1.4;
    params.size_outer = 10;
    params.size_inner = 10;
    params.aggressive = false;

    LEAGUE league;
    //find_league(hosts, params, league);
    find_league3(hosts, params, league, 1.5, 0);
    league.print();

}
