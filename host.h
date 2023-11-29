#include <vector>

using std::vector;

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

extern void make_hosts(vector<HOST*>&, bool cpu, bool gpu, bool ipu);
extern void make_hosts_test(vector<HOST*>&);
