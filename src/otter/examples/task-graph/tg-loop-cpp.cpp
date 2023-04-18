#include <stdio.h>
#include <otter/otter-task-graph-wrapper.hpp>

using namespace otter;

static const int NUM_CHILDREN = 5;

int main(int argc, char *argv[])
{
    auto& trace = Otter::get_otter();
    auto& root = trace.get_root_task();
    for (int i=0; i<NUM_CHILDREN; i++) {
        auto child = root.make_child();
    }
    root.synchronise_tasks(otter_sync_children);
    return 0;
}